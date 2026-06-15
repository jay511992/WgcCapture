# GraphicsCapture 调用文档

Windows 平台指定窗口后台截图 DLL，支持 **x86** / **x64**，默认使用 **Windows Graphics Capture (WGC)** 捕获 DirectX 窗口，失败时自动回退 GDI（`PrintWindow` / `BitBlt`）。

---

## 1. 文件说明

| 文件 | 说明 |
|------|------|
| `GraphicsCapture.dll` | 动态库（与调用方架构一致：32 位程序用 x86，64 位程序用 x64） |
| `GraphicsCapture.lib` | 导入库（C/C++ 静态链接符号用） |
| `GraphicsCapture.h` | 头文件 |

运行时需将 `GraphicsCapture.dll` 放在 EXE 同目录，或加入 `PATH`。

---

## 2. 环境要求

- **操作系统**：Windows 10 1803 及以上（WGC 需要；低版本仅能尝试 GDI）
- **架构**：调用方与 DLL 位数必须一致
- **调用约定**：所有导出函数均为 **`__stdcall`**
- **链接方式**：`LoadLibrary` 动态加载，或链接 `GraphicsCapture.lib`

---

## 3. 头文件与链接（C/C++）

```c
#include "GraphicsCapture.h"
```

```text
# MSVC 链接
GraphicsCapture.lib

# 运行时
GraphicsCapture.dll
```

编译 DLL 工程时定义 `GRAPHICSCAPTURE_EXPORTS`；调用方不要定义该宏。

---

## 4. 数据类型

### 4.1 GC_Flags（位标志，可组合）

| 常量 | 值 | 说明 |
|------|----|------|
| `GC_FLAG_NONE` | 0 | 默认，**仅客户区**（不含标题栏/边框） |
| `GC_FLAG_CLIENT_ONLY` | `1 << 0` | 与默认相同，显式指定仅客户区 |
| `GC_FLAG_RENDER_FULL_CONTENT` | `1 << 1` | GDI 路径使用 `PrintWindow` 的 `PW_RENDERFULLCONTENT` |
| `GC_FLAG_FORCE_GDI` | `1 << 2` | 强制仅用 GDI，不使用 WGC |
| `GC_FLAG_INCLUDE_FRAME` | `1 << 3` | 包含标题栏与非客户区边框 |

常用组合：

```c
// 默认：优先 WGC，仅客户区（无标题栏）
GC_FLAG_NONE

// 游戏 / DirectX 窗口（推荐，仍不含标题栏）
GC_FLAG_RENDER_FULL_CONTENT

// 需要标题栏和窗口边框
GC_FLAG_INCLUDE_FRAME

// 整窗 + GDI
GC_FLAG_INCLUDE_FRAME | GC_FLAG_RENDER_FULL_CONTENT

// 强制 GDI（兼容测试）
GC_FLAG_FORCE_GDI | GC_FLAG_RENDER_FULL_CONTENT
```

### 4.2 GC_Result（返回值）

| 常量 | 值 | 说明 |
|------|----|------|
| `GC_OK` | 0 | 成功 |
| `GC_ERROR_INVALID_ARGUMENT` | -1 | 参数无效（如区域宽高 ≤ 0、区域越界） |
| `GC_ERROR_INVALID_WINDOW` | -2 | 无效 `HWND` |
| `GC_ERROR_CAPTURE_FAILED` | -3 | 捕获失败 |
| `GC_ERROR_OUT_OF_MEMORY` | -4 | 内存不足 |
| `GC_ERROR_INTERNAL` | -5 | 内部错误 |
| `GC_ERROR_INVALID_SESSION` | -6 | 无效或未打开的会话 |

失败时可调用 `GC_GetLastErrorMessage()` 获取英文描述。

### 4.3 GC_Image（输出图像）

```c
typedef struct GC_Image {
    uint32_t width;    // 像素宽度
    uint32_t height;   // 像素高度
    uint32_t stride;   // 每行字节数，等于 width * 4
    uint8_t* pixels;   // 像素数据，由 DLL 分配
} GC_Image;
```

- **像素格式**：`BGRA`，每像素 4 字节（蓝、绿、红、Alpha）
- **内存**：`pixels` 由 DLL 内 `malloc` 分配，用完后必须调用 `GC_FreeImage()` 释放
- **行序**：自上而下，无额外 padding（`stride == width * 4`）

### 4.4 GC_ImageBgr24（BGR24 输出图像）

