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

    private:
        /// Creates the hardware Direct3D 11 device and immediate context.
        void CreateDevice();

        /// Creates the DXGI swap chain for the current native window.
        void CreateSwapChain();

        /// Creates the back-buffer render target view from the swap chain.
        void CreateRenderTargetView();

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
    };
}
