#include "platform/windows/directx11/directx11_bootstrap.hpp"

#include <stdexcept>

namespace helengine::windows {
    /// Creates the DirectX11 bootstrap for one native window.
    DirectX11Bootstrap::DirectX11Bootstrap(HWND windowHandle, int width, int height)
        : WindowHandle(windowHandle)
        , Width(width)
        , Height(height) {
        CreateDevice();
        CreateSwapChain();
        CreateRenderTargetView();
    }

    /// Releases all DirectX11 resources.
    DirectX11Bootstrap::~DirectX11Bootstrap() = default;

    /// Gets the Direct3D 11 device.
    ID3D11Device* DirectX11Bootstrap::GetDevice() const {
        return Device.Get();
    }

    /// Gets the immediate device context.
    ID3D11DeviceContext* DirectX11Bootstrap::GetDeviceContext() const {
        return DeviceContext.Get();
    }

    /// Gets the swap chain bound to the native window.
    IDXGISwapChain1* DirectX11Bootstrap::GetSwapChain() const {
        return SwapChain.Get();
    }

    /// Gets the back-buffer render target view.
    ID3D11RenderTargetView* DirectX11Bootstrap::GetRenderTargetView() const {
        return RenderTargetView.Get();
    }

    /// Creates the hardware Direct3D 11 device and immediate context.
    void DirectX11Bootstrap::CreateDevice() {
        static const D3D_FEATURE_LEVEL FeatureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        D3D_FEATURE_LEVEL createdFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        HRESULT result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            FeatureLevels,
            static_cast<UINT>(sizeof(FeatureLevels) / sizeof(FeatureLevels[0])),
            D3D11_SDK_VERSION,
            Device.GetAddressOf(),
            &createdFeatureLevel,
            DeviceContext.GetAddressOf());

        ThrowIfFailed(result, "D3D11CreateDevice failed for the HelEngine Windows host.");
    }

    /// Creates the DXGI swap chain for the current native window.
    void DirectX11Bootstrap::CreateSwapChain() {
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        ThrowIfFailed(Device.As(&dxgiDevice), "ID3D11Device to IDXGIDevice query failed.");

        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        ThrowIfFailed(dxgiDevice->GetAdapter(adapter.GetAddressOf()), "IDXGIDevice::GetAdapter failed.");

        Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        ThrowIfFailed(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())), "IDXGIAdapter::GetParent for IDXGIFactory2 failed.");

        DXGI_SWAP_CHAIN_DESC1 swapChainDescription {};
        swapChainDescription.Width = static_cast<UINT>(Width);
        swapChainDescription.Height = static_cast<UINT>(Height);
        swapChainDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDescription.SampleDesc.Count = 1;
        swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDescription.BufferCount = 2;
        swapChainDescription.Scaling = DXGI_SCALING_STRETCH;
        swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        ThrowIfFailed(
            factory->CreateSwapChainForHwnd(
                Device.Get(),
                WindowHandle,
                &swapChainDescription,
                nullptr,
                nullptr,
                SwapChain.GetAddressOf()),
            "IDXGIFactory2::CreateSwapChainForHwnd failed.");

        ThrowIfFailed(factory->MakeWindowAssociation(WindowHandle, DXGI_MWA_NO_ALT_ENTER), "IDXGIFactory2::MakeWindowAssociation failed.");
    }

    /// Creates the back-buffer render target view from the swap chain.
    void DirectX11Bootstrap::CreateRenderTargetView() {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        ThrowIfFailed(SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf())), "IDXGISwapChain1::GetBuffer failed for the back buffer.");
        ThrowIfFailed(Device->CreateRenderTargetView(backBuffer.Get(), nullptr, RenderTargetView.GetAddressOf()), "ID3D11Device::CreateRenderTargetView failed for the back buffer.");
    }

    /// Throws when one native DirectX call fails.
    void DirectX11Bootstrap::ThrowIfFailed(HRESULT result, const char* message) {
        if (FAILED(result)) {
            throw std::runtime_error(message);
        }
    }
}