```c
typedef struct GC_ImageBgr24 {
    uint32_t width;    // 像素宽度
    uint32_t height;   // 像素高度
    uint32_t stride;   // 每行字节数，等于 width * 3
    uint8_t* pixels;   // B,G,R 顺序，无 Alpha
} GC_ImageBgr24;
```

- **像素格式**：`BGR24`，每像素 3 字节（蓝、绿、红）
- **内存**：约为同尺寸 BGRA 的 **75%**（例如 1424×861 约 3.51 MB）
- **释放**：调用 `GC_FreeImageBgr24()`
- 所有 `*Bgr24` 捕获 API 的参数、标志、`GC_Rect` 与 BGRA 版本相同

### 4.5 GC_Rect（可选裁剪区域）

```c
typedef struct GC_Rect {
    int32_t x;       // 左上角 X
    int32_t y;       // 左上角 Y
    int32_t width;   // 宽度（必须 > 0）
    int32_t height;  // 高度（必须 > 0）
} GC_Rect;
```

**坐标原点**（相对捕获结果图像左上角）：

| 标志 | 原点 |
|------|------|
| 默认（无 `GC_FLAG_INCLUDE_FRAME`） | 客户区左上角 |
| 有 `GC_FLAG_INCLUDE_FRAME` | 整窗左上角（含标题栏） |

传 `region = NULL` 表示不裁剪，返回完整捕获区域。

---

## 5. 导出函数

### 5.1 GC_CaptureWindow

```c
GC_Result GC_CaptureWindow(
    HWND hwnd,
    GC_Flags flags,
    GC_Image* out_image
);
```

截取指定窗口的完整区域（等价于 `GC_CaptureWindowEx(..., NULL, ...)`）。

| 参数 | 说明 |
|------|------|
| `hwnd` | 目标窗口句柄 |
| `flags` | 捕获标志 |
| `out_image` | 输出图像，不可为 `NULL` |

---

### 5.2 GC_CaptureWindowEx

```c
GC_Result GC_CaptureWindowEx(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_Image* out_image
);
```

支持指定矩形区域截图。

| 参数 | 说明 |
|------|------|
| `region` | 裁剪矩形；`NULL` = 整窗/整客户区 |
| 其余 | 同 `GC_CaptureWindow` |

---

### 5.3 GC_CaptureWindowPrintWindow

```c
GC_Result GC_CaptureWindowPrintWindow(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_Image* out_image
);
```

**专用 GDI 路径**：使用 `PrintWindow` 捕获整窗，再按标志裁剪客户区，最后可选 `region` 二次裁剪。

| 标志 | 说明 |
|------|------|
| `GC_FLAG_RENDER_FULL_CONTENT` | 传入 `PrintWindow` 的 `PW_RENDERFULLCONTENT`；未设置则用 `PrintWindow(..., 0)` |
| 默认客户区 | 不含标题栏；加 `GC_FLAG_INCLUDE_FRAME` 保留边框 |
| 客户区裁剪 | 按 `GetWindowRect` 对齐，避免左侧黑边 |
| 黑屏 | 返回 `GC_ERROR_CAPTURE_FAILED`，**不会**回退到其他截图方式 |

| 特点 | 说明 |
|------|------|
| 不走 WGC | 与其他截图 API 独立，互不共享捕获逻辑 |
| DirectX 窗口 | 部分游戏仍可能黑屏，请改用 `GC_CaptureWindow`（WGC） |

参数与 `GC_CaptureWindowEx` 相同。

---

### 5.3.1 GC_CaptureWindowPrintWindowBgr24

```c
GC_Result GC_CaptureWindowPrintWindowBgr24(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_ImageBgr24* out_image
);
```

与 `GC_CaptureWindowPrintWindow` 相同，输出 **BGR24**。

---

### 5.4 GC_CaptureWindowDesktopDuplication

```c
GC_Result GC_CaptureWindowDesktopDuplication(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_Image* out_image
);
```

**DXGI Desktop Duplication**：截取窗口所在显示器的桌面帧，再按窗口屏幕坐标裁剪。

| 特点 | 说明 |
|------|------|
| 裁剪依据 | `MonitorFromWindow` 定位显示器；默认按**客户区屏幕矩形**裁剪 |
| 窗口被遮挡 | 仍可能截到遮挡物（桌面级复制，非 per-window） |
| 黑屏 | 返回 `GC_ERROR_CAPTURE_FAILED`，**不会**回退到 WGC 或其他方式 |
| 权限 | 需 Windows 8+；独占全屏或部分驱动环境可能 `DuplicateOutput` 失败 |

参数与 `GC_CaptureWindowEx` 相同。

---

### 5.4.1 GC_CaptureWindowDesktopDuplicationBgr24

