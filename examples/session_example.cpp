#include "GraphicsCapture.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("Usage: GCSessionExample.exe <hwnd_hex> [frame_count]\n");
        return 1;
    }

    const HWND hwnd = reinterpret_cast<HWND>(std::strtoull(argv[1], nullptr, 16));
    const int frame_count = argc >= 3 ? std::atoi(argv[2]) : 30;

    GC_CaptureSession* session = nullptr;
    GC_Result open_result = GC_OpenCaptureSession(
        hwnd,
        GC_FLAG_RENDER_FULL_CONTENT,
        &session);
    if (open_result != GC_OK) {
        std::printf("Open session failed (%d): %s\n", open_result, GC_GetLastErrorMessage());
        return 1;
    }

    std::printf("Session opened. Capturing %d BGR24 frames...\n", frame_count);

    for (int i = 0; i < frame_count; ++i) {
        GC_ImageBgr24 image{};
        const GC_Result capture_result = GC_CaptureSessionFrameBgr24(session, nullptr, &image);
        if (capture_result != GC_OK) {
            std::printf(
                "Frame %d failed (%d): %s\n",
                i,
                capture_result,
                GC_GetLastErrorMessage());
            break;
        }

        std::printf(
            "Frame %d: %ux%u, stride=%u, bytes=%zu\n",
            i,
            image.width,
            image.height,
            image.stride,
            static_cast<size_t>(image.stride) * image.height);
        GC_FreeImageBgr24(&image);
    }

    GC_CloseCaptureSession(session);
    return 0;
}
