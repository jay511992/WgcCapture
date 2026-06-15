#pragma once

#include "GraphicsCapture.h"

#include <tlhelp32.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace capture_test {

inline constexpr wchar_t kGw2ProcessName[] = L"Gw2-64.exe";

struct Gw2WindowInfo {
    DWORD process_id = 0;
    HWND hwnd = nullptr;
};

struct MainWindowSearch {
    DWORD process_id = 0;
    HWND window = nullptr;
    LONG best_area = 0;
};

inline BOOL CALLBACK EnumMainWindowProc(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<MainWindowSearch*>(param);

    DWORD window_process_id = 0;
    GetWindowThreadProcessId(hwnd, &window_process_id);
    if (window_process_id != search->process_id) {
        return TRUE;
    }

    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return TRUE;
    }

    const LONG width = rect.right - rect.left;
    const LONG height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return TRUE;
    }

    const LONG area = width * height;
    if (area > search->best_area) {
        search->best_area = area;
        search->window = hwnd;
    }

    return TRUE;
}

inline DWORD FindProcessIdByName(const wchar_t* process_name) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD process_id = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, process_name) == 0) {
                process_id = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return process_id;
}

inline HWND FindMainWindow(DWORD process_id) {
    MainWindowSearch search{};
    search.process_id = process_id;
    EnumWindows(EnumMainWindowProc, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

inline bool FindGw2Window(Gw2WindowInfo& info) {
    info.process_id = FindProcessIdByName(kGw2ProcessName);
    if (info.process_id == 0) {
        return false;
    }

    info.hwnd = FindMainWindow(info.process_id);
    return info.hwnd != nullptr;
}

inline void PrintWindowTitle(HWND hwnd) {
    wchar_t title[512]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    std::wprintf(L"  Window title: %s\n", title);
}

inline std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L".";
    }

    std::wstring directory(path, length);
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }
    return directory.substr(0, slash);
}

inline std::wstring ResolveGw2OutputPath(int argc, wchar_t* argv[], const wchar_t* default_name) {
    if (argc >= 2 && argv[1] != nullptr && argv[1][0] != L'\0') {
        const std::wstring arg(argv[1]);
        if (arg.find(L':') != std::wstring::npos || arg.rfind(L"\\", 0) == 0) {
            return arg;
        }
        return GetExeDirectory() + L"\\" + arg;
    }
    return GetExeDirectory() + L"\\" + default_name;
}

inline bool ParseGw2Region(int argc, wchar_t* argv[], GC_Rect& region, const GC_Rect** region_ptr) {
    *region_ptr = nullptr;
    if (argc < 6) {
        return true;
    }

    region.x = std::wcstol(argv[2], nullptr, 10);
    region.y = std::wcstol(argv[3], nullptr, 10);
    region.width = std::wcstol(argv[4], nullptr, 10);
    region.height = std::wcstol(argv[5], nullptr, 10);
    *region_ptr = &region;
    return true;
}

inline bool ValidateGw2Args(int argc) {
    if (argc >= 3 && argc < 6) {
        return false;
    }
    return true;
}

inline HWND ParseWindowHandle(const wchar_t* text) {
    if (text == nullptr || text[0] == L'\0') {
        return nullptr;
    }

    const bool force_hex = text[0] == L'0' && (text[1] == L'x' || text[1] == L'X');
    bool has_hex_letters = false;
    for (const wchar_t* cursor = text; *cursor != L'\0'; ++cursor) {
        const wchar_t ch = *cursor;
        if ((ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F')) {
            has_hex_letters = true;
            break;
        }
    }

    const int base = (force_hex || has_hex_letters) ? 16 : 10;
    wchar_t* end = nullptr;
    const unsigned long long value = std::wcstoull(text, &end, base);
    if (end == text || *end != L'\0' || value == 0) {
        return nullptr;
    }

    return reinterpret_cast<HWND>(static_cast<uintptr_t>(value));
}

inline bool InitializeCom() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

inline HRESULT SavePng(const wchar_t* path, const GC_Image& image) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) {
        return hr;
    }

    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) {
        return hr;
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    hr = encoder->CreateNewFrame(&frame, &properties);
    if (FAILED(hr)) {
        return hr;
    }

    if (properties) {
        PROPBAG2 option{};
        option.pstrName = const_cast<wchar_t*>(L"FilterOption");
        option.dwType = VT_UI1;
        VARIANT value{};
        value.vt = VT_UI1;
        value.bVal = WICPngFilterAdaptive;
        properties->Write(1, &option, &value);
        VariantClear(&value);
    }

    hr = frame->Initialize(properties.Get());
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->SetSize(image.width, image.height);
    if (FAILED(hr)) {
        return hr;
    }

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixel_format);
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->WritePixels(
        image.height,
        image.stride,
        image.stride * image.height,
        image.pixels);
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        return hr;
    }

    return encoder->Commit();
}

inline HRESULT SavePngBgr24(const wchar_t* path, const GC_ImageBgr24& image) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) {
        return hr;
    }

    hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) {
        return hr;
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    hr = encoder->CreateNewFrame(&frame, &properties);
    if (FAILED(hr)) {
        return hr;
    }

    if (properties) {
        PROPBAG2 option{};
        option.pstrName = const_cast<wchar_t*>(L"FilterOption");
        option.dwType = VT_UI1;
        VARIANT value{};
        value.vt = VT_UI1;
        value.bVal = WICPngFilterAdaptive;
        properties->Write(1, &option, &value);
        VariantClear(&value);
    }

    hr = frame->Initialize(properties.Get());
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->SetSize(image.width, image.height);
    if (FAILED(hr)) {
        return hr;
    }

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat24bppBGR;
    hr = frame->SetPixelFormat(&pixel_format);
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->WritePixels(
        image.height,
        image.stride,
        image.stride * image.height,
        image.pixels);
    if (FAILED(hr)) {
        return hr;
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        return hr;
    }

    return encoder->Commit();
}

inline bool InitializeDpiAwareness() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    return true;
}

inline void PauseBeforeExit(int exit_code) {
    std::fflush(stdout);
    std::fflush(stderr);
    if (exit_code != 0) {
        std::printf("\nFailed with exit code %d.\n", exit_code);
    } else {
        std::printf("\nDone.\n");
    }
    std::system("pause");
}

inline void PrintSamplePixelBgr24(const GC_ImageBgr24& image) {
    if (image.pixels == nullptr || image.width == 0 || image.height == 0) {
        return;
    }

    const uint8_t* pixel = image.pixels;
    std::printf(
        "  Sample pixel BGR: %u %u %u\n",
        pixel[0],
        pixel[1],
        pixel[2]);
    std::printf(
        "  Buffer size: %zu bytes (stride=%u)\n",
        static_cast<size_t>(image.stride) * image.height,
        image.stride);
}

inline void PrintSamplePixel(const GC_Image& image) {
    if (image.pixels == nullptr || image.width == 0 || image.height == 0) {
        return;
    }

    const uint8_t* pixel = image.pixels;
    std::printf(
        "  Sample pixel BGRA: %u %u %u %u\n",
        pixel[0],
        pixel[1],
        pixel[2],
        pixel[3]);
}

inline void PrintUsage(const wchar_t* exe_name, const wchar_t* method) {
    std::wprintf(L"Usage: %s [output.png] [x y width height]\n", exe_name);
    std::wprintf(L"  Target process: Gw2-64.exe (must be running)\n");
    std::wprintf(L"  Example: %s gw2_printwindow.png 100 100 800 600\n", exe_name);
    std::wprintf(L"  method: %s\n", method);
}

}  // namespace capture_test