```c
GC_Result GC_CaptureWindowDesktopDuplicationBgr24(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_ImageBgr24* out_image
);
```

与 `GC_CaptureWindowDesktopDuplication` 相同，输出 **BGR24**。

---

### 5.5 BGR24 主路径 API

```c
GC_Result GC_CaptureWindowBgr24(HWND hwnd, GC_Flags flags, GC_ImageBgr24* out_image);

GC_Result GC_CaptureWindowExBgr24(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_ImageBgr24* out_image);
```

等价于 BGRA 版 `GC_CaptureWindow` / `GC_CaptureWindowEx`，默认优先 **WGC**，仅客户区（无标题栏）。

| 项目 | 说明 |
|------|------|
| 客户区裁剪（WGC/Session） | 多种边框算法取 **最小 crop_x**、**最大 crop_y**，尽量保留左侧 UI |
| 内存 | `width × height × 3` 字节 |
| 释放 | `GC_FreeImageBgr24(&image)` |

---

### 5.6 GC_FreeImage

```c
void GC_FreeImage(GC_Image* image);
```

释放 `GC_CaptureWindow` / `GC_CaptureWindowEx` 分配的 `pixels`。  
可传 `NULL`（无操作）。释放后会把 `width/height/stride/pixels` 清零。

---

### 5.7 GC_FreeImageBgr24

```c
void GC_FreeImageBgr24(GC_ImageBgr24* image);
```

释放所有 `*Bgr24` 捕获 API 分配的 `pixels`。可传 `NULL`。

---

### 5.8 GC_GetLastErrorMessage

```c
const char* GC_GetLastErrorMessage(void);
```

返回线程局部错误字符串（UTF-8 英文），无错误时可能为空串。  
指针仅在同一线程、下一次调用本 DLL 前有效，请及时复制内容。

---

### 5.9 会话复用 API（高频截图推荐）

适用于连续、多次截图。`GC_OpenCaptureSession` 时建立 WGC 会话并保持运行，后续 `GC_CaptureSessionFrame` 只取帧，**显著快于** 每次调用 `GC_CaptureWindow`。

```c
GC_Result GC_OpenCaptureSession(
    HWND hwnd,
    GC_Flags flags,
    GC_CaptureSession** out_session);

GC_Result GC_CaptureSessionFrame(
    GC_CaptureSession* session,
    const GC_Rect* region,
    GC_Image* out_image);

void GC_CloseCaptureSession(GC_CaptureSession* session);
```

```c
GC_Result GC_CaptureSessionFrameBgr24(
    GC_CaptureSession* session,
    const GC_Rect* region,
    GC_ImageBgr24* out_image);
```

| 函数 | 说明 |
|------|------|
| `GC_OpenCaptureSession` | 打开会话；`out_session` 不可为 `NULL` |
| `GC_CaptureSessionFrame` | 从已打开会话取一帧（BGRA） |
| `GC_CaptureSessionFrameBgr24` | 从已打开会话取一帧（BGR24） |
| `GC_CloseCaptureSession` | 关闭并释放会话；可传 `NULL` |

**典型流程：**

```text
Open → CaptureSessionFrameBgr24（循环） → FreeImageBgr24 → CloseCaptureSession
```

**注意：**

- 同一会话不要在多线程中并发调用 `GC_CaptureSessionFrame`
- 窗口大小变化时会自动重建帧池
- `GC_FLAG_FORCE_GDI` 时会话不缓存 WGC，每帧仍走 GDI（提速有限）

---

## 6. 调用流程

### 6.1 单次截图

```text
1. 获取目标 HWND
2. 准备 GC_Image image = {0}
3. 调用 GC_CaptureWindow 或 GC_CaptureWindowEx
4. 若返回 GC_OK，使用 image.pixels（BGRA）
5. 调用 GC_FreeImage(&image)
```

### 6.2 连续截图（会话复用）

```text
1. GC_OpenCaptureSession(hwnd, flags, &session)
2. 循环：GC_CaptureSessionFrame(session, region, &image) → 使用 pixels → GC_FreeImage
3. GC_CloseCaptureSession(session)
```

**线程安全**：不同线程可同时调用；`GC_GetLastErrorMessage` 为线程局部存储。

**COM**：WGC 内部会初始化 WinRT/COM（多线程公寓）。若调用方已初始化 COM，建议使用 `COINIT_MULTITHREADED`，避免与 WGC 冲突。

---

## 7. C 语言示例

### 7.1 整窗截图

