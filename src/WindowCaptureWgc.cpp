#include "WindowCapture.h"

#include <Windows.Graphics.DirectX.Direct3D11.interop.h>
#include <d3d11.h>
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <wrl/client.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowsapp.lib")

namespace gc {
namespace {

using Microsoft::WRL::ComPtr;

std::once_flag g_winrt_once;

void EnsureWinRtInitialized() {
    std::call_once(g_winrt_once, []() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    });
}

ComPtr<ID3D11Device> CreateD3D11Device() {
    ComPtr<ID3D11Device> device;
    const D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0};
    const HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
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

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice CreateWinRtDevice(
    ID3D11Device* d3d_device) {
    ComPtr<IDXGIDevice> dxgi_device;
    winrt::check_hresult(d3d_device->QueryInterface(IID_PPV_ARGS(&dxgi_device)));

    winrt::com_ptr<IInspectable> inspectable;
    winrt::check_hresult(
        CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(), inspectable.put()));

    return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateCaptureItem(HWND hwnd) {
    const auto interop =
        winrt::get_activation_factory<
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
            IGraphicsCaptureItemInterop>();

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(interop->CreateForWindow(
        hwnd,
        winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
        winrt::put_abi(item)));
    return item;
}

}  // namespace

std::optional<CaptureResult> CaptureWindowWgc(HWND hwnd, bool client_only, std::string& error) {
    try {
        EnsureWinRtInitialized();
        EnsureCaptureDpiAwareness();

        if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
            error = "Windows Graphics Capture is not supported on this system.";
            return std::nullopt;
        }

        const auto d3d_device = CreateD3D11Device();
        if (!d3d_device) {
            error = "Failed to create D3D11 device for WGC.";
            return std::nullopt;
        }

        const auto winrt_device = CreateWinRtDevice(d3d_device.Get());
        const auto item = CreateCaptureItem(hwnd);
        const auto size = item.Size();
        if (size.Width <= 0 || size.Height <= 0) {
            error = "Capture item size is invalid.";
            return std::nullopt;
        }

        auto frame_pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrt_device,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size);

        auto session = frame_pool.CreateCaptureSession(item);
        try {
            session.IsBorderRequired(false);
        } catch (...) {
        }
        try {
            session.IsCursorCaptureEnabled(false);
        } catch (...) {
        }

        std::mutex frame_mutex;
        std::condition_variable frame_cv;
        bool frame_ready = false;
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame latest_frame{nullptr};

        const auto token = frame_pool.FrameArrived([&](const auto& pool, const auto&) {
            const auto frame = pool.TryGetNextFrame();
            if (!frame) {
                return;
            }

            std::lock_guard lock(frame_mutex);
            latest_frame = frame;
            frame_ready = true;
            frame_cv.notify_one();
        });

        session.StartCapture();

        {
            std::unique_lock lock(frame_mutex);
            if (!frame_cv.wait_for(lock, std::chrono::seconds(5), [&] { return frame_ready; })) {
                frame_pool.FrameArrived(token);
                session.Close();
                frame_pool.Close();
                error = "Timed out waiting for WGC frame.";
                return std::nullopt;
            }
        }

        const auto frame = latest_frame;
        frame_pool.FrameArrived(token);
        session.Close();
        frame_pool.Close();

        const auto surface = frame.Surface();
        const auto access = surface.as<
            Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        D3D11_TEXTURE2D_DESC staging_desc = desc;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        winrt::check_hresult(d3d_device->CreateTexture2D(&staging_desc, nullptr, &staging));

        ComPtr<ID3D11DeviceContext> context;
        d3d_device->GetImmediateContext(&context);
        context->CopyResource(staging.Get(), texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        winrt::check_hresult(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));

        CaptureResult result{};
        result.width = desc.Width;
        result.height = desc.Height;
        result.pixels.resize(static_cast<size_t>(desc.Width) * static_cast<size_t>(desc.Height) * 4U);

        for (UINT y = 0; y < desc.Height; ++y) {
            const auto* src_row =
                static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
            auto* dst_row = result.pixels.data() + static_cast<size_t>(y) * desc.Width * 4U;
            std::memcpy(dst_row, src_row, static_cast<size_t>(desc.Width) * 4U);
            for (UINT x = 0; x < desc.Width; ++x) {
                dst_row[x * 4 + 3] = 255;
            }
        }

        context->Unmap(staging.Get(), 0);

        if (client_only) {
            return CropClientAreaFromWindow(hwnd, std::move(result), error);
        }

        return result;
    } catch (const winrt::hresult_error& ex) {
        error = winrt::to_string(ex.message());
        return std::nullopt;
    }
}

}  // namespace gc
