#include "WindowCapture.h"

#include <dwmapi.h>

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <mutex>
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

    GdiHandle(GdiHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    GdiHandle& operator=(GdiHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

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

    DeviceContext(DeviceContext&& other) noexcept
        : dc_(other.dc_), owns_(other.owns_) {
        other.dc_ = nullptr;
        other.owns_ = false;
    }

    DeviceContext& operator=(DeviceContext&& other) noexcept {
        if (this != &other) {
            Reset();
            dc_ = other.dc_;
            owns_ = other.owns_;
            other.dc_ = nullptr;
            other.owns_ = false;
        }
        return *this;
    }

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

struct CaptureRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

bool IsWindowCapturable(HWND hwnd, std::string& error) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        error = "Invalid window handle.";
        return false;
    }

    return true;
}

std::optional<CaptureRect> GetCaptureRect(HWND hwnd, bool client_only) {
    const auto screen = GetWindowScreenRect(hwnd, client_only);
    if (!screen.has_value()) {
        return std::nullopt;
    }

    return CaptureRect{
        screen->x,
        screen->y,
        screen->width,
        screen->height,
    };
}

bool CaptureWithPrintWindow(HWND hwnd, HDC target_dc, bool use_full_content) {
    const UINT flags = use_full_content ? PW_RENDERFULLCONTENT : 0;
    if (PrintWindow(hwnd, target_dc, flags)) {
        return true;
    }

    if (use_full_content && PrintWindow(hwnd, target_dc, 0)) {
        return true;
    }

    return false;
}

bool CaptureWithBitBlt(
    HWND hwnd,
    HDC target_dc,
    int width,
    int height,
    bool client_only) {
    HDC source_dc = client_only ? GetDC(hwnd) : GetWindowDC(hwnd);
    if (source_dc == nullptr) {
        return false;
    }

    const BOOL copied =
        BitBlt(target_dc, 0, 0, width, height, source_dc, 0, 0, SRCCOPY);

    ReleaseDC(hwnd, source_dc);
    return copied != 0;
}

std::optional<CaptureResult> ReadBitmapPixels(HBITMAP bitmap, int width, int height) {
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

    DeviceContext screen_dc(GetDC(nullptr), false);
    if (!screen_dc) {
        return std::nullopt;
    }

    if (GetDIBits(
            screen_dc.Get(),
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

}  // namespace

std::optional<ScreenRect> GetWindowScreenRect(HWND hwnd, bool client_only) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return std::nullopt;
    }

    RECT rect{};
    if (client_only) {
        if (!GetClientRect(hwnd, &rect)) {
            return std::nullopt;
        }

        POINT origin{0, 0};
        if (!ClientToScreen(hwnd, &origin)) {
            return std::nullopt;
        }

        return ScreenRect{
            origin.x,
            origin.y,
            rect.right - rect.left,
            rect.bottom - rect.top,
        };
    }

    if (FAILED(DwmGetWindowAttribute(
            hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        if (!GetWindowRect(hwnd, &rect)) {
            return std::nullopt;
        }
    }

    return ScreenRect{
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
    };
}

std::optional<CaptureResult> CropImageRegion(
    CaptureResult source,
    const CaptureRegion& region,
    std::string& error) {
    if (region.width <= 0 || region.height <= 0) {
        error = "Capture region width and height must be positive.";
        return std::nullopt;
    }

    if (region.x < 0 || region.y < 0 ||
        region.x + region.width > static_cast<int32_t>(source.width) ||
        region.y + region.height > static_cast<int32_t>(source.height)) {
        error = "Capture region is outside image bounds.";
        return std::nullopt;
    }

    CaptureResult cropped{};
    cropped.width = static_cast<uint32_t>(region.width);
    cropped.height = static_cast<uint32_t>(region.height);
    cropped.pixels.resize(
        static_cast<size_t>(region.width) * static_cast<size_t>(region.height) * 4U);

    for (int32_t y = 0; y < region.height; ++y) {
        const size_t src_offset = (static_cast<size_t>(region.y + y) * source.width +
                                   static_cast<size_t>(region.x)) *
                                  4U;
        const size_t dst_offset =
            static_cast<size_t>(y) * static_cast<size_t>(region.width) * 4U;
        std::memcpy(
            cropped.pixels.data() + dst_offset,
            source.pixels.data() + src_offset,
            static_cast<size_t>(region.width) * 4U);
    }

    return cropped;
}

void EnsureCaptureDpiAwareness() {
    static std::once_flag once;
    std::call_once(once, []() {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    });
}

bool GetCaptureReferenceScreenRect(HWND hwnd, RECT& reference_rect) {
    if (!GetWindowRect(hwnd, &reference_rect)) {
        return false;
    }

    const int window_width = reference_rect.right - reference_rect.left;
    const int window_height = reference_rect.bottom - reference_rect.top;
    return window_width > 0 && window_height > 0;
}

int ScaleToImagePixels(int value, int image_size, int reference_size) {
    if (reference_size <= 0 || image_size == reference_size) {
        return value;
    }

    return static_cast<int>(
        (static_cast<int64_t>(value) * image_size) / reference_size);
}

bool GetClientBorderInsetsFromStyle(
    HWND hwnd,
    int client_width,
    int client_height,
    int& border_left,
    int& border_top) {
    const DWORD style = static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_STYLE));
    const DWORD exstyle = static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_EXSTYLE));

    RECT adjusted{0, 0, client_width, client_height};
    if (!AdjustWindowRectEx(&adjusted, style, FALSE, exstyle)) {
        return false;
    }

    border_left = -adjusted.left;
    border_top = -adjusted.top;
    return border_left >= 0 && border_top >= 0;
}

