#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "shell32.lib")

static const WCHAR kMutexName[] = L"Local\\TFTDx11Watcher.Singleton";
static const WCHAR kEngineFileName[] = L"Engine.ini";
static const char kDx11Marker[] = "DefaultGraphicsRHI=DefaultGraphicsRHI_DX11";
static const char kDx11Block[] =
    "[/Script/WindowsTargetPlatform.WindowsTargetSettings]\r\n"
    "DefaultGraphicsRHI=DefaultGraphicsRHI_DX11\r\n";

#define DX11_MARKER_LENGTH (sizeof(kDx11Marker) - 1u)
#define ENGINE_FILE_NAME_LENGTH ((int)(sizeof(kEngineFileName) / sizeof(kEngineFileName[0]) - 1u))

#define RETRY_COUNT 8u
#define RETRY_DELAY_MS 50u
#define EVENT_SETTLE_DELAY_MS 100u
#define DIRECTORY_RETRY_DELAY_MS 500u
#define DIRECTORY_BUFFER_SIZE (16u * 1024u)

/* Kept volatile so the watcher has a real cleanup path if shutdown is added later. */
static volatile BOOL g_watcherRunning = TRUE;

static WCHAR *GetLocalAppDataPath(void)
{
    DWORD capacity = 512u;

    for (;;) {
        SIZE_T allocationBytes;
        WCHAR *buffer;
        DWORD length;

        if ((SIZE_T)capacity > ((SIZE_T)-1) / sizeof(WCHAR)) {
            return NULL;
        }

        allocationBytes = (SIZE_T)capacity * sizeof(WCHAR);
        buffer = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, allocationBytes);
        if (buffer == NULL) {
            return NULL;
        }

        SetLastError(ERROR_SUCCESS);
        length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, capacity);
        if (length != 0u && length < capacity) {
            return buffer;
        }

        HeapFree(GetProcessHeap(), 0, buffer);

        if (length == 0u || capacity > ((DWORD)-1) / 2u) {
            return NULL;
        }

        capacity *= 2u;
    }
}

static WCHAR *JoinPath(const WCHAR *base, const WCHAR *leaf)
{
    const SIZE_T maxSize = (SIZE_T)-1;
    SIZE_T baseLength = wcslen(base);
    SIZE_T leafLength = wcslen(leaf);
    BOOL needsSeparator = FALSE;
    SIZE_T characterCount = baseLength;
    SIZE_T position = 0u;
    WCHAR *result;

    if (baseLength != 0u) {
        WCHAR last = base[baseLength - 1u];
        needsSeparator = (last != L'\\' && last != L'/');
    }

    if (needsSeparator) {
        if (characterCount == maxSize) {
            return NULL;
        }
        ++characterCount;
    }

    if (leafLength > maxSize - characterCount) {
        return NULL;
    }
    characterCount += leafLength;

    if (characterCount == maxSize) {
        return NULL;
    }
    ++characterCount;

    if (characterCount > maxSize / sizeof(WCHAR)) {
        return NULL;
    }

    result = (WCHAR *)HeapAlloc(
        GetProcessHeap(), 0, characterCount * sizeof(WCHAR));
    if (result == NULL) {
        return NULL;
    }

    if (baseLength != 0u) {
        memcpy(result + position, base, baseLength * sizeof(WCHAR));
        position += baseLength;
    }
    if (needsSeparator) {
        result[position++] = L'\\';
    }
    if (leafLength != 0u) {
        memcpy(result + position, leaf, leafLength * sizeof(WCHAR));
        position += leafLength;
    }
    result[position] = L'\0';

    return result;
}

static BOOL EnsureDirectoryTree(const WCHAR *directoryPath)
{
    int result = SHCreateDirectoryExW(NULL, directoryPath, NULL);

    return result == ERROR_SUCCESS ||
           result == ERROR_FILE_EXISTS ||
           result == ERROR_ALREADY_EXISTS;
}