```c
#include "GraphicsCapture.h"
#include <stdio.h>

void CaptureFullWindow(HWND hwnd) {
    GC_Image image = {0};
    GC_Result rc = GC_CaptureWindow(
        hwnd,
        GC_FLAG_RENDER_FULL_CONTENT,
        &image);

    if (rc != GC_OK) {
        printf("failed: %d, %s\n", rc, GC_GetLastErrorMessage());
        return;
    }

    printf("size: %ux%u, stride: %u\n", image.width, image.height, image.stride);
    /* 在此处理 image.pixels（BGRA） */

    GC_FreeImage(&image);
}
```

### 7.2 指定区域截图

```c
GC_Rect region = {100, 100, 800, 600};  /* x, y, width, height */

GC_Image image = {0};
GC_Result rc = GC_CaptureWindowEx(
    hwnd,
    GC_FLAG_RENDER_FULL_CONTENT,
    &region,
    &image);
```

### 7.3 BGR24 截图（省内存）

```c
GC_ImageBgr24 image = {0};
GC_Result rc = GC_CaptureWindowExBgr24(
    hwnd,
    GC_FLAG_RENDER_FULL_CONTENT,
    NULL,
    &image);

if (rc == GC_OK) {
    /* image.pixels: B,G,R,...  stride = width * 3 */
    GC_FreeImageBgr24(&image);
}
```

### 7.4 保存为 BMP（示例思路）

每个像素 4 字节 BGRA，写入 BMP 时需设置 `biBitCount = 32`，或使用 WIC / GDI+ 转 PNG。

---

## 8. C++ 示例

```cpp
#include "GraphicsCapture.h"
#include <vector>

bool CaptureClientRegion(HWND hwnd, int x, int y, int w, int h,
                         std::vector<uint8_t>& out, uint32_t& out_w, uint32_t& out_h) {
    GC_Rect region{x, y, w, h};
    GC_Image image{};
    const GC_Result rc = GC_CaptureWindowEx(
        hwnd,
        static_cast<GC_Flags>(GC_FLAG_CLIENT_ONLY | GC_FLAG_RENDER_FULL_CONTENT),
        &region,
        &image);

    if (rc != GC_OK) {
        return false;
    }

    out.assign(image.pixels, image.pixels + image.stride * image.height);
    out_w = image.width;
    out_h = image.height;
    GC_FreeImage(&image);
    return true;
}
```

### 7.5 会话复用（连续截图，BGR24）

```cpp
GC_CaptureSession* session = nullptr;
if (GC_OpenCaptureSession(hwnd, GC_FLAG_RENDER_FULL_CONTENT, &session) != GC_OK) {
    return;
}

for (int i = 0; i < 100; ++i) {
    GC_ImageBgr24 image{};
    if (GC_CaptureSessionFrameBgr24(session, nullptr, &image) == GC_OK) {
        // use image.pixels (BGR24)
        GC_FreeImageBgr24(&image);
    }
}

GC_CloseCaptureSession(session);
```

---

## 9. Python（ctypes）示例

```python
import ctypes
from ctypes import wintypes

gc = ctypes.WinDLL("GraphicsCapture.dll")

class GC_Image(ctypes.Structure):
    _fields_ = [
        ("width", wintypes.UINT),
        ("height", wintypes.UINT),
        ("stride", wintypes.UINT),
        ("pixels", ctypes.POINTER(ctypes.c_uint8)),
    ]

class GC_Rect(ctypes.Structure):
    _fields_ = [
        ("x", ctypes.c_int32),
        ("y", ctypes.c_int32),
        ("width", ctypes.c_int32),
        ("height", ctypes.c_int32),
    ]

GC_CaptureWindowEx = gc.GC_CaptureWindowEx
GC_CaptureWindowEx.argtypes = [wintypes.HWND, wintypes.UINT, ctypes.POINTER(GC_Rect), ctypes.POINTER(GC_Image)]
GC_CaptureWindowEx.restype = ctypes.c_int

GC_FreeImage = gc.GC_FreeImage
GC_FreeImage.argtypes = [ctypes.POINTER(GC_Image)]
GC_FreeImage.restype = None

hwnd = ...  # 目标窗口句柄
image = GC_Image()
rc = GC_CaptureWindowEx(hwnd, 2, None, ctypes.byref(image))  # flags=GC_FLAG_RENDER_FULL_CONTENT
if rc == 0:
    size = image.stride * image.height
    buf = bytes(image.pixels[:size])
    GC_FreeImage(ctypes.byref(image))
```

注意：Python 中 `WinDLL` 默认使用 **`stdcall`**，与 DLL 一致。

