#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>

namespace helengine::windows {
    /// Creates the pinned ACES 2.0 display LUT embedded in the host; no filesystem lookup or runtime OCIO dependency.
    class DirectX11AcesSdrLut {
    public:
        /// Identifies the private RCDATA payload generated from the official OCIO display/view transform.
        static constexpr WORD ResourceId = 401;
        /// Matches the generated logarithmic lattice; each texel contains four IEEE16 channels.
        static constexpr UINT Edge = 129;
        /// Loads and validates the embedded payload before creating one immutable, filtered GPU volume.
        static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Create(ID3D11Device* device) {
            if(!device) throw std::invalid_argument("ACES SDR LUT requires a device");
            HMODULE module=GetModuleHandleW(nullptr);
            HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(ResourceId),MAKEINTRESOURCEW(10));
            if(!resource || SizeofResource(module,resource)!=Edge*Edge*Edge*8)
                throw std::runtime_error("Missing or invalid embedded ACES 2.0 SDR LUT");
            HGLOBAL loaded=LoadResource(module,resource);
            const void* bytes=loaded ? LockResource(loaded) : nullptr;
            if(!bytes) throw std::runtime_error("Cannot load embedded ACES 2.0 SDR LUT");
            D3D11_TEXTURE3D_DESC description{};
            description.Width=description.Height=description.Depth=Edge;
            description.MipLevels=1; description.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
            description.Usage=D3D11_USAGE_IMMUTABLE; description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{bytes,Edge*8,Edge*Edge*8};
            Microsoft::WRL::ComPtr<ID3D11Texture3D> texture;
            HRESULT result=device->CreateTexture3D(&description,&initial,&texture);
            if(FAILED(result)) throw std::runtime_error("Create ACES SDR LUT failed: "+std::to_string(result));
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            result=device->CreateShaderResourceView(texture.Get(),nullptr,&view);
            if(FAILED(result)) throw std::runtime_error("Create ACES SDR LUT view failed: "+std::to_string(result));
            return view;
        }
    };
}