static BOOL FileContainsDx11Marker(HANDLE fileHandle, BOOL *containsMarker)
{
    BYTE readBuffer[8192];
    SIZE_T prefixTable[DX11_MARKER_LENGTH];
    SIZE_T prefixLength = 0u;
    LARGE_INTEGER fileOffset;

    fileOffset.QuadPart = 0;
    if (!SetFilePointerEx(fileHandle, fileOffset, NULL, FILE_BEGIN)) {
        return FALSE;
    }

    prefixTable[0] = 0u;
    for (SIZE_T index = 1u; index < DX11_MARKER_LENGTH; ++index) {
        while (prefixLength > 0u &&
               kDx11Marker[index] != kDx11Marker[prefixLength]) {
            prefixLength = prefixTable[prefixLength - 1u];
        }
        if (kDx11Marker[index] == kDx11Marker[prefixLength]) {
            ++prefixLength;
        }
        prefixTable[index] = prefixLength;
    }

    prefixLength = 0u;
    for (;;) {
        DWORD bytesRead = 0u;

        if (!ReadFile(fileHandle, readBuffer, (DWORD)sizeof(readBuffer),
                      &bytesRead, NULL)) {
            return FALSE;
        }
        if (bytesRead == 0u) {
            break;
        }

        for (DWORD index = 0u; index < bytesRead; ++index) {
            BYTE value = readBuffer[index];

            while (prefixLength > 0u &&
                   value != (BYTE)kDx11Marker[prefixLength]) {
                prefixLength = prefixTable[prefixLength - 1u];
            }
            if (value == (BYTE)kDx11Marker[prefixLength]) {
                ++prefixLength;
            }
            if (prefixLength == DX11_MARKER_LENGTH) {
                *containsMarker = TRUE;
                return TRUE;
            }
        }
    }

    *containsMarker = FALSE;
    return TRUE;
}

static BOOL WriteBytes(HANDLE fileHandle, const char *data, SIZE_T length)
{
    SIZE_T offset = 0u;

    while (offset < length) {
        SIZE_T remaining = length - offset;
        DWORD requestSize = remaining > (SIZE_T)MAXDWORD
                                ? MAXDWORD
                                : (DWORD)remaining;
        DWORD bytesWritten = 0u;

        if (!WriteFile(fileHandle, data + offset, requestSize,
                       &bytesWritten, NULL) ||
            bytesWritten == 0u || bytesWritten > requestSize) {
            return FALSE;
        }
        offset += (SIZE_T)bytesWritten;
    }

    return TRUE;
}

static BOOL AppendDx11Block(HANDLE fileHandle, BOOL addSeparator)
{
    char payload[4u + sizeof(kDx11Block)];
    SIZE_T payloadLength = 0u;

    if (addSeparator) {
        payload[payloadLength++] = '\r';
        payload[payloadLength++] = '\n';
        payload[payloadLength++] = '\r';
        payload[payloadLength++] = '\n';
    }

    memcpy(payload + payloadLength, kDx11Block, sizeof(kDx11Block) - 1u);
    payloadLength += sizeof(kDx11Block) - 1u;

    return WriteBytes(fileHandle, payload, payloadLength);
}

