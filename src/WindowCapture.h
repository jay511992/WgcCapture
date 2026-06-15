#pragma once

#include "GraphicsCapture.h"

#include <optional>
#include <string>
#include <vector>

namespace gc {

struct CaptureResult {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

struct CaptureRegion {
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
};

std::optional<CaptureResult> CropClientAreaFromWindow(
    HWND hwnd,
    CaptureResult full_window,
    std::string& error);

std::optional<CaptureResult> CropClientAreaFromPrintWindow(
    HWND hwnd,
    CaptureResult full_window,
    std::string& error);

std::optional<CaptureResult> CropImageRegion(
    CaptureResult source,
    const CaptureRegion& region,
    std::string& error);

struct ScreenRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

std::optional<ScreenRect> GetWindowScreenRect(HWND hwnd, bool client_only);
bool GetCaptureReferenceScreenRect(HWND hwnd, RECT& reference_rect);
void EnsureCaptureDpiAwareness();

std::optional<CaptureResult> CaptureWindowPrintWindow(
    HWND hwnd,
    GC_Flags flags,
    const CaptureRegion* region,
    std::string& error);

std::optional<CaptureResult> CaptureWindowDesktopDuplication(
    HWND hwnd,
    GC_Flags flags,
    const CaptureRegion* region,
    std::string& error);

std::optional<CaptureResult> CaptureWindowWgc(HWND hwnd, bool client_only, std::string& error);
std::optional<CaptureResult> CaptureWindow(
    HWND hwnd,
    GC_Flags flags,
    const CaptureRegion* region,
    std::string& error);

void OpaqueAlphaChannel(std::vector<uint8_t>& pixels);
bool IsCaptureMostlyBlack(const CaptureResult& result, uint8_t threshold = 8);

}  // namespace gc