---

## 10. 易语言 / Delphi / VB 声明要点

| 项目 | 值 |
|------|----|
| 调用约定 | `stdcall` |
| DLL 名 | `GraphicsCapture.dll` |
| 字符集 | `GC_GetLastErrorMessage` 返回 `PAnsiChar` / `LPCSTR` |

**x86 导出符号**（带 `@` 栈字节数，以实际 `dumpbin /exports` 为准）：

```text
_GC_CaptureWindow@12
_GC_CaptureWindowEx@16
_GC_CaptureWindowBgr24@12
_GC_CaptureWindowExBgr24@16
_GC_CaptureWindowPrintWindow@16
_GC_CaptureWindowPrintWindowBgr24@16
_GC_CaptureWindowDesktopDuplication@16
_GC_CaptureWindowDesktopDuplicationBgr24@16
_GC_FreeImage@4
_GC_FreeImageBgr24@4
_GC_GetLastErrorMessage@0
_GC_OpenCaptureSession@12
_GC_CaptureSessionFrame@12
_GC_CaptureSessionFrameBgr24@12
_GC_CloseCaptureSession@4
```

**x64** 导出名为 undecorated：`GC_CaptureWindow` 等，无 `@N` 后缀。

---

## 11. 获取 HWND 的常见方式

```c
// 前台窗口
HWND hwnd = GetForegroundWindow();

// 按窗口标题
HWND hwnd = FindWindowW(L"ClassName", L"Window Title");

// 按进程 PID 枚举（见 examples/capture_gw2.cpp 中的 FindMainWindow）
```

---

## 12. 命令行示例程序

构建后位于 `out/x64` 或 `out/x86`：

| 程序 | 说明 |
|------|------|
| `CaptureGw2.exe` | WGC 截 GW2，默认 `GC_CaptureWindowExBgr24`，输出 PNG |
| `TestPrintWindow.exe` | `GC_CaptureWindowPrintWindowBgr24` + `PW_RENDERFULLCONTENT` |
| `TestDesktopDup.exe` | `GC_CaptureWindowDesktopDuplicationBgr24` |
| `GCSessionExample.exe` | `GC_CaptureSessionFrameBgr24` 连拍测试 |

```powershell
# WGC 客户区截图（BGR24 → 保存 PNG）
CaptureGw2.exe gw2.png

# PrintWindow 测试
TestPrintWindow.exe gw2_print.png

# 指定区域：x y 宽 高
CaptureGw2.exe gw2_crop.png 100 100 800 600
```

---

## 13. 限制与说明

1. **最小化窗口**：WGC 通常无法捕获；请保持窗口可见（可后台、可被遮挡）。
2. **独占全屏游戏**：可能失败，建议无边框窗口模式。
3. **DirectX / GPU 应用**：请使用默认 WGC 路径；`GC_FLAG_FORCE_GDI` 对游戏常会黑屏。
4. **Protected Content**：DRM 保护内容无法捕获。
5. **DPI**：捕获结果为像素级 bitmap，与窗口实际像素尺寸一致；默认 **Per-Monitor DPI V2**。
6. **客户区**：默认不含标题栏；`GC_FLAG_INCLUDE_FRAME` 保留边框。WGC/Session 与 PrintWindow 使用不同客户区对齐策略。
7. **性能**：每次调用完成一次捕获；高频场景请用 Session API 或 `*Bgr24` 降低内存。

---

## 14. 编译产物路径

```text
out/x64/GraphicsCapture.dll   # 64 位
out/x86/GraphicsCapture.dll   # 32 位
include/GraphicsCapture.h
```

重新编译：

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

---

## 15. 错误排查

| 现象 | 可能原因 |
|------|----------|
| 黑屏 | 使用了 GDI；去掉 `GC_FLAG_FORCE_GDI`，或游戏在独占全屏 |
| 返回 -2 | HWND 无效 |
| 返回 -1 且 region 相关 | 裁剪区域超出图像范围 |
| 返回 -3 | WGC/GDI 均失败；窗口不可见或超时 |
| 调用崩溃 | 调用约定错误（必须用 `stdcall`）；或 x86/x64 混用 |
| COM 相关崩溃 | 调用方 COM 公寓与 DLL 冲突，改用 `COINIT_MULTITHREADED` |

---

## 16. 版本信息

- **API 版本**：1.0  
- **C 标准接口**：`extern "C"`  
- **C++ 标准（内部实现）**：C++17  
- **像素格式**：32 bpp BGRA  

如有新接口或行为变更，以 `include/GraphicsCapture.h` 为准。
