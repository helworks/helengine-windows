#include "platform/windows/directx11/directx11_output_attachments.hpp"
#include "platform/windows/directx11/directx11_hresult_formatter.hpp"
#include <stdexcept>
#include <string>

namespace helengine::windows {
    /// Creates complete output attachments before exposing any views to the renderer.
    DirectX11OutputAttachments::DirectX11OutputAttachments(ID3D11Device* device, ID3D11Texture2D* destination, UINT sampleCount)
        : Device(device), Destination(destination), SampleCount(sampleCount) {
        if (!device || !destination) { throw std::invalid_argument("Output attachments require a device and a resolve destination."); }
        if (sampleCount != 1 && sampleCount != 4) { throw std::invalid_argument("Output sample count must be 1 or 4."); }
        Microsoft::WRL::ComPtr<ID3D11Device> owner;
        destination->GetDevice(&owner);
        if (owner.Get() != device) { throw std::invalid_argument("Output destination belongs to another graphics device."); }
        D3D11_TEXTURE2D_DESC description {};
        destination->GetDesc(&description);
        if (description.Width == 0 || description.Height == 0 || description.MipLevels != 1 || description.ArraySize != 1
            || description.Format != DXGI_FORMAT_B8G8R8A8_UNORM || description.SampleDesc.Count != 1
            || description.SampleDesc.Quality != 0 || !(description.BindFlags & D3D11_BIND_RENDER_TARGET)) {
            throw std::invalid_argument("Output destination must be a single-sample BGRA8 render target with one mip and array slice.");
        }
        ID3D11Texture2D* color = destination;
        if (SampleCount == 4) {
            RequireMultisampleSupport(description.Format);
            RequireMultisampleSupport(DXGI_FORMAT_D24_UNORM_S8_UINT);
            D3D11_TEXTURE2D_DESC multisampled = description;
            multisampled.SampleDesc.Count = SampleCount;
            multisampled.Usage = D3D11_USAGE_DEFAULT;
            multisampled.BindFlags = D3D11_BIND_RENDER_TARGET;
            multisampled.CPUAccessFlags = 0; multisampled.MiscFlags = 0;
            Check(device->CreateTexture2D(&multisampled,nullptr,&MultisampledColor),"Create multisampled output color");
            color = MultisampledColor.Get();
        }
        Check(device->CreateRenderTargetView(color,nullptr,&ColorView),"Create output color view");
        D3D11_TEXTURE2D_DESC depth {};
        depth.Width=description.Width; depth.Height=description.Height; depth.MipLevels=1; depth.ArraySize=1;
        depth.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; depth.SampleDesc.Count=SampleCount;
        depth.Usage=D3D11_USAGE_DEFAULT; depth.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        Check(device->CreateTexture2D(&depth,nullptr,&DepthBuffer),"Create output depth storage");
        Check(device->CreateDepthStencilView(DepthBuffer.Get(),nullptr,&DepthView),"Create output depth view");
    }
    /// Gets the output's engine color view.
    ID3D11RenderTargetView* DirectX11OutputAttachments::GetRenderTargetView() const { return ColorView.Get(); }
    /// Gets the depth/stencil view matching this output's color surface.
    ID3D11DepthStencilView* DirectX11OutputAttachments::GetDepthStencilView() const { return DepthView.Get(); }
    /// Gets the explicit sample count without inspecting or changing renderer state.
    UINT DirectX11OutputAttachments::GetSampleCount() const { return SampleCount; }
    /// Resolves the completed output without changing swapchain formats, presentation timing or another output's resources.
    void DirectX11OutputAttachments::Resolve(ID3D11DeviceContext* context) {
        if (!context) { throw std::invalid_argument("Output resolve requires a graphics context."); }
        Microsoft::WRL::ComPtr<ID3D11Device> owner;
        context->GetDevice(&owner);
        if (owner.Get() != Device.Get()) { throw std::invalid_argument("Output resolve context belongs to another graphics device."); }
        if (SampleCount == 4) {
            context->OMSetRenderTargets(0,nullptr,nullptr);
            context->ResolveSubresource(Destination.Get(),0,MultisampledColor.Get(),0,DXGI_FORMAT_B8G8R8A8_UNORM);
        }
    }
    /// Requires quality index zero to exist for both multisampled render-target formats.
    void DirectX11OutputAttachments::RequireMultisampleSupport(DXGI_FORMAT format) {
        UINT levels=0;
        Check(Device->CheckMultisampleQualityLevels(format,SampleCount,&levels),"Check output multisample support");
        if (levels == 0) { throw std::runtime_error("The output device does not support requested 4x MSAA for color and depth."); }
    }
    /// Reports graphics failures with the failing operation and original HRESULT.
    void DirectX11OutputAttachments::Check(HRESULT result,const char* operation) {
        if (FAILED(result)) { throw std::runtime_error(std::string(operation)+" failed with HRESULT "+DirectX11HResultFormatter::ToHex(result)); }
    }
}