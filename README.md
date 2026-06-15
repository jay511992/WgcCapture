# WgcCapture

Windows 后台窗口截图 DLL，支持 **x86 / x64**，提供 WGC、PrintWindow、Desktop Duplication 三种捕获路径，以及 BGR24 低内存输出。

## 下载

在 [Releases](https://github.com/jay511992/WgcCapture/releases) 页面下载预编译包：

| 包 | 适用 |
|----|------|
| `WgcCapture-x86.zip` | 32 位程序 / 易语言 |
| `WgcCapture-x64.zip` | 64 位程序 |

每个包内含 `GraphicsCapture.dll`、`GraphicsCapture.lib`、`GraphicsCapture.h`、测试工具及文档。

## 快速开始

```c
#include "GraphicsCapture.h"

GC_ImageBgr24* img = nullptr;
GC_Result r = GC_CaptureWindowBgr24(hwnd, GC_FLAG_NONE, &img);
if (r == GC_OK) {
    // img->data: BGR 像素, img->width x img->height
    GC_FreeImageBgr24(img);
}
```

## 文档

- [API 文档](docs/API.md)
- [易语言声明](docs/易语言声明.md)

## 自行编译

```powershell
.\build.ps1
```

产物输出到 `out\x86\` 和 `out\x64\`。

## 环境要求

- Windows 10 1803+
- 调用方与 DLL 架构一致（32 位程序用 x86 DLL）
