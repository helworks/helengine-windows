#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <memory>
#include "platform/windows/directx11/directx11_output_color_state.hpp"

namespace helengine::windows {
    class DirectX11OutputAttachments;
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
        /// <param name="sampleCount">Requests 1x or 4x engine rendering without multisampling the swapchain.</param>
        /// <param name="useDesktop">Preserves the layered Explorer child; HDR uses opaque DirectComposition and SDR retains blt presentation.</param>
        DirectX11Bootstrap(HWND windowHandle, int width, int height, bool useComposition, bool useDesktop = false, UINT sampleCount = 1);

        /// Creates an additional swap chain using the primary device and context; resource caches remain shared.
        /// useComposition selects overlay alpha; useDesktop keeps an opaque Explorer child with HDR composition or SDR blt.
        DirectX11Bootstrap(HWND windowHandle, int width, int height, bool useComposition, DirectX11Bootstrap& sharedDevice, bool useDesktop = false, UINT sampleCount = 1);

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

        /// Gets the requested engine color/depth sample count; the swapchain itself remains single-sample.
        UINT GetSampleCount() const;

        /// Gets the actual presentation format selected after explicit HDR capability validation.
        DXGI_FORMAT GetOutputFormat() const;

        /// Reports whether the current swapchain presents linear FP16 scRGB through the compositor.
        bool IsHdrOutput() const;

        /// Gets Windows UI reference white in scRGB units; scene lighting uses its own 80-nit reference.
        float GetSdrWhiteScale() const;

        /// Refreshes monitor/HDR/reference-white changes at bounded intervals before a new output frame begins.
        void RefreshOutputColorState();

        /// Resolves the completed output before hit-test readback, capture or presentation.
        void ResolveOutput();

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

        /// Creates a two-buffer flip-sequential composition swapchain, opaque for desktop and premultiplied for overlay, and shows it
        /// on the window through a DirectComposition device, a topmost target, one visual, SetContent, SetRoot and
        /// Commit. Throws std::runtime_error carrying the failing HRESULT in hexadecimal when any call fails.
        void CreateCompositionSwapChain();

        /// Applies scRGB only after the created swapchain explicitly reports color-space presentation support.
        void ConfigureOutputColorSpace();

        /// Disconnects the visual and releases presentation resources before changing the output format.
        void ReleaseSwapChain();

        /// Writes the selected output transfer, white levels and capability fallback to native diagnostics.
        void WriteOutputColorDiagnostics() const;

        /// Creates all output attachments against the current single-sample swapchain buffer.
        void CreateOutputAttachments();

        /// Unbinds and releases every attachment reference before ResizeBuffers.
        void ReleaseOutputAttachments();

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

        /// Stores semantic overlay mode; HDR desktop composition independently preserves its opaque input contract.
        bool UseComposition;

        /// Selects an opaque Explorer child, using HDR composition or the existing SDR blt fallback.
        bool UseDesktop;

        /// Stores the validated per-output engine sample count.
        UINT SampleCount;

        /// Keeps monitor capabilities and Windows UI reference white local to this output.
        DirectX11OutputColorState OutputColor;

        /// Throttles OS display discovery while still applying reference-white changes during rendering.
        ULONGLONG NextColorQueryTick = 0;

        /// Stores the Direct3D 11 device.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;

        /// Stores the Direct3D 11 immediate context.
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeviceContext;

        /// Stores the created swap chain.
        Microsoft::WRL::ComPtr<IDXGISwapChain1> SwapChain;

        /// Owns compatible color/depth resources and retains the resolve destination until resize.
        std::unique_ptr<DirectX11OutputAttachments> OutputAttachments;

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