bool GetClientBorderInsetsFromScreen(
    HWND hwnd,
    const RECT& reference_rect,
    int& border_left,
    int& border_top) {
    POINT client_origin{0, 0};
    if (!ClientToScreen(hwnd, &client_origin)) {
        return false;
    }

    border_left = client_origin.x - reference_rect.left;
    border_top = client_origin.y - reference_rect.top;
    return true;
}

void ComputeClientCropOrigin(
    HWND hwnd,
    int client_width,
    int client_height,
    int image_width,
    int image_height,
    int& crop_x,
    int& crop_y) {
    RECT window_rect{};
    GetWindowRect(hwnd, &window_rect);

    RECT dwm_rect = window_rect;
    DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &dwm_rect, sizeof(dwm_rect));

    const int window_width = window_rect.right - window_rect.left;
    const int window_height = window_rect.bottom - window_rect.top;
    const int dwm_width = dwm_rect.right - dwm_rect.left;
    const int dwm_height = dwm_rect.bottom - dwm_rect.top;

    int scale_width = window_width;
    int scale_height = window_height;
    if (dwm_width == image_width && dwm_height == image_height) {
        scale_width = dwm_width;
        scale_height = dwm_height;
    } else if (window_width == image_width && window_height == image_height) {
        scale_width = window_width;
        scale_height = window_height;
    }

    int min_crop_x = image_width;
    int max_crop_y = 0;

    int border_left = 0;
    int border_top = 0;
    if (GetClientBorderInsetsFromStyle(hwnd, client_width, client_height, border_left, border_top)) {
        const int candidate_x = ScaleToImagePixels(border_left, image_width, scale_width);
        const int candidate_y = ScaleToImagePixels(border_top, image_height, scale_height);
        min_crop_x = (std::min)(min_crop_x, candidate_x);
        max_crop_y = (std::max)(max_crop_y, candidate_y);
    }

    if (GetClientBorderInsetsFromScreen(hwnd, window_rect, border_left, border_top)) {
        const int candidate_x = ScaleToImagePixels(border_left, image_width, scale_width);
        const int candidate_y = ScaleToImagePixels(border_top, image_height, scale_height);
        min_crop_x = (std::min)(min_crop_x, candidate_x);
        max_crop_y = (std::max)(max_crop_y, candidate_y);
    }

    if (GetClientBorderInsetsFromScreen(hwnd, dwm_rect, border_left, border_top)) {
        const int candidate_x = ScaleToImagePixels(border_left, image_width, scale_width);
        const int candidate_y = ScaleToImagePixels(border_top, image_height, scale_height);
        min_crop_x = (std::min)(min_crop_x, candidate_x);
        max_crop_y = (std::max)(max_crop_y, candidate_y);
    }

    if (min_crop_x == image_width) {
        min_crop_x = 0;
    }

    crop_x = (std::max)(0, min_crop_x);
    crop_y = (std::max)(0, max_crop_y);
}

