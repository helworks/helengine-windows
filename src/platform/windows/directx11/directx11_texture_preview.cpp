#include "directx11_texture_preview.hpp"
#include "directx11_hresult_formatter.hpp"
#include <stdexcept>
#include <string>

namespace helengine::windows {
    DirectX11TexturePreview::DirectX11TexturePreview(ID3D11Device* device,UINT maximumWidth,UINT maximumHeight)
        : Device(device),MaximumWidth(maximumWidth),MaximumHeight(maximumHeight) {
        if(device==nullptr || maximumWidth==0 || maximumHeight==0 || maximumWidth>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || maximumHeight>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
            throw std::invalid_argument("A preview requires a device and positive supported image bounds.");
        }
    }
    ID3D11Texture2D* DirectX11TexturePreview::GetTexture() const { return Texture.Get(); }
    ID3D11ShaderResourceView* DirectX11TexturePreview::GetShaderResourceView() const { return View.Get(); }
    void DirectX11TexturePreview::ResetSourceCopy() { SourceCopy.Reset(); }
    void DirectX11TexturePreview::SetDimensions(UINT width,UINT height) {
        D3D11_TEXTURE2D_DESC desc {};
        if(Texture) { Texture->GetDesc(&desc); if(desc.Width==width && desc.Height==height) { return; } }
        desc={}; desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
        HRESULT result=Device->CreateTexture2D(&desc,nullptr,&texture);
        if(SUCCEEDED(result)) { result=Device->CreateShaderResourceView(texture.Get(),nullptr,&view); }
        if(SUCCEEDED(result)) { result=Device->CreateRenderTargetView(texture.Get(),nullptr,&target); }
        if(FAILED(result)) { throw std::runtime_error("Cannot allocate engine preview: "+DirectX11HResultFormatter::ToHex(result)); }
        Texture=std::move(texture); View=std::move(view); Target=std::move(target);
    }
}