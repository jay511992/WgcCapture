#include "GraphicsCapture.h"

#include "CaptureSession.h"
#include "WindowCapture.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

thread_local std::string g_last_error;

void SetLastError(std::string message) {
    g_last_error = std::move(message);
}

const gc::CaptureRegion* ToInternalRegion(const GC_Rect* region, gc::CaptureRegion& storage) {
    if (region == nullptr) {
        return nullptr;
    }

    storage.x = region->x;
    storage.y = region->y;
    storage.width = region->width;
    storage.height = region->height;
    return &storage;
}

GC_Result MapCaptureError(const std::string& error) {
    SetLastError(error.empty() ? "Capture failed." : error);
    if (error == "Invalid window handle.") {
        return GC_ERROR_INVALID_WINDOW;
    }
    if (error.find("memory") != std::string::npos) {
        return GC_ERROR_OUT_OF_MEMORY;
    }
    if (error.find("session") != std::string::npos) {
        return GC_ERROR_INVALID_SESSION;
    }
    if (error.find("region") != std::string::npos || error.find("bounds") != std::string::npos) {
        return GC_ERROR_INVALID_ARGUMENT;
    }
    return GC_ERROR_CAPTURE_FAILED;
}

GC_Result FillOutputImage(const gc::CaptureResult& captured, GC_Image* out_image) {
    const size_t byte_count = captured.pixels.size();
    auto* buffer = static_cast<uint8_t*>(std::malloc(byte_count));
    if (buffer == nullptr) {
        SetLastError("Failed to allocate image buffer.");
        return GC_ERROR_OUT_OF_MEMORY;
    }

    std::memcpy(buffer, captured.pixels.data(), byte_count);
    for (size_t i = 3; i < byte_count; i += 4U) {
        buffer[i] = 255;
    }

    out_image->width = captured.width;
    out_image->height = captured.height;
    out_image->stride = captured.width * 4U;
    out_image->pixels = buffer;

    SetLastError({});
    return GC_OK;
}

GC_Result FillOutputImageBgr24(const gc::CaptureResult& captured, GC_ImageBgr24* out_image) {
    if (captured.width == 0 || captured.height == 0 || captured.pixels.empty()) {
        SetLastError("Captured image is empty.");
        return GC_ERROR_CAPTURE_FAILED;
    }

    const size_t row_bytes = static_cast<size_t>(captured.width) * 3U;
    const size_t byte_count = row_bytes * static_cast<size_t>(captured.height);
    auto* buffer = static_cast<uint8_t*>(std::malloc(byte_count));
    if (buffer == nullptr) {
        SetLastError("Failed to allocate BGR24 image buffer.");
        return GC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t y = 0; y < captured.height; ++y) {
        const uint8_t* src_row =
            captured.pixels.data() + static_cast<size_t>(y) * captured.width * 4U;
        uint8_t* dst_row = buffer + static_cast<size_t>(y) * row_bytes;
        for (uint32_t x = 0; x < captured.width; ++x) {
            const size_t src_offset = static_cast<size_t>(x) * 4U;
            const size_t dst_offset = static_cast<size_t>(x) * 3U;
            dst_row[dst_offset] = src_row[src_offset];
            dst_row[dst_offset + 1] = src_row[src_offset + 1];
            dst_row[dst_offset + 2] = src_row[src_offset + 2];
        }
    }

    out_image->width = captured.width;
    out_image->height = captured.height;
    out_image->stride = static_cast<uint32_t>(row_bytes);
    out_image->pixels = buffer;

    SetLastError({});
    return GC_OK;
}

void ResetOutputImageBgr24(GC_ImageBgr24* out_image) {
    out_image->width = 0;
    out_image->height = 0;
    out_image->stride = 0;
    out_image->pixels = nullptr;
}

GC_Result ValidateCaptureRequestBgr24(const GC_Rect* region, GC_ImageBgr24* out_image) {
    if (out_image == nullptr) {
        SetLastError("Output image pointer is null.");
        return GC_ERROR_INVALID_ARGUMENT;
    }

    if (region != nullptr && (region->width <= 0 || region->height <= 0)) {
        SetLastError("Capture region width and height must be positive.");
        return GC_ERROR_INVALID_ARGUMENT;
    }

    ResetOutputImageBgr24(out_image);
    return GC_OK;
}

