#pragma once

#include <stdint.h>
#include <windows.h>

#define GC_CALL __stdcall

#ifdef GRAPHICSCAPTURE_EXPORTS
#define GC_API __declspec(dllexport)
#else
#define GC_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GC_Flags : uint32_t {
    GC_FLAG_NONE = 0,
    GC_FLAG_CLIENT_ONLY = 1 << 0,          /* same as default: client area only */
    GC_FLAG_RENDER_FULL_CONTENT = 1 << 1,
    GC_FLAG_FORCE_GDI = 1 << 2,
    GC_FLAG_INCLUDE_FRAME = 1 << 3,        /* include title bar and non-client border */
} GC_Flags;

typedef enum GC_Result : int32_t {
    GC_OK = 0,
    GC_ERROR_INVALID_ARGUMENT = -1,
    GC_ERROR_INVALID_WINDOW = -2,
    GC_ERROR_CAPTURE_FAILED = -3,
    GC_ERROR_OUT_OF_MEMORY = -4,
    GC_ERROR_INTERNAL = -5,
    GC_ERROR_INVALID_SESSION = -6,
} GC_Result;

typedef struct GC_Image {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t* pixels;
} GC_Image;

typedef struct GC_ImageBgr24 {
    uint32_t width;
    uint32_t height;
    uint32_t stride;   /* bytes per row, normally width * 3 */
    uint8_t* pixels;   /* B,G,R order, 3 bytes per pixel, no alpha */
} GC_ImageBgr24;

/* Rectangle relative to captured image origin (top-left).
 * Default capture excludes title bar (client area only).
 * With GC_FLAG_INCLUDE_FRAME: origin is full window top-left.
 * Pass region=NULL to capture the entire current capture area. */
typedef struct GC_Rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} GC_Rect;

typedef struct GC_CaptureSession GC_CaptureSession;

GC_API GC_Result GC_CALL GC_CaptureWindow(HWND hwnd, GC_Flags flags, GC_Image* out_image);
GC_API GC_Result GC_CALL GC_CaptureWindowEx(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_Image* out_image);

/* Same as GC_CaptureWindowEx but returns packed BGR24 (25% less memory than BGRA). */
GC_API GC_Result GC_CALL GC_CaptureWindowBgr24(
    HWND hwnd,
    GC_Flags flags,
    GC_ImageBgr24* out_image);
GC_API GC_Result GC_CALL GC_CaptureWindowExBgr24(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_ImageBgr24* out_image);

/* PrintWindow (GDI). Set GC_FLAG_RENDER_FULL_CONTENT for PW_RENDERFULLCONTENT. */
GC_API GC_Result GC_CALL GC_CaptureWindowPrintWindow(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_Image* out_image);
GC_API GC_Result GC_CALL GC_CaptureWindowPrintWindowBgr24(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_ImageBgr24* out_image);

/* DXGI Desktop Duplication, cropped to window screen bounds. */
GC_API GC_Result GC_CALL GC_CaptureWindowDesktopDuplication(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_Image* out_image);
GC_API GC_Result GC_CALL GC_CaptureWindowDesktopDuplicationBgr24(
    HWND hwnd,
    GC_Flags flags,
    const GC_Rect* region,
    GC_ImageBgr24* out_image);

GC_API void GC_CALL GC_FreeImage(GC_Image* image);
GC_API void GC_CALL GC_FreeImageBgr24(GC_ImageBgr24* image);
GC_API const char* GC_CALL GC_GetLastErrorMessage(void);

/* Reusable WGC session for high-frequency capture. */
GC_API GC_Result GC_CALL GC_OpenCaptureSession(
    HWND hwnd,
    GC_Flags flags,
    GC_CaptureSession** out_session);
GC_API GC_Result GC_CALL GC_CaptureSessionFrame(
    GC_CaptureSession* session,
    const GC_Rect* region,
    GC_Image* out_image);
GC_API GC_Result GC_CALL GC_CaptureSessionFrameBgr24(
    GC_CaptureSession* session,
    const GC_Rect* region,
    GC_ImageBgr24* out_image);
GC_API void GC_CALL GC_CloseCaptureSession(GC_CaptureSession* session);

#ifdef __cplusplus
}
#endif
