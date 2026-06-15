#include "WindowCapture.h"

#include <windows.h>
#include <dwmapi.h>

#include <cstring>
#include <string>

#pragma comment(lib, "dwmapi.lib")

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace gc {
namespace {

class GdiHandle {
public:
    GdiHandle() = default;
    explicit GdiHandle(HGDIOBJ handle) : handle_(handle) {}
    ~GdiHandle() { Reset(); }

    GdiHandle(const GdiHandle&) = delete;
    GdiHandle& operator=(const GdiHandle&) = delete;

    void Reset(HGDIOBJ handle = nullptr) {
        if (handle_ != nullptr) {
            DeleteObject(handle_);
        }
        handle_ = handle;
    }

    HGDIOBJ Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HGDIOBJ handle_ = nullptr;
};

class DeviceContext {
public:
    DeviceContext() = default;
    DeviceContext(HDC dc, bool owns) : dc_(dc), owns_(owns) {}
    ~DeviceContext() { Reset(); }

    DeviceContext(const DeviceContext&) = delete;
    DeviceContext& operator=(const DeviceContext&) = delete;

    void Reset() {
        if (owns_ && dc_ != nullptr) {
            DeleteDC(dc_);
        }
        dc_ = nullptr;
        owns_ = false;
    }

    HDC Get() const { return dc_; }
    explicit operator bool() const { return dc_ != nullptr; }

private:
    HDC dc_ = nullptr;
    bool owns_ = false;
};

class WindowDeviceContext {
public:
    explicit WindowDeviceContext(HWND hwnd) : hwnd_(hwnd), dc_(GetWindowDC(hwnd)) {}
    ~WindowDeviceContext() { Reset(); }

    WindowDeviceContext(const WindowDeviceContext&) = delete;
    WindowDeviceContext& operator=(const WindowDeviceContext&) = delete;

    HDC Get() const { return dc_; }
    explicit operator bool() const { return dc_ != nullptr; }

private:
    void Reset() {
        if (hwnd_ != nullptr && dc_ != nullptr) {
            ReleaseDC(hwnd_, dc_);
        }
        hwnd_ = nullptr;
        dc_ = nullptr;
    }

    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
};

std::optional<CaptureResult> ReadBitmapPixels(HDC dc, HBITMAP bitmap, int width, int height) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    CaptureResult result{};
    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);
    result.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4U);

    if (GetDIBits(
            dc,
            bitmap,
            0,
            static_cast<UINT>(height),
            result.pixels.data(),
            &info,
            DIB_RGB_COLORS) == 0) {
        return std::nullopt;
    }

    return result;
}

std::optional<CaptureResult> CaptureWithPrintWindow(
    HWND hwnd,
    int bitmap_width,
    int bitmap_height,
    bool use_full_content,
    std::string& error) {
    WindowDeviceContext window_dc(hwnd);
    if (!window_dc) {
        error = "Failed to get window device context.";
        return std::nullopt;
    }

    DeviceContext mem_dc(CreateCompatibleDC(window_dc.Get()), true);
    if (!mem_dc) {
        error = "Failed to create compatible device context.";
        return std::nullopt;
    }

    GdiHandle bitmap(CreateCompatibleBitmap(window_dc.Get(), bitmap_width, bitmap_height));
    if (!bitmap) {
        error = "Failed to create compatible bitmap.";
        return std::nullopt;
    }

    const HGDIOBJ previous_object = SelectObject(mem_dc.Get(), bitmap.Get());
    if (previous_object == nullptr) {
        error = "Failed to select bitmap into device context.";
        return std::nullopt;
    }

    const UINT print_flags = use_full_content ? PW_RENDERFULLCONTENT : 0U;
    const bool captured = PrintWindow(hwnd, mem_dc.Get(), print_flags) != 0;

    SelectObject(mem_dc.Get(), previous_object);

    if (!captured) {
        error = use_full_content
            ? "PrintWindow with PW_RENDERFULLCONTENT failed."
            : "PrintWindow failed.";
        return std::nullopt;
    }

    auto result = ReadBitmapPixels(
        mem_dc.Get(), static_cast<HBITMAP>(bitmap.Get()), bitmap_width, bitmap_height);
    if (!result.has_value()) {
        error = "Failed to read PrintWindow bitmap pixels.";
        return std::nullopt;
    }

    OpaqueAlphaChannel(result->pixels);
    return result;
}

std::optional<CaptureResult> FinalizeCapturedFrame(
    HWND hwnd,
    bool client_only,
    CaptureResult frame,
    const CaptureRegion* region,
    std::string& error) {
    if (client_only) {
        auto cropped = CropClientAreaFromPrintWindow(hwnd, std::move(frame), error);
        if (!cropped.has_value()) {
            return std::nullopt;
        }
        frame = std::move(*cropped);
    }

    if (region != nullptr) {
        return CropImageRegion(std::move(frame), *region, error);
    }

    return frame;
}

}  // namespace

std::optional<CaptureResult> CaptureWindowPrintWindow(
    HWND hwnd,
    GC_Flags flags,
    const CaptureRegion* region,
    std::string& error) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        error = "Invalid window handle.";
        return std::nullopt;
    }

    EnsureCaptureDpiAwareness();

    const bool client_only = (flags & GC_FLAG_INCLUDE_FRAME) == 0;
    const bool use_full_content = (flags & GC_FLAG_RENDER_FULL_CONTENT) != 0;

    RECT window_rect{};
    if (!GetCaptureReferenceScreenRect(hwnd, window_rect)) {
        error = "Failed to query window bounds for PrintWindow.";
        return std::nullopt;
    }

    const int bitmap_width = window_rect.right - window_rect.left;
    const int bitmap_height = window_rect.bottom - window_rect.top;
    if (bitmap_width <= 0 || bitmap_height <= 0) {
        error = "Window bounds are empty.";
        return std::nullopt;
    }

    auto captured = CaptureWithPrintWindow(
        hwnd, bitmap_width, bitmap_height, use_full_content, error);
    if (!captured.has_value()) {
        return std::nullopt;
    }

    auto finalized = FinalizeCapturedFrame(hwnd, client_only, std::move(*captured), region, error);
    if (!finalized.has_value()) {
        return std::nullopt;
    }

    if (IsCaptureMostlyBlack(*finalized)) {
        error = "PrintWindow captured a black image.";
        return std::nullopt;
    }

    return finalized;
}

}  // namespace gc
