#include "CaptureSession.h"

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
#include <new>

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

bool IsWindowValid(HWND hwnd, std::string& error) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        error = "Invalid window handle.";
        return false;
    }
    return true;
}

std::optional<CaptureResult> CopyTextureToCaptureResult(
    ID3D11Device* device,
    ID3D11Texture2D* texture,
    std::string& error) {
    if (device == nullptr || texture == nullptr) {
        error = "Invalid texture for capture copy.";
        return std::nullopt;
    }

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&staging_desc, nullptr, &staging))) {
        error = "Failed to create staging texture.";
        return std::nullopt;
    }

    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    context->CopyResource(staging.Get(), texture);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        error = "Failed to map staging texture.";
        return std::nullopt;
    }

    CaptureResult result{};
    result.width = desc.Width;
    result.height = desc.Height;
    result.pixels.resize(static_cast<size_t>(desc.Width) * static_cast<size_t>(desc.Height) * 4U);

    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* src_row =
            static_cast<const uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        auto* dst_row = result.pixels.data() + static_cast<size_t>(y) * desc.Width * 4U;
        std::memcpy(dst_row, src_row, static_cast<size_t>(desc.Width) * 4U);
    }

    context->Unmap(staging.Get(), 0);
    return result;
}

std::optional<CaptureResult> FinalizeFrame(
    HWND hwnd,
    bool client_only,
    CaptureResult frame,
    const CaptureRegion* region,
    std::string& error) {
    if (client_only) {
        auto cropped = CropClientAreaFromWindow(hwnd, std::move(frame), error);
        if (!cropped.has_value()) {
            return std::nullopt;
        }
        frame = std::move(*cropped);
    }

    if (region != nullptr) {
        return CropImageRegion(std::move(frame), *region, error);
    }

    return frame;
}

}  // namespace

struct CaptureSession::Impl {
    HWND hwnd = nullptr;
    GC_Flags flags = GC_FLAG_NONE;
    bool client_only = true;
    bool force_gdi = false;
    bool wgc_active = false;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem wgc_item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool wgc_frame_pool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession wgc_session{nullptr};
    ComPtr<ID3D11Device> d3d_device;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrt_device{nullptr};
    winrt::Windows::Graphics::SizeInt32 wgc_pool_size{};

    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    bool frame_ready = false;
    winrt::event_token frame_token{};

    bool InitializeWgc(std::string& error);
    bool EnsureWgcPoolSize(std::string& error);
    std::optional<CaptureResult> CaptureFrameWgc(const CaptureRegion* region, std::string& error);
    std::optional<CaptureResult> CaptureFrameGdi(const CaptureRegion* region, std::string& error);
    void ShutdownWgc();
};

bool CaptureSession::Impl::InitializeWgc(std::string& error) {
    try {
        EnsureWinRtInitialized();

        if (!winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()) {
            error = "Windows Graphics Capture is not supported on this system.";
            return false;
        }

        d3d_device = CreateD3D11Device();
        if (!d3d_device) {
            error = "Failed to create D3D11 device for WGC session.";
            return false;
        }

        winrt_device = CreateWinRtDevice(d3d_device.Get());
        wgc_item = CreateCaptureItem(hwnd);
        wgc_pool_size = wgc_item.Size();
        if (wgc_pool_size.Width <= 0 || wgc_pool_size.Height <= 0) {
            error = "Capture item size is invalid.";
            return false;
        }

        wgc_frame_pool =
            winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                winrt_device,
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                2,
                wgc_pool_size);

        wgc_session = wgc_frame_pool.CreateCaptureSession(wgc_item);
        try {
            wgc_session.IsBorderRequired(false);
        } catch (...) {
        }
        try {
            wgc_session.IsCursorCaptureEnabled(false);
        } catch (...) {
        }

        frame_token = wgc_frame_pool.FrameArrived([this](const auto&, const auto&) {
            {
                std::lock_guard lock(frame_mutex);
                frame_ready = true;
            }
            frame_cv.notify_one();
        });

        wgc_session.StartCapture();

        {
            std::unique_lock lock(frame_mutex);
            frame_cv.wait_for(lock, std::chrono::seconds(5), [this] { return frame_ready; });
        }

        return true;
    } catch (const winrt::hresult_error& ex) {
        error = winrt::to_string(ex.message());
        return false;
    }
}

