#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_output_attachments.hpp"
#include "platform/windows/directx11/directx11_debug_diagnostics.hpp"

#include <sstream>
#include <stdexcept>
#include <iostream>

#include "platform/windows/directx11/directx11_hresult_formatter.hpp"
#include "platform/windows/runtime/runtime_render_diagnostics.hpp"

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
        CreateDevice();
        OutputColor=DirectX11OutputColorState::Query(WindowHandle,Device.Get(),SampleCount);
        CreateSwapChain();
        ConfigureOutputColorSpace();
        CreateOutputAttachments();
        WriteOutputColorDiagnostics();
    }

    /// Creates window-owned presentation resources on the existing shared DirectX11 device.
    DirectX11Bootstrap::DirectX11Bootstrap(HWND windowHandle, int width, int height, bool useComposition, DirectX11Bootstrap& sharedDevice, bool useDesktop, UINT sampleCount)
        : WindowHandle(windowHandle), Width(width), Height(height), UseComposition(useComposition), UseDesktop(useDesktop), SampleCount(sampleCount),
          Device(sharedDevice.GetDevice()), DeviceContext(sharedDevice.GetDeviceContext()) {
        if (SampleCount != 1 && SampleCount != 4) { throw std::invalid_argument("Output sample count must be 1 or 4."); }
        if (windowHandle == nullptr || width <= 0 || height <= 0) {
            throw std::invalid_argument("A secondary swap chain requires a live window and positive client dimensions.");
        }
        OutputColor=DirectX11OutputColorState::Query(WindowHandle,Device.Get(),SampleCount);
        CreateSwapChain();
        ConfigureOutputColorSpace();
        CreateOutputAttachments();
        WriteOutputColorDiagnostics();
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
            DirectX11DebugDiagnostics::CreationFlags(),
            FeatureLevels,
            static_cast<UINT>(sizeof(FeatureLevels) / sizeof(FeatureLevels[0])),
            D3D11_SDK_VERSION,
            Device.GetAddressOf(),
            &createdFeatureLevel,
            DeviceContext.GetAddressOf());

        ThrowIfFailed(result, "D3D11CreateDevice failed for the HelEngine Windows host.");
        DirectX11DebugDiagnostics::Configure(Device.Get());
    }

    /// Creates the DXGI swap chain for the current native window: the composition swap chain in overlay mode,
    /// otherwise the window swap chain with exactly the calls the player has always made.
    void DirectX11Bootstrap::CreateSwapChain() {
        if (UseComposition || (UseDesktop && OutputColor.Hdr)) {
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
        swapChainDescription.Format = GetOutputFormat();
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

        HRESULT createResult=factory->CreateSwapChainForHwnd(Device.Get(),WindowHandle,&swapChainDescription,nullptr,nullptr,SwapChain.GetAddressOf());
        if(createResult==DXGI_ERROR_UNSUPPORTED && OutputColor.Hdr) {
            OutputColor.Hdr=false;OutputColor.Reason="fp16_swapchain_unsupported";CreateSwapChain();return;
        }
        ThrowIfCompositionFailed(createResult,"IDXGIFactory2::CreateSwapChainForHwnd");

        ThrowIfFailed(factory->MakeWindowAssociation(WindowHandle, DXGI_MWA_NO_ALT_ENTER), "IDXGIFactory2::MakeWindowAssociation failed.");
    }

    /// Creates a two-buffer flip-sequential composition swapchain, opaque for desktop and premultiplied for overlay, and shows it
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
        swapChainDescription.Format = GetOutputFormat();
        swapChainDescription.SampleDesc.Count = 1;
        swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDescription.BufferCount = 2;
        swapChainDescription.Scaling = DXGI_SCALING_STRETCH;
        swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapChainDescription.AlphaMode = UseDesktop ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED;

        HRESULT createResult=factory->CreateSwapChainForComposition(Device.Get(),&swapChainDescription,nullptr,SwapChain.GetAddressOf());
        if(createResult==DXGI_ERROR_UNSUPPORTED && OutputColor.Hdr) {
            OutputColor.Hdr=false;OutputColor.Reason="fp16_swapchain_unsupported";CreateSwapChain();return;
        }
        ThrowIfCompositionFailed(createResult,"IDXGIFactory2::CreateSwapChainForComposition");

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
    /// Gets the actual format shared by swapchain, attachments and all format-dependent readback consumers.
    DXGI_FORMAT DirectX11Bootstrap::GetOutputFormat() const {return OutputColor.Hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;}
    /// Reports actual native HDR presentation rather than the scene's internal floating-point storage.
    bool DirectX11Bootstrap::IsHdrOutput() const {return OutputColor.Hdr;}
    /// Keeps the UI's Windows reference white independent of exposure and scene highlight compression.
    float DirectX11Bootstrap::GetSdrWhiteScale() const {return OutputColor.Hdr ? OutputColor.SdrWhiteScale : 1.f;}
    /// Validates color-space presentation; unsupported capability falls back explicitly, unexpected failures remain fatal.
    void DirectX11Bootstrap::ConfigureOutputColorSpace() {
        if(!OutputColor.Hdr) return;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> advanced;
        HRESULT result=SwapChain.As(&advanced);
        UINT support=0;
        if(result!=E_NOINTERFACE) {
            ThrowIfCompositionFailed(result,"Query HDR swapchain interface");
            ThrowIfCompositionFailed(advanced->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,&support),"Check scRGB presentation support");
        }
        if(result==E_NOINTERFACE || !(support&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
            OutputColor.Hdr=false;OutputColor.Reason="scrgb_presentation_unsupported";
            ReleaseSwapChain();CreateSwapChain();return;
        }
        ThrowIfCompositionFailed(advanced->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709),"Set linear scRGB presentation");
    }
    /// Detaches composition references before destroying or replacing a swapchain.
    void DirectX11Bootstrap::ReleaseSwapChain() {
        ReleaseOutputAttachments();
        if(CompositionVisual) ThrowIfCompositionFailed(CompositionVisual->SetContent(nullptr),"Detach output composition content");
        if(CompositionTarget) ThrowIfCompositionFailed(CompositionTarget->SetRoot(nullptr),"Detach output composition root");
        if(CompositionDevice) ThrowIfCompositionFailed(CompositionDevice->Commit(),"Commit output composition detach");
        CompositionVisual.Reset();CompositionTarget.Reset();CompositionDevice.Reset();SwapChain.Reset();
        DeviceContext->Flush();
    }
    /// Applies HDR/display-mode changes before output views are borrowed for a frame.
    void DirectX11Bootstrap::RefreshOutputColorState() {
        ULONGLONG now=GetTickCount64();
        HMONITOR monitor=MonitorFromWindow(WindowHandle,MONITOR_DEFAULTTONEAREST);
        if(now<NextColorQueryTick && monitor==OutputColor.Monitor) return;
        NextColorQueryTick=now+1000;
        auto updated=DirectX11OutputColorState::Query(WindowHandle,Device.Get(),SampleCount);
        if(updated.Hdr && updated.Monitor==OutputColor.Monitor && (OutputColor.Reason=="scrgb_presentation_unsupported" || OutputColor.Reason=="fp16_swapchain_unsupported")) {
            updated.Hdr=false;updated.Reason=OutputColor.Reason;
        }
        bool replace=updated.Hdr!=OutputColor.Hdr;
        bool changed=replace || updated.Monitor!=OutputColor.Monitor || updated.SdrWhiteScale!=OutputColor.SdrWhiteScale;
        OutputColor=updated;
        if(replace) {ReleaseSwapChain();CreateSwapChain();ConfigureOutputColorSpace();CreateOutputAttachments();}
        if(changed) WriteOutputColorDiagnostics();
    }
    /// Emits the complete native output contract without reading any desktop pixels.
    void DirectX11Bootstrap::WriteOutputColorDiagnostics() const {
        std::ostringstream message;
        message<<"native-output hwnd="<<WindowHandle<<" desktop="<<UseDesktop<<" overlay="<<UseComposition
            <<" hdr="<<OutputColor.Hdr<<" format="<<GetOutputFormat()<<" transfer="<<(OutputColor.Hdr?"scRGB_linear":"sRGB_SDR")
            <<" ui_white_nits="<<GetSdrWhiteScale()*80<<" scene_reference_nits=80 scene_limit_nits=400 reported_peak_nits="<<OutputColor.ReportedPeakNits
            <<" reason="<<OutputColor.Reason<<'\n';
        OutputDebugStringA(message.str().c_str());std::clog<<message.str();
        RuntimeRenderDiagnostics::WriteHostEvent("native-output",message.str());
    }
    /// Resolves color and alpha before any external consumer reads this output's single-sample destination.
    void DirectX11Bootstrap::ResolveOutput() { OutputAttachments->Resolve(DeviceContext.Get()); }
    /// Throws when one native DirectX call fails.
    void DirectX11Bootstrap::ThrowIfFailed(HRESULT result, const char* message) {
        if (FAILED(result)) {
            throw std::runtime_error(std::string(message)+" HRESULT "+DirectX11HResultFormatter::ToHex(result));
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
