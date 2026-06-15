#include "GraphicsCapture.h"

#include <cstdio>
#include <cstdlib>

static void PrintUsage() {
    std::printf("Usage: GCExample.exe <window_handle_hex> [flags]\n");
    std::printf("  flags: 0=default, 1=client only, 2=render full content, 3=both\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const HWND hwnd = reinterpret_cast<HWND>(std::strtoull(argv[1], nullptr, 16));
    GC_Flags flags = GC_FLAG_RENDER_FULL_CONTENT;
    if (argc >= 3) {
        flags = static_cast<GC_Flags>(std::strtoul(argv[2], nullptr, 0));
    }

    GC_ImageBgr24 image{};
    const GC_Result result = GC_CaptureWindowBgr24(hwnd, flags, &image);
    if (result != GC_OK) {
        std::printf("Capture failed (%d): %s\n", result, GC_GetLastErrorMessage());
        return 1;
    }

    std::printf(
        "Captured %ux%u BGR24, stride=%u, pixels=%p, bytes=%zu\n",
        image.width,
        image.height,
        image.stride,
        static_cast<void*>(image.pixels),
        static_cast<size_t>(image.stride) * image.height);

    GC_FreeImageBgr24(&image);
    return 0;
}
