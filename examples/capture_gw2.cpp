#include "GraphicsCapture.h"
#include "capture_test_common.h"

#include <cstdio>
#include <cstdlib>

int wmain(int argc, wchar_t* argv[]) {
    const int exit_code = [&]() -> int {
        capture_test::InitializeDpiAwareness();

        if (argc >= 3 && argc < 6) {
            std::printf("Usage: CaptureGw2.exe [output.png] [x y width height]\n");
            std::printf("Example: CaptureGw2.exe gw2.png 100 100 800 600\n");
            return 1;
        }

        if (!capture_test::InitializeCom()) {
            std::printf("CoInitializeEx failed.\n");
            return 1;
        }

        capture_test::Gw2WindowInfo gw2{};
        if (!capture_test::FindGw2Window(gw2)) {
            if (gw2.process_id == 0) {
                std::printf("Process Gw2-64.exe not found. Start the game first.\n");
            } else {
                std::printf(
                    "No visible main window found for Gw2-64.exe (pid=%lu).\n",
                    gw2.process_id);
            }
            return 1;
        }

        const std::wstring output_path =
            capture_test::ResolveGw2OutputPath(argc, argv, L"Gw2-64.png");

        std::printf("Found Gw2-64.exe\n");
        std::printf("  PID:   %lu\n", gw2.process_id);
        std::printf("  HWND:  0x%p\n", gw2.hwnd);
        capture_test::PrintWindowTitle(gw2.hwnd);
        std::printf("Capture method: Windows Graphics Capture (WGC), BGR24\n");
        std::wprintf(L"  Output: %s\n", output_path.c_str());

        const GC_Rect* region_ptr = nullptr;
        GC_Rect region{};
        capture_test::ParseGw2Region(argc, argv, region, &region_ptr);
        if (region_ptr != nullptr) {
            std::printf(
                "Capture region: x=%d y=%d width=%d height=%d\n",
                region.x,
                region.y,
                region.width,
                region.height);
        }

        GC_ImageBgr24 image{};
        const GC_Result capture_result = GC_CaptureWindowExBgr24(
            gw2.hwnd,
            static_cast<GC_Flags>(GC_FLAG_RENDER_FULL_CONTENT),
            region_ptr,
            &image);

        if (capture_result != GC_OK) {
            std::printf(
                "Capture failed (%d): %s\n",
                capture_result,
                GC_GetLastErrorMessage());
            return 1;
        }

        std::printf("Captured %ux%u pixels (BGR24)\n", image.width, image.height);
        capture_test::PrintSamplePixelBgr24(image);

        const HRESULT save_hr = capture_test::SavePngBgr24(output_path.c_str(), image);
        GC_FreeImageBgr24(&image);

        if (FAILED(save_hr)) {
            std::printf("Save image failed: 0x%08lX\n", save_hr);
            return 1;
        }

        std::wprintf(L"Saved: %s\n", output_path.c_str());
        return 0;
    }();

    capture_test::PauseBeforeExit(exit_code);
    return exit_code;
}
