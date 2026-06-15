#include "WindowCapture.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstring>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace gc {
namespace {

using Microsoft::WRL::ComPtr;

struct DuplicationTarget {
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIOutput1> output;
    RECT desktop_rect{};
};

ComPtr<ID3D11Device> CreateD3D11DeviceOnAdapter(IDXGIAdapter* adapter) {
    ComPtr<ID3D11Device> device;
    const D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0};
    const HRESULT hr = D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        feature_levels,
        1,
        D3D11_SDK_VERSION,
        &device,
        nullptr,
        nullptr);
    if (FAILED(hr)) {
        return {};
    }
    return device;
}

std::optional<DuplicationTarget> FindDuplicationTarget(HWND hwnd, std::string& error) {
    const HMONITOR window_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (window_monitor == nullptr) {
        error = "Failed to resolve monitor for window.";
        return std::nullopt;
    }

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        error = "Failed to create DXGI factory.";
        return std::nullopt;
    }

    for (UINT adapter_index = 0;; ++adapter_index) {
        ComPtr<IDXGIAdapter> adapter;
        if (factory->EnumAdapters(adapter_index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        for (UINT output_index = 0;; ++output_index) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(output_index, &output) == DXGI_ERROR_NOT_FOUND) {
                break;
            }

            DXGI_OUTPUT_DESC output_desc{};
            if (FAILED(output->GetDesc(&output_desc))) {
                continue;
            }

            if (!output_desc.AttachedToDesktop) {
                continue;
            }

            const HMONITOR output_monitor =
                MonitorFromRect(&output_desc.DesktopCoordinates, MONITOR_DEFAULTTONULL);
            if (output_monitor != window_monitor) {
                continue;
            }

            ComPtr<IDXGIOutput1> output1;
            if (FAILED(output.As(&output1))) {
                continue;
            }

            return DuplicationTarget{
                adapter,
                output1,
                output_desc.DesktopCoordinates,
            };
        }
    }

    error = "No matching DXGI output found for window monitor.";
    return std::nullopt;
}

bool AcquireDesktopFrame(
    IDXGIOutputDuplication* duplication,
    ComPtr<IDXGIResource>& resource,
    std::string& error) {
    resource.Reset();

    for (int attempt = 0; attempt < 10; ++attempt) {
        DXGI_OUTDUPL_FRAME_INFO frame_info{};
        ComPtr<IDXGIResource> frame_resource;
        const HRESULT acquire_hr = duplication->AcquireNextFrame(500, &frame_info, &frame_resource);

        if (acquire_hr == DXGI_ERROR_ACCESS_LOST) {
            error = "Desktop duplication access lost.";
            return false;
        }

        if (acquire_hr == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }

        if (FAILED(acquire_hr)) {
            error = "AcquireNextFrame failed.";
            return false;
        }

        if (frame_info.AccumulatedFrames == 0 && attempt < 9) {
            duplication->ReleaseFrame();
            Sleep(16);
            continue;
        }

        resource = std::move(frame_resource);
        return true;
    }

    error = "AcquireNextFrame timed out waiting for desktop frame.";
    return false;
}

std::optional<CaptureResult> CopyMappedDesktopRegion(
    const D3D11_MAPPED_SUBRESOURCE& mapped,
    uint32_t desktop_width,
    uint32_t desktop_height,
    int crop_x,
    int crop_y,
    int crop_width,
    int crop_height) {
    if (crop_x < 0 || crop_y < 0 || crop_width <= 0 || crop_height <= 0 ||
        crop_x + crop_width > static_cast<int>(desktop_width) ||
        crop_y + crop_height > static_cast<int>(desktop_height)) {
        return std::nullopt;
    }

    CaptureResult result{};
    result.width = static_cast<uint32_t>(crop_width);
    result.height = static_cast<uint32_t>(crop_height);
    result.pixels.resize(static_cast<size_t>(crop_width) * static_cast<size_t>(crop_height) * 4U);

    for (int y = 0; y < crop_height; ++y) {
        const auto* src_row = static_cast<const uint8_t*>(mapped.pData) +
                              static_cast<size_t>(crop_y + y) * mapped.RowPitch +
                              static_cast<size_t>(crop_x) * 4U;
        auto* dst_row = result.pixels.data() + static_cast<size_t>(y) * crop_width * 4U;
        std::memcpy(dst_row, src_row, static_cast<size_t>(crop_width) * 4U);
        for (int x = 0; x < crop_width; ++x) {
            dst_row[x * 4 + 3] = 255;
        }
    }

    return result;
}

}  // namespace

std::optional<CaptureResult> CaptureWindowDesktopDuplication(
    HWND hwnd,
    GC_Flags flags,
    const CaptureRegion* region,
    std::string& error) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        error = "Invalid window handle.";
        return std::nullopt;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const bool client_only = (flags & GC_FLAG_INCLUDE_FRAME) == 0;

    const auto target = FindDuplicationTarget(hwnd, error);
    if (!target.has_value()) {
        return std::nullopt;
    }

    const auto window_screen = GetWindowScreenRect(hwnd, client_only);
    if (!window_screen.has_value() || window_screen->width <= 0 || window_screen->height <= 0) {
        error = "Failed to query window screen bounds for desktop duplication.";
        return std::nullopt;
    }

    const auto device = CreateD3D11DeviceOnAdapter(target->adapter.Get());
    if (!device) {
        error = "Failed to create D3D11 device on DXGI adapter.";
        return std::nullopt;
    }

    ComPtr<IDXGIOutputDuplication> duplication;
    const HRESULT dup_hr = target->output->DuplicateOutput(device.Get(), &duplication);
    if (FAILED(dup_hr)) {
        error = "DuplicateOutput failed. Desktop duplication may be unavailable.";
        return std::nullopt;
    }

    ComPtr<IDXGIResource> resource;
    if (!AcquireDesktopFrame(duplication.Get(), resource, error)) {
        return std::nullopt;
    }

    ComPtr<ID3D11Texture2D> desktop_texture;
    const HRESULT query_hr = resource.As(&desktop_texture);
    if (FAILED(query_hr)) {
        duplication->ReleaseFrame();
        error = "Failed to query desktop duplication texture.";
        return std::nullopt;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desktop_texture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    const HRESULT staging_hr = device->CreateTexture2D(&staging_desc, nullptr, &staging);
    if (FAILED(staging_hr)) {
        duplication->ReleaseFrame();
        error = "Failed to create staging texture for desktop duplication.";
        return std::nullopt;
    }

    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    context->CopyResource(staging.Get(), desktop_texture.Get());
    context->Flush();

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT map_hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    duplication->ReleaseFrame();

    if (FAILED(map_hr)) {
        error = "Failed to map desktop duplication texture.";
        return std::nullopt;
    }

    const int crop_x = window_screen->x - target->desktop_rect.left;
    const int crop_y = window_screen->y - target->desktop_rect.top;

    auto cropped = CopyMappedDesktopRegion(
        mapped,
        desc.Width,
        desc.Height,
        crop_x,
        crop_y,
        window_screen->width,
        window_screen->height);

    context->Unmap(staging.Get(), 0);

    if (!cropped.has_value()) {
        error = "Failed to crop desktop duplication image to window bounds.";
        return std::nullopt;
    }

    if (IsCaptureMostlyBlack(*cropped)) {
        error = "Desktop duplication captured a black image.";
        return std::nullopt;
    }

    if (region != nullptr) {
        return CropImageRegion(std::move(*cropped), *region, error);
    }

    return cropped;
}

}  // namespace gc
