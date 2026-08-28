# TFTDx11Watcher

[English](README.md) | Tiếng Việt

`TFTDx11Watcher.exe` là helper native Win32 rất nhỏ, viết bằng C thuần. Ứng dụng giữ cho Teamfight Tactics (TFT) chạy bằng DirectX 11 trên các máy mà phiên bản Unreal Engine mới bị crash khi dùng đường DirectX 12 mặc định.

## Vì sao cần ứng dụng này?

TFT gần đây đã chuyển sang **Unreal Engine**. Bản game mới mặc định khởi chạy với **DirectX 12**.

Một số card đồ họa cũ hoặc GPU tích hợp cũ—ví dụ phần cứng cùng phân khúc GTX 750 và các iGPU đời cũ—chỉ có đường DirectX 11 thực tế phù hợp, hoặc không cung cấp đủ feature set DirectX 12 mà bản game mới yêu cầu. Trên các máy bị ảnh hưởng, TFT có thể crash ngay khi khởi động với dialog như sau:

![Dialog lỗi fatal của TFT](docs/TFT_Fatal_error.png)

Nội dung dialog:

```text
The UE-TFT Game has crashed and will close
Fatal error!
```

Cách khắc phục thủ công thường là sửa `Engine.ini` trước khi mở game và thêm:

```ini
[/Script/WindowsTargetPlatform.WindowsTargetSettings]
DefaultGraphicsRHI=DefaultGraphicsRHI_DX11
```

Cách này khá phiền vì TFT có thể ghi lại `Engine.ini` sau khi đọc config và xóa section DX11. Người dùng phải sửa file lại trước những lần launch sau.

TFTDx11Watcher tự động hóa workaround này. Khi cần, nó khôi phục thiết lập DX11 để lần launch TFT bình thường tiếp theo từ Riot/League client dùng DirectX 11. Ứng dụng có thể sửa đúng lỗi crash startup liên quan tới graphics backend này; không đảm bảo sửa các lỗi crash khác.

## Tải xuống

Tải bản phát hành đóng gói tại [GitHub Releases 1.0.0](https://github.com/phamngocduy98/LOL_TFT_DX11/releases/tag/1.0.0).

## Ứng dụng làm gì?

- Đọc `%LOCALAPPDATA%`, không hard-code username.
- Tạo thư mục config TFT và block ban đầu trong `Engine.ini` khi cần lúc startup.
- Theo dõi thư mục config bằng `ReadDirectoryChangesW()`.
- Chỉ append lại DX11 block sau khi TFT modify, replace, recreate hoặc rename `Engine.ini`.
- Dùng `OPEN_EXISTING` trong event path, không tự tạo file cạnh tranh với quá trình replace của TFT.
- Verify marker sau khi append và retry ngắn nếu TFT lập tức rewrite file.

Ứng dụng không launch TFT, không inject, không hook process, không sửa file trong `D:\Riot Games`, không bypass Riot/Vanguard và không thay đổi/reset `CachedClientID`.

## Build

Mở **x64 Native Tools Command Prompt for Visual Studio** hoặc **Developer Command Prompt for VS**, chuyển tới thư mục project rồi chạy:

```cmd
build.cmd
```

Script dùng MSVC `cl.exe` và `rc.exe`, build Windows-subsystem release với `/O2 /GL /W4 /WX`, link-time optimization và loại bỏ code không dùng. Icon được nhúng qua `TFTDx11Watcher.rc` từ `appicon.ico` đa kích thước, là bản chuyển đổi từ `appicon.png` đã cung cấp.

## Test

Sau khi build:

```cmd
test.cmd
```

Script kiểm tra executable, start watcher, in nội dung `Engine.ini` và hướng dẫn launch TFT bình thường. Sau khi TFT exit, kiểm tra lại `Engine.ini`. Watcher không tự launch TFT.

## Install

Để watcher chạy khi user logon:

```cmd
install-task.cmd
```

Script tạo Scheduled Task tên `TFT DX11 Watcher`, trigger `ONLOGON`, quyền `LIMITED`, dùng absolute path tới executable trong thư mục hiện tại và start task ngay. Không cần Administrator nếu Windows cho phép user hiện tại tạo task.

## Uninstall

```cmd
uninstall-task.cmd
```

Script xóa scheduled task và dừng `TFTDx11Watcher.exe` nếu đang chạy. Nó không xóa hoặc reset `Engine.ini`, DX11 hay `CachedClientID`.

## How it works

1. Watcher lấy `LOCALAPPDATA` bằng `GetEnvironmentVariableW()` và đảm bảo thư mục config TFT tồn tại.
2. Khi startup, watcher mở hoặc tạo `Engine.ini`, rồi quét raw bytes để tìm chính xác marker `DefaultGraphicsRHI=DefaultGraphicsRHI_DX11`.
3. Sau filesystem event, watcher chỉ dùng `OPEN_EXISTING`. Nếu TFT tạm thời delete hoặc rename file, watcher chờ và retry mà không tự tạo file thay thế.
4. Nếu thiếu marker, watcher append block ASCII/UTF-8-compatible bằng CRLF và không rewrite nội dung hiện có.
5. Watcher đóng append handle, mở lại file để verify marker và retry ngắn nếu TFT rewrite đồng thời làm mất marker.
6. Khi idle, worker thread duy nhất block trong `ReadDirectoryChangesW()`; kết quả notification zero-byte sẽ trigger một lần reconcile để xử lý khả năng buffer overflow.

Named mutex `Local\TFTDx11Watcher.Singleton` ngăn nhiều instance chạy cùng lúc.

## Resource usage và an toàn

Không có polling loop dài hạn và không có vòng `Sleep + check file` liên tục. Khi idle, thread block trong kernel chờ filesystem notification nên CPU thực tế gần 0%. Process chỉ dùng một thread, buffer notification nhỏ và các handle directory/file/mutex cần thiết.

Process và thread chạy ở mức priority thấp hơn. Đây là ứng dụng Win32 user-mode bình thường, không có console window, không cần Administrator, không inject game và không bypass Riot/Vanguard.

## Limitations

- Retry file lock được giữ ngắn và chỉ xảy ra sau startup hoặc filesystem event. Nếu file bị khóa quá lâu, có thể cần event sau đó để reconcile lại.
- Event path không tự tạo lại `Engine.ini` nếu TFT đã xóa nhưng chưa recreate file.
- Detection dựa trên raw bytes và không convert encoding. File config TFT được giả định tương thích ASCII/UTF-8.
- Setting được match chính xác. Biến thể whitespace khác sẽ được coi là thiếu và block chuẩn sẽ được append.
- Watcher chỉ chuẩn bị config cho lần launch TFT tiếp theo; không sửa game đang chạy.