void ComputePrintWindowClientCropOrigin(
    HWND hwnd,
    int client_width,
    int client_height,
    int image_width,
    int image_height,
    int& crop_x,
    int& crop_y) {
    RECT window_rect{};
    GetWindowRect(hwnd, &window_rect);

    const int window_width = window_rect.right - window_rect.left;
    const int window_height = window_rect.bottom - window_rect.top;

    int border_left = 0;
    int border_top = 0;
    if (GetClientBorderInsetsFromScreen(hwnd, window_rect, border_left, border_top)) {
        crop_x = ScaleToImagePixels(border_left, image_width, window_width);
        crop_y = ScaleToImagePixels(border_top, image_height, window_height);
        return;
    }

    if (GetClientBorderInsetsFromStyle(hwnd, client_width, client_height, border_left, border_top)) {
        crop_x = ScaleToImagePixels(border_left, image_width, window_width);
        crop_y = ScaleToImagePixels(border_top, image_height, window_height);
        return;
    }

    crop_x = 0;
    crop_y = 0;
}

std::optional<CaptureResult> CropClientAreaWithOrigin(
    HWND hwnd,
    CaptureResult full_window,
    int crop_x,
    int crop_y,
    std::string& error) {
    RECT client_rect{};
    if (!GetClientRect(hwnd, &client_rect)) {
        error = "Failed to query client bounds for crop.";
        return std::nullopt;
    }

    const int client_width = client_rect.right - client_rect.left;
    const int client_height = client_rect.bottom - client_rect.top;
    if (client_width <= 0 || client_height <= 0) {
        error = "Client bounds are empty.";
        return std::nullopt;
    }

    const int image_width = static_cast<int>(full_window.width);
    const int image_height = static_cast<int>(full_window.height);
    if (image_width <= 0 || image_height <= 0) {
        error = "Captured image bounds are empty.";
        return std::nullopt;
    }

    if (image_width == client_width && image_height == client_height) {
        return full_window;
    }

    if (crop_x < 0) {
        crop_x = 0;
    }
    if (crop_y < 0) {
        crop_y = 0;
    }

    CaptureRegion region{
        crop_x,
        crop_y,
        client_width,
        client_height,
    };

    if (region.x + region.width > image_width) {
        region.width = image_width - region.x;
    }
    if (region.y + region.height > image_height) {
        region.height = image_height - region.y;
    }

    if (region.width <= 0 || region.height <= 0) {
        error = "Client area crop bounds are outside captured image.";
        return std::nullopt;
    }

    return CropImageRegion(std::move(full_window), region, error);
}

std::optional<CaptureResult> CropClientAreaFromWindow(
    HWND hwnd,
    CaptureResult full_window,
    std::string& error) {
    EnsureCaptureDpiAwareness();

    RECT client_rect{};
    if (!GetClientRect(hwnd, &client_rect)) {
        error = "Failed to query client bounds for crop.";
        return std::nullopt;
    }

    const int client_width = client_rect.right - client_rect.left;
    const int client_height = client_rect.bottom - client_rect.top;
    if (client_width <= 0 || client_height <= 0) {
        error = "Client bounds are empty.";
        return std::nullopt;
    }

    const int image_width = static_cast<int>(full_window.width);
    const int image_height = static_cast<int>(full_window.height);

    int crop_x = 0;
    int crop_y = 0;
    ComputeClientCropOrigin(
        hwnd, client_width, client_height, image_width, image_height, crop_x, crop_y);

    return CropClientAreaWithOrigin(hwnd, std::move(full_window), crop_x, crop_y, error);
}

std::optional<CaptureResult> CropClientAreaFromPrintWindow(
    HWND hwnd,
    CaptureResult full_window,
    std::string& error) {
    EnsureCaptureDpiAwareness();

    RECT client_rect{};
    if (!GetClientRect(hwnd, &client_rect)) {
        error = "Failed to query client bounds for crop.";
        return std::nullopt;
    }

    const int client_width = client_rect.right - client_rect.left;
    const int client_height = client_rect.bottom - client_rect.top;
    if (client_width <= 0 || client_height <= 0) {
        error = "Client bounds are empty.";
        return std::nullopt;
    }

    const int image_width = static_cast<int>(full_window.width);
    const int image_height = static_cast<int>(full_window.height);

    int crop_x = 0;
    int crop_y = 0;
    ComputePrintWindowClientCropOrigin(
        hwnd, client_width, client_height, image_width, image_height, crop_x, crop_y);

    return CropClientAreaWithOrigin(hwnd, std::move(full_window), crop_x, crop_y, error);
}