bool CaptureSession::Impl::EnsureWgcPoolSize(std::string& error) {
    try {
        const auto new_size = wgc_item.Size();
        if (new_size.Width <= 0 || new_size.Height <= 0) {
            error = "Capture item size is invalid.";
            return false;
        }

        if (new_size.Width == wgc_pool_size.Width && new_size.Height == wgc_pool_size.Height) {
            return true;
        }

        wgc_frame_pool.Recreate(
            winrt_device,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            new_size);
        wgc_pool_size = new_size;

        {
            std::lock_guard lock(frame_mutex);
            frame_ready = false;
        }

        return true;
    } catch (const winrt::hresult_error& ex) {
        error = winrt::to_string(ex.message());
        return false;
    }
}

std::optional<CaptureResult> CaptureSession::Impl::CaptureFrameWgc(
    const CaptureRegion* region,
    std::string& error) {
    try {
        if (!EnsureWgcPoolSize(error)) {
            return std::nullopt;
        }

        auto frame = wgc_frame_pool.TryGetNextFrame();
        if (!frame) {
            {
                std::lock_guard lock(frame_mutex);
                frame_ready = false;
            }

            std::unique_lock lock(frame_mutex);
            if (!frame_cv.wait_for(lock, std::chrono::milliseconds(500), [this] {
                    return frame_ready;
                })) {
                error = "Timed out waiting for WGC frame.";
                return std::nullopt;
            }
            lock.unlock();

            frame = wgc_frame_pool.TryGetNextFrame();
            if (!frame) {
                error = "No WGC frame available.";
                return std::nullopt;
            }
        }

        const auto surface = frame.Surface();
        const auto access = surface.as<
            Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));

        auto copied = CopyTextureToCaptureResult(d3d_device.Get(), texture.Get(), error);
        if (!copied.has_value()) {
            return std::nullopt;
        }

        return FinalizeFrame(hwnd, client_only, std::move(*copied), region, error);
    } catch (const winrt::hresult_error& ex) {
        error = winrt::to_string(ex.message());
        return std::nullopt;
    }
}

std::optional<CaptureResult> CaptureSession::Impl::CaptureFrameGdi(
    const CaptureRegion* region,
    std::string& error) {
    return CaptureWindow(hwnd, flags, region, error);
}

void CaptureSession::Impl::ShutdownWgc() {
    if (!wgc_active) {
        return;
    }

    try {
        if (wgc_frame_pool && frame_token) {
            wgc_frame_pool.FrameArrived(frame_token);
            frame_token = {};
        }
        if (wgc_session) {
            wgc_session.Close();
            wgc_session = nullptr;
        }
        if (wgc_frame_pool) {
            wgc_frame_pool.Close();
            wgc_frame_pool = nullptr;
        }
    } catch (...) {
    }

    wgc_item = nullptr;
    winrt_device = nullptr;
    d3d_device.Reset();
    wgc_active = false;
}

CaptureSession* CaptureSession::Open(HWND hwnd, GC_Flags flags, std::string& error) {
    if (!IsWindowValid(hwnd, error)) {
        return nullptr;
    }

    auto* session = new (std::nothrow) CaptureSession();
    if (session == nullptr) {
        error = "Failed to allocate capture session.";
        return nullptr;
    }

    session->impl_ = std::make_unique<Impl>();
    session->impl_->hwnd = hwnd;
    session->impl_->flags = flags;
    session->impl_->client_only = (flags & GC_FLAG_INCLUDE_FRAME) == 0;
    session->impl_->force_gdi = (flags & GC_FLAG_FORCE_GDI) != 0;

    if (!session->impl_->force_gdi && session->impl_->InitializeWgc(error)) {
        session->impl_->wgc_active = true;
        return session;
    }

    if (session->impl_->force_gdi) {
        error.clear();
        return session;
    }

    delete session;
    if (error.empty()) {
        error = "Failed to initialize capture session.";
    }
    return nullptr;
}

std::optional<CaptureResult> CaptureSession::CaptureFrame(
    const CaptureRegion* region,
    std::string& error) {
    if (impl_ == nullptr || !IsWindowValid(impl_->hwnd, error)) {
        return std::nullopt;
    }

    if (impl_->wgc_active) {
        return impl_->CaptureFrameWgc(region, error);
    }

    return impl_->CaptureFrameGdi(region, error);
}

void CaptureSession::Close() {
    if (impl_ != nullptr) {
        impl_->ShutdownWgc();
        impl_->hwnd = nullptr;
    }
}

CaptureSession::~CaptureSession() {
    Close();
}

}  // namespace gc