void ResetOutputImage(GC_Image* out_image) {
    out_image->width = 0;
    out_image->height = 0;
    out_image->stride = 0;
    out_image->pixels = nullptr;
}

GC_Result ValidateCaptureRequest(const GC_Rect* region, GC_Image* out_image) {
    if (out_image == nullptr) {
        SetLastError("Output image pointer is null.");
        return GC_ERROR_INVALID_ARGUMENT;
    }

    if (region != nullptr && (region->width <= 0 || region->height <= 0)) {
        SetLastError("Capture region width and height must be positive.");
        return GC_ERROR_INVALID_ARGUMENT;
    }

    ResetOutputImage(out_image);
    return GC_OK;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)module;
    (void)reserved;

    switch (reason) {
        case DLL_PROCESS_ATTACH:
            gc::EnsureCaptureDpiAwareness();
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }

    return TRUE;
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureWindow(HWND hwnd, GC_Flags flags, GC_Image* out_image) {
    return GC_CaptureWindowEx(hwnd, flags, nullptr, out_image);
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureWindowEx(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_Image* out_image) {
    const GC_Result validation = ValidateCaptureRequest(region, out_image);
    if (validation != GC_OK) {
        return validation;
    }

    std::string error;
    gc::CaptureRegion capture_region{};
    const gc::CaptureRegion* internal_region = ToInternalRegion(region, capture_region);

    auto captured = gc::CaptureWindow(hwnd, flags, internal_region, error);
    if (!captured.has_value()) {
        return MapCaptureError(error);
    }

    return FillOutputImage(*captured, out_image);
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureWindowBgr24(
    HWND hwnd,
    GC_Flags flags,
    GC_ImageBgr24* out_image) {
    return GC_CaptureWindowExBgr24(hwnd, flags, nullptr, out_image);
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureWindowExBgr24(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_ImageBgr24* out_image) {
    const GC_Result validation = ValidateCaptureRequestBgr24(region, out_image);
    if (validation != GC_OK) {
        return validation;
    }

    std::string error;
    gc::CaptureRegion capture_region{};
    const gc::CaptureRegion* internal_region = ToInternalRegion(region, capture_region);

    auto captured = gc::CaptureWindow(hwnd, flags, internal_region, error);
    if (!captured.has_value()) {
        return MapCaptureError(error);
    }

    return FillOutputImageBgr24(*captured, out_image);
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureWindowPrintWindow(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_Image* out_image) {
    const GC_Result validation = ValidateCaptureRequest(region, out_image);
    if (validation != GC_OK) {
        return validation;
    }

    std::string error;
    gc::CaptureRegion capture_region{};
    const gc::CaptureRegion* internal_region = ToInternalRegion(region, capture_region);

    auto captured = gc::CaptureWindowPrintWindow(hwnd, flags, internal_region, error);
    if (!captured.has_value()) {
        return MapCaptureError(error);
    }

    return FillOutputImage(*captured, out_image);
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureWindowPrintWindowBgr24(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_ImageBgr24* out_image) {
    const GC_Result validation = ValidateCaptureRequestBgr24(region, out_image);
    if (validation != GC_OK) {
        return validation;
    }

    std::string error;
    gc::CaptureRegion capture_region{};
    const gc::CaptureRegion* internal_region = ToInternalRegion(region, capture_region);

    auto captured = gc::CaptureWindowPrintWindow(hwnd, flags, internal_region, error);
    if (!captured.has_value()) {
        return MapCaptureError(error);
    }

    return FillOutputImageBgr24(*captured, out_image);
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureWindowDesktopDuplication(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_Image* out_image) {
    const GC_Result validation = ValidateCaptureRequest(region, out_image);
    if (validation != GC_OK) {
        return validation;
    }

    std::string error;
    gc::CaptureRegion capture_region{};
    const gc::CaptureRegion* internal_region = ToInternalRegion(region, capture_region);

    auto captured = gc::CaptureWindowDesktopDuplication(hwnd, flags, internal_region, error);
    if (!captured.has_value()) {
        return MapCaptureError(error);
    }

    return FillOutputImage(*captured, out_image);
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureWindowDesktopDuplicationBgr24(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_ImageBgr24* out_image) {
    const GC_Result validation = ValidateCaptureRequestBgr24(region, out_image);
    if (validation != GC_OK) {
        return validation;
    }

    std::string error;
    gc::CaptureRegion capture_region{};
    const gc::CaptureRegion* internal_region = ToInternalRegion(region, capture_region);

    auto captured = gc::CaptureWindowDesktopDuplication(hwnd, flags, internal_region, error);
    if (!captured.has_value()) {
        return MapCaptureError(error);
    }

    return FillOutputImageBgr24(*captured, out_image);
}

extern "C" GC_API GC_Result GC_CALL GC_OpenCaptureSession(
    HWND hwnd,
    GC_Flags flags,
    GC_CaptureSession** out_session) {
    if (out_session == nullptr) {
        SetLastError("Output session pointer is null.");
        return GC_ERROR_INVALID_ARGUMENT;
    }

    *out_session = nullptr;

    std::string error;
    auto* session = gc::CaptureSession::Open(hwnd, flags, error);
    if (session == nullptr) {
        return MapCaptureError(error);
    }

    *out_session = reinterpret_cast<GC_CaptureSession*>(session);
    SetLastError({});
    return GC_OK;
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureSessionFrame(
    GC_CaptureSession* session,
    const GC_Rect* region,
    GC_Image* out_image) {
    if (session == nullptr) {
        SetLastError("Capture session is null.");
        return GC_ERROR_INVALID_SESSION;
    }

    const GC_Result validation = ValidateCaptureRequest(region, out_image);
    if (validation != GC_OK) {
        return validation;
    }

    std::string error;
    gc::CaptureRegion capture_region{};
    const gc::CaptureRegion* internal_region = ToInternalRegion(region, capture_region);

    auto* impl = reinterpret_cast<gc::CaptureSession*>(session);
    auto captured = impl->CaptureFrame(internal_region, error);
    if (!captured.has_value()) {
        return MapCaptureError(error);
    }

    return FillOutputImage(*captured, out_image);
}

extern "C" GC_API GC_Result GC_CALL GC_CaptureSessionFrameBgr24(
    GC_CaptureSession* session,
    const GC_Rect* region,
    GC_ImageBgr24* out_image) {
    if (session == nullptr) {
        SetLastError("Capture session is null.");
        return GC_ERROR_INVALID_SESSION;
    }

    const GC_Result validation = ValidateCaptureRequestBgr24(region, out_image);
    if (validation != GC_OK) {
        return validation;
    }

    std::string error;
    gc::CaptureRegion capture_region{};
    const gc::CaptureRegion* internal_region = ToInternalRegion(region, capture_region);

    auto* impl = reinterpret_cast<gc::CaptureSession*>(session);
    auto captured = impl->CaptureFrame(internal_region, error);
    if (!captured.has_value()) {
        return MapCaptureError(error);
    }

    return FillOutputImageBgr24(*captured, out_image);
}

extern "C" GC_API void GC_CALL GC_CloseCaptureSession(GC_CaptureSession* session) {
    if (session == nullptr) {
        return;
    }

    auto* impl = reinterpret_cast<gc::CaptureSession*>(session);
    impl->Close();
    delete impl;
}

extern "C" GC_API void GC_CALL GC_FreeImage(GC_Image* image) {
    if (image == nullptr) {
        return;
    }

    std::free(image->pixels);
    image->pixels = nullptr;
    image->width = 0;
    image->height = 0;
    image->stride = 0;
}

extern "C" GC_API void GC_CALL GC_FreeImageBgr24(GC_ImageBgr24* image) {
    if (image == nullptr) {
        return;
    }

    std::free(image->pixels);
    image->pixels = nullptr;
    image->width = 0;
    image->height = 0;
    image->stride = 0;
}

extern "C" GC_API const char* GC_CALL GC_GetLastErrorMessage(void) {
    return g_last_error.c_str();
}
