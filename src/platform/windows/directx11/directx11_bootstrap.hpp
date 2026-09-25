#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace helengine::windows {
    /// Owns the first DirectX11 device, context, swap chain, and back-buffer render target. In the default mode the
    /// swap chain is bound to the window with CreateSwapChainForHwnd; in the opt-in overlay mode it is a premultiplied
    /// composition swap chain that DirectComposition shows on the window.
    class DirectX11Bootstrap {
    public:
        /// Creates the DirectX11 bootstrap for one native window.
        /// <param name="windowHandle">Window the swap chain presents to.</param>
        /// <param name="width">Initial back-buffer width in pixels.</param>
        /// <param name="height">Initial back-buffer height in pixels.</param>
        /// <param name="useComposition">True only in overlay mode: create a premultiplied composition swap chain and
        /// the DirectComposition device, target and visual instead of the window swap chain.</param>
        DirectX11Bootstrap(HWND windowHandle, int width, int height, bool useComposition);

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

        /// Creates the DXGI swap chain for the current native window: the composition swap chain in overlay mode,
        /// otherwise the window swap chain with exactly the calls the player has always made.
        void CreateSwapChain();

        /// Creates the premultiplied composition swap chain (B8G8R8A8, 2 buffers, STRETCH, FLIP_DISCARD) and shows it
        /// on the window through a DirectComposition device, a topmost target, one visual, SetContent, SetRoot and
        /// Commit. Throws std::runtime_error carrying the failing HRESULT in hexadecimal when any call fails.
        void CreateCompositionSwapChain();

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

        /// Throws std::runtime_error naming the failed composition-path operation and carrying its HRESULT in
        /// hexadecimal, so an overlay bootstrap failure is diagnosable from the startup log.
        static void ThrowIfCompositionFailed(HRESULT result, const char* operation);

        /// Stores the target native window handle.
        HWND WindowHandle;

        /// Stores the target client width.
        int Width;

        /// Stores the target client height.
        int Height;

        /// Stores whether the swap chain is a composition swap chain shown through DirectComposition (overlay mode).
        bool UseComposition;

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

        /// Stores the DirectComposition device in overlay mode; null otherwise. Declared after the swap chain and the
        /// device so it is released before them.
        Microsoft::WRL::ComPtr<IDCompositionDevice> CompositionDevice;

        /// Stores the topmost DirectComposition target bound to the window in overlay mode; null otherwise. Released
        /// before the composition device.
        Microsoft::WRL::ComPtr<IDCompositionTarget> CompositionTarget;

        /// Stores the root visual whose content is the swap chain in overlay mode; null otherwise. Released first.
        Microsoft::WRL::ComPtr<IDCompositionVisual> CompositionVisual;
    };
}
