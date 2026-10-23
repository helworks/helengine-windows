#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_output_attachments.hpp"

#include <sstream>
#include <stdexcept>

#include "platform/windows/directx11/directx11_hresult_formatter.hpp"

namespace helengine::windows {
    /// Creates the DirectX11 bootstrap for one native window.
    /// <param name="windowHandle">Window the swap chain presents to.</param>
    /// <param name="width">Initial back-buffer width in pixels.</param>
    /// <param name="height">Initial back-buffer height in pixels.</param>
    /// <param name="useComposition">True only in overlay mode: create a premultiplied composition swap chain and
    /// the DirectComposition device, target and visual instead of the window swap chain.</param>
    DirectX11Bootstrap::DirectX11Bootstrap(HWND windowHandle, int width, int height, bool useComposition, bool useDesktop, UINT sampleCount)
        : WindowHandle(windowHandle)
        , Width(width)
        , Height(height)
        , UseComposition(useComposition)
        , UseDesktop(useDesktop), SampleCount(sampleCount) {
        if (SampleCount != 1 && SampleCount != 4) { throw std::invalid_argument("Output sample count must be 1 or 4."); }
        if (UseDesktop && UseComposition) { throw std::invalid_argument("Desktop presentation cannot use overlay composition."); }
        CreateDevice();
        CreateSwapChain();
        CreateOutputAttachments();
    }

    /// Creates window-owned presentation resources on the existing shared DirectX11 device.
    DirectX11Bootstrap::DirectX11Bootstrap(HWND windowHandle, int width, int height, bool useComposition, DirectX11Bootstrap& sharedDevice, bool useDesktop, UINT sampleCount)
        : WindowHandle(windowHandle), Width(width), Height(height), UseComposition(useComposition), UseDesktop(useDesktop), SampleCount(sampleCount),
          Device(sharedDevice.GetDevice()), DeviceContext(sharedDevice.GetDeviceContext()) {
        if (SampleCount != 1 && SampleCount != 4) { throw std::invalid_argument("Output sample count must be 1 or 4."); }
        if (UseDesktop && UseComposition) { throw std::invalid_argument("Desktop presentation cannot use overlay composition."); }
        if (windowHandle == nullptr || width <= 0 || height <= 0) {
            throw std::invalid_argument("A secondary swap chain requires a live window and positive client dimensions.");
        }
        CreateSwapChain();
        CreateOutputAttachments();
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
        return OutputAttachments->GetRenderTargetView();
    }

    /// Gets the back-buffer depth-stencil view.
    ID3D11DepthStencilView* DirectX11Bootstrap::GetDepthStencilView() const {
        return OutputAttachments->GetDepthStencilView();
    }

    /// Gets the current swap-chain width in pixels.
    int DirectX11Bootstrap::GetWidth() const {
        return Width;
    }

    /// Gets the current swap-chain height in pixels.
    int DirectX11Bootstrap::GetHeight() const {
        return Height;
    }

    /// Recreates the back-buffer resources for a new client size.
    void DirectX11Bootstrap::Resize(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }

        if (Width == width && Height == height) {
            return;
        }

        ReleaseOutputAttachments();
        ThrowIfFailed(
            SwapChain->ResizeBuffers(
                0,
                static_cast<UINT>(width),
                static_cast<UINT>(height),
                DXGI_FORMAT_UNKNOWN,
                0),
            "IDXGISwapChain1::ResizeBuffers failed for the HelEngine Windows host.");

