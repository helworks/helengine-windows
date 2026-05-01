#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace helengine::windows {
    /// Owns the first DirectX11 device, context, swap chain, and back-buffer render target.
    class DirectX11Bootstrap {
    public:
        /// Creates the DirectX11 bootstrap for one native window.
        DirectX11Bootstrap(HWND windowHandle, int width, int height);

        /// Releases all DirectX11 resources.
        ~DirectX11Bootstrap();

        /// Gets the Direct3D 11 device.
        ID3D11Device* GetDevice() const;

        /// Gets the immediate device context.
        ID3D11DeviceContext* GetDeviceContext() const;

        /// Gets the swap chain bound to the native window.
        IDXGISwapChain1* GetSwapChain() const;

        /// Gets the back-buffer render target view.
        ID3D11RenderTargetView* GetRenderTargetView() const;

        /// Gets the back-buffer depth-stencil view.
        ID3D11DepthStencilView* GetDepthStencilView() const;

        /// Gets the current swap-chain width in pixels.
        int GetWidth() const;

        /// Gets the current swap-chain height in pixels.
        int GetHeight() const;

        /// Recreates the back-buffer resources for a new client size.
        void Resize(int width, int height);

    private:
        /// Creates the hardware Direct3D 11 device and immediate context.
        void CreateDevice();

        /// Creates the DXGI swap chain for the current native window.
        void CreateSwapChain();

        /// Creates the back-buffer render target view from the swap chain.
        void CreateRenderTargetView();

        /// Creates the back-buffer depth-stencil resources for the current client size.
        void CreateDepthStencilView();

        /// Releases the current back-buffer render target binding and view.
        void ReleaseRenderTargetView();

        /// Releases the current depth-stencil resources.
        void ReleaseDepthStencilView();

        /// Throws when one native DirectX call fails.
        static void ThrowIfFailed(HRESULT result, const char* message);

        /// Stores the target native window handle.
        HWND WindowHandle;

        /// Stores the target client width.
        int Width;

        /// Stores the target client height.
        int Height;

        /// Stores the Direct3D 11 device.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;

        /// Stores the Direct3D 11 immediate context.
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeviceContext;

        /// Stores the created swap chain.
        Microsoft::WRL::ComPtr<IDXGISwapChain1> SwapChain;

        /// Stores the created back-buffer render target view.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> RenderTargetView;

        /// Stores the created depth-stencil texture for the back buffer.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> DepthStencilBuffer;

        /// Stores the created depth-stencil view for the back buffer.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> DepthStencilView;
    };
}