void OpaqueAlphaChannel(std::vector<uint8_t>& pixels) {
    for (size_t i = 3; i < pixels.size(); i += 4U) {
        pixels[i] = 255;
    }
}

bool IsCaptureMostlyBlack(const CaptureResult& result, uint8_t threshold) {
    if (result.pixels.empty()) {
        return true;
    }

    const size_t pixel_count = result.pixels.size() / 4U;
    size_t non_black = 0;
    for (size_t i = 0; i < pixel_count; ++i) {
        const size_t offset = i * 4U;
        if (result.pixels[offset] > threshold ||
            result.pixels[offset + 1] > threshold ||
            result.pixels[offset + 2] > threshold) {
            ++non_black;
        }
    }

    return non_black < pixel_count / 20U;
}

std::optional<CaptureResult> CaptureWindow(
    HWND hwnd,
    GC_Flags flags,
    const CaptureRegion* region,
    std::string& error) {
    if (!IsWindowCapturable(hwnd, error)) {
        return std::nullopt;
    }

    const bool client_only = (flags & GC_FLAG_INCLUDE_FRAME) == 0;
    const bool force_gdi = (flags & GC_FLAG_FORCE_GDI) != 0;

    if (!force_gdi) {
        std::string wgc_error;
        if (auto wgc_result = CaptureWindowWgc(hwnd, client_only, wgc_error)) {
            if (region != nullptr) {
                return CropImageRegion(*wgc_result, *region, error);
            }
            return wgc_result;
        }
        error = wgc_error;
    } else {
        error.clear();
    }

    const bool use_full_content = (flags & GC_FLAG_RENDER_FULL_CONTENT) != 0;

    const auto rect = GetCaptureRect(hwnd, false);
    if (!rect.has_value() || rect->width <= 0 || rect->height <= 0) {
        error = "Failed to query window bounds.";
        return std::nullopt;
    }

    DeviceContext screen_dc(CreateCompatibleDC(nullptr), true);
    if (!screen_dc) {
        error = "Failed to create compatible device context.";
        return std::nullopt;
    }

    GdiHandle bitmap(CreateCompatibleBitmap(screen_dc.Get(), rect->width, rect->height));
    if (!bitmap) {
        error = "Failed to create compatible bitmap.";
        return std::nullopt;
    }

    const HGDIOBJ previous_object = SelectObject(screen_dc.Get(), bitmap.Get());
    if (previous_object == nullptr) {
        error = "Failed to select bitmap into device context.";
        return std::nullopt;
    }

    const auto restore_bitmap = [&]() {
        SelectObject(screen_dc.Get(), previous_object);
    };

    bool captured = CaptureWithPrintWindow(hwnd, screen_dc.Get(), use_full_content);

    if (!captured) {
        captured = CaptureWithBitBlt(
            hwnd, screen_dc.Get(), rect->width, rect->height, false);
    }

    restore_bitmap();

    if (!captured) {
        if (!force_gdi && !error.empty()) {
            error += " GDI fallback also failed.";
        } else {
            error = "PrintWindow and BitBlt capture both failed.";
        }
        return std::nullopt;
    }

    auto result = ReadBitmapPixels(static_cast<HBITMAP>(bitmap.Get()), rect->width, rect->height);
    if (!result.has_value()) {
        error = "Failed to read captured bitmap pixels.";
        return std::nullopt;
    }

    if (!force_gdi && !error.empty()) {
        error.clear();
    }

    if (client_only) {
        auto cropped = CropClientAreaFromPrintWindow(hwnd, *result, error);
        if (!cropped.has_value()) {
            return std::nullopt;
        }
        result = cropped;
    }

    if (region != nullptr) {
        return CropImageRegion(*result, *region, error);
    }

    return result;
}

}  // namespace gc