static BOOL EnsureDx11Setting(const WCHAR *filePath, BOOL allowCreate)
{
    DWORD creationDisposition = allowCreate ? OPEN_ALWAYS : OPEN_EXISTING;

    for (DWORD attempt = 0u; attempt < RETRY_COUNT; ++attempt) {
        HANDLE fileHandle;
        LARGE_INTEGER fileSize;
        BOOL containsMarker = FALSE;
        BOOL readSucceeded;

        fileHandle = CreateFileW(
            filePath,
            GENERIC_READ | FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            creationDisposition,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (fileHandle == INVALID_HANDLE_VALUE) {
            if (attempt + 1u < RETRY_COUNT) {
                Sleep(RETRY_DELAY_MS);
            }
            continue;
        }

        fileSize.QuadPart = 0;
        readSucceeded = GetFileSizeEx(fileHandle, &fileSize) &&
                        fileSize.QuadPart >= 0 &&
                        FileContainsDx11Marker(fileHandle, &containsMarker);

        if (readSucceeded && containsMarker) {
            CloseHandle(fileHandle);
            return TRUE;
        }

        if (readSucceeded &&
            AppendDx11Block(fileHandle, fileSize.QuadPart > 0)) {
            CloseHandle(fileHandle);

            /*
             * TFT can still finish a truncate/rewrite after our append.
             * Verify through a fresh OPEN_EXISTING handle and retry if the
             * setting was removed by that concurrent rewrite.
             */
            Sleep(RETRY_DELAY_MS);
            fileHandle = CreateFileW(
                filePath,
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL);
            if (fileHandle != INVALID_HANDLE_VALUE) {
                BOOL verifiedMarker = FALSE;
                BOOL verifySucceeded = FileContainsDx11Marker(
                    fileHandle, &verifiedMarker);

                CloseHandle(fileHandle);
                if (verifySucceeded && verifiedMarker) {
                    return TRUE;
                }
            }
        } else {
            CloseHandle(fileHandle);
        }

        if (attempt + 1u < RETRY_COUNT) {
            Sleep(RETRY_DELAY_MS);
        }
    }

    return FALSE;
}

static BOOL NotificationIsForEngineIni(
    const BYTE *notificationBuffer, DWORD bytesReturned)
{
    const DWORD headerSize = (DWORD)offsetof(FILE_NOTIFY_INFORMATION, FileName);
    DWORD offset = 0u;

    while (offset < bytesReturned) {
        DWORD remaining = bytesReturned - offset;
        const FILE_NOTIFY_INFORMATION *notification;
        DWORD entrySize;
        DWORD nameCharacterCount;

        if (remaining < headerSize) {
            break;
        }

        notification = (const FILE_NOTIFY_INFORMATION *)(
            notificationBuffer + offset);
        entrySize = notification->NextEntryOffset == 0u
                        ? remaining
                        : notification->NextEntryOffset;

        if (entrySize < headerSize || entrySize > remaining ||
            notification->FileNameLength > entrySize - headerSize ||
            notification->FileNameLength % sizeof(WCHAR) != 0u) {
            break;
        }

        nameCharacterCount = notification->FileNameLength / sizeof(WCHAR);
        if (nameCharacterCount == (DWORD)ENGINE_FILE_NAME_LENGTH &&
            CompareStringOrdinal(
                notification->FileName,
                (int)nameCharacterCount,
                kEngineFileName,
                ENGINE_FILE_NAME_LENGTH,
                TRUE) == CSTR_EQUAL) {
            return TRUE;
        }

        if (notification->NextEntryOffset == 0u) {
            break;
        }
        offset += notification->NextEntryOffset;
    }

    return FALSE;
}

static void WatchDirectory(const WCHAR *directoryPath, const WCHAR *filePath)
{
    DWORD notificationBuffer[DIRECTORY_BUFFER_SIZE / sizeof(DWORD)];

    for (; g_watcherRunning; ) {
        HANDLE directoryHandle;

        if (!EnsureDirectoryTree(directoryPath)) {
            Sleep(DIRECTORY_RETRY_DELAY_MS);
            continue;
        }

        directoryHandle = CreateFileW(
            directoryPath,
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            NULL);
        if (directoryHandle == INVALID_HANDLE_VALUE) {
            Sleep(DIRECTORY_RETRY_DELAY_MS);
            continue;
        }

        for (;;) {
            DWORD bytesReturned = 0u;
            BOOL readSucceeded = ReadDirectoryChangesW(
                directoryHandle,
                (void *)notificationBuffer,
                (DWORD)sizeof(notificationBuffer),
                FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME |
                    FILE_NOTIFY_CHANGE_LAST_WRITE |
                    FILE_NOTIFY_CHANGE_SIZE,
                &bytesReturned,
                NULL,
                NULL);

            if (!readSucceeded) {
                CloseHandle(directoryHandle);
                Sleep(DIRECTORY_RETRY_DELAY_MS);
                break;
            }

            if (bytesReturned == 0u) {
                /* A zero-byte result means the notification buffer overflowed. */
                Sleep(EVENT_SETTLE_DELAY_MS);
                (void)EnsureDx11Setting(filePath, FALSE);
                continue;
            }

            if (NotificationIsForEngineIni(
                    (const BYTE *)notificationBuffer, bytesReturned)) {
                Sleep(EVENT_SETTLE_DELAY_MS);
                (void)EnsureDx11Setting(filePath, FALSE);
            }
        }
    }
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE previousInstance,
    PWSTR commandLine,
    int showCommand)
{
    HANDLE singletonMutex;
    WCHAR *localAppDataPath;
    WCHAR *directoryPath;
    WCHAR *filePath;
    int exitCode = 1;

    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(previousInstance);
    UNREFERENCED_PARAMETER(commandLine);
    UNREFERENCED_PARAMETER(showCommand);

    singletonMutex = CreateMutexW(NULL, FALSE, kMutexName);
    if (singletonMutex == NULL) {
        return exitCode;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singletonMutex);
        return 0;
    }

    (void)SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);

    localAppDataPath = GetLocalAppDataPath();
    if (localAppDataPath == NULL) {
        CloseHandle(singletonMutex);
        return exitCode;
    }

    directoryPath = JoinPath(
        localAppDataPath, L"TFT\\Saved\\Config\\WindowsClient");
    HeapFree(GetProcessHeap(), 0, localAppDataPath);
    if (directoryPath == NULL) {
        CloseHandle(singletonMutex);
        return exitCode;
    }

    filePath = JoinPath(directoryPath, kEngineFileName);
    if (filePath == NULL) {
        HeapFree(GetProcessHeap(), 0, directoryPath);
        CloseHandle(singletonMutex);
        return exitCode;
    }

    (void)EnsureDirectoryTree(directoryPath);
    (void)EnsureDx11Setting(filePath, TRUE);
    exitCode = 0;
    WatchDirectory(directoryPath, filePath);

    HeapFree(GetProcessHeap(), 0, filePath);
    HeapFree(GetProcessHeap(), 0, directoryPath);
    CloseHandle(singletonMutex);
    return exitCode;
}