        Width = width;
        Height = height;
        CreateOutputAttachments();
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
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            FeatureLevels,
            static_cast<UINT>(sizeof(FeatureLevels) / sizeof(FeatureLevels[0])),
            D3D11_SDK_VERSION,
            Device.GetAddressOf(),
            &createdFeatureLevel,
            DeviceContext.GetAddressOf());

        ThrowIfFailed(result, "D3D11CreateDevice failed for the HelEngine Windows host.");
    }

    /// Creates the DXGI swap chain for the current native window: the composition swap chain in overlay mode,
    /// otherwise the window swap chain with exactly the calls the player has always made.
    void DirectX11Bootstrap::CreateSwapChain() {
        if (UseComposition) {
            CreateCompositionSwapChain();
            return;
        }

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
        if (UseDesktop) {
            // Explorer's layered child wallpaper path requires blt presentation, including on raised desktops.
            swapChainDescription.BufferCount = 1;
            swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        }

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

    /// Creates the premultiplied composition swap chain (B8G8R8A8, 2 buffers, STRETCH, FLIP_DISCARD) and shows it
    /// on the window through a DirectComposition device, a topmost target, one visual, SetContent, SetRoot and
    /// Commit. Throws std::runtime_error carrying the failing HRESULT in hexadecimal when any call fails.
    void DirectX11Bootstrap::CreateCompositionSwapChain() {
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        ThrowIfCompositionFailed(Device.As(&dxgiDevice), "ID3D11Device to IDXGIDevice query");

        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        ThrowIfCompositionFailed(dxgiDevice->GetAdapter(adapter.GetAddressOf()), "IDXGIDevice::GetAdapter");

        Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        ThrowIfCompositionFailed(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())), "IDXGIAdapter::GetParent for IDXGIFactory2");

        DXGI_SWAP_CHAIN_DESC1 swapChainDescription {};
        swapChainDescription.Width = static_cast<UINT>(Width);
        swapChainDescription.Height = static_cast<UINT>(Height);
        swapChainDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDescription.SampleDesc.Count = 1;
        swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDescription.BufferCount = 2;
        swapChainDescription.Scaling = DXGI_SCALING_STRETCH;
        swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

        ThrowIfCompositionFailed(
            factory->CreateSwapChainForComposition(
                Device.Get(),
                &swapChainDescription,
                nullptr,
                SwapChain.GetAddressOf()),
            "IDXGIFactory2::CreateSwapChainForComposition");

        // DXGI watches the window for Alt+Enter per factory and HWND, independently of how the swap chain presents;
        // the association keeps the player's opt-out of the DXGI fullscreen toggle in overlay mode too.
        ThrowIfCompositionFailed(factory->MakeWindowAssociation(WindowHandle, DXGI_MWA_NO_ALT_ENTER), "IDXGIFactory2::MakeWindowAssociation");

        ThrowIfCompositionFailed(
            DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice), reinterpret_cast<void**>(CompositionDevice.GetAddressOf())),
            "DCompositionCreateDevice");
        ThrowIfCompositionFailed(
            CompositionDevice->CreateTargetForHwnd(WindowHandle, TRUE, CompositionTarget.GetAddressOf()),
            "IDCompositionDevice::CreateTargetForHwnd");
        ThrowIfCompositionFailed(CompositionDevice->CreateVisual(CompositionVisual.GetAddressOf()), "IDCompositionDevice::CreateVisual");
        ThrowIfCompositionFailed(CompositionVisual->SetContent(SwapChain.Get()), "IDCompositionVisual::SetContent");
        ThrowIfCompositionFailed(CompositionTarget->SetRoot(CompositionVisual.Get()), "IDCompositionTarget::SetRoot");
        ThrowIfCompositionFailed(CompositionDevice->Commit(), "IDCompositionDevice::Commit");
    }

    /// Creates complete engine-owned attachments against the current swapchain destination.
    void DirectX11Bootstrap::CreateOutputAttachments() {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        ThrowIfFailed(SwapChain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(backBuffer.GetAddressOf())),"Get output swapchain buffer failed.");
        OutputAttachments = std::make_unique<DirectX11OutputAttachments>(Device.Get(),backBuffer.Get(),SampleCount);
    }
    /// Drops all context bindings and retained buffer references before changing the swapchain size.
    void DirectX11Bootstrap::ReleaseOutputAttachments() {
        DeviceContext->OMSetRenderTargets(0,nullptr,nullptr);
        OutputAttachments.reset();
    }
    /// Gets the per-output engine sample count without changing presentation policy.
    UINT DirectX11Bootstrap::GetSampleCount() const { return SampleCount; }
    /// Resolves color and alpha before any external consumer reads this output's single-sample destination.
    void DirectX11Bootstrap::ResolveOutput() { OutputAttachments->Resolve(DeviceContext.Get()); }
    /// Throws when one native DirectX call fails.
    void DirectX11Bootstrap::ThrowIfFailed(HRESULT result, const char* message) {
        if (FAILED(result)) {
            throw std::runtime_error(message);
        }
    }

    /// Throws std::runtime_error naming the failed composition-path operation and carrying its HRESULT in
    /// hexadecimal, so an overlay bootstrap failure is diagnosable from the startup log.
    void DirectX11Bootstrap::ThrowIfCompositionFailed(HRESULT result, const char* operation) {
        if (FAILED(result)) {
            std::ostringstream messageBuilder;
            messageBuilder << operation << " failed for the HelEngine Windows overlay with HRESULT " << DirectX11HResultFormatter::ToHex(result) << ".";
            throw std::runtime_error(messageBuilder.str());
        }
    }
}
