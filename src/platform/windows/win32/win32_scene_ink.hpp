#pragma once
#include "platform/windows/win32/win32_scene_ink_configuration.hpp"
#include "platform/windows/win32/win32_scene_ink_gpu_state.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <memory>
#include <string>
namespace helengine::windows {
    /// <summary>Rasterizes one bounded cubemap face per interval and publishes only complete, mip-filtered six-face cycles.</summary>
    class Win32SceneInk {
        /// <summary>Retains the explicit current palette and authored room bounds.</summary>
        Win32SceneInkConfiguration Configuration;
        /// <summary>Freezes artistic palette and capture geometry for the six private faces of the current cycle.</summary>
        Win32SceneInkConfiguration CycleConfiguration;
        /// <summary>Keeps parallax metadata paired with the last complete public cube while a new origin is being captured.</summary>
        Win32SceneInkConfiguration PublishedConfiguration;
        /// <summary>Retains two separate cube textures so partially refreshed faces never reach receivers.</summary>
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,2> Cubes;
        /// <summary>Exposes complete mip chains for the published cube and GenerateMips.</summary>
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,2> Views;
        /// <summary>Addresses the mip-zero face targets of both capture buffers.</summary>
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,6>,2> Targets;
        /// <summary>Owns nearest-surface capture depth shared sequentially across all six directions.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> Depth;
        /// <summary>Enables opaque depth-tested capture independently of output alpha contracts.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> DepthState;
        /// <summary>Disables inherited blend equations during the initial face clear and opaque geometry.</summary>
        Microsoft::WRL::ComPtr<ID3D11BlendState> Blend;
        /// <summary>Uses two-sided capture with no inherited UI scissor.</summary>
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
        /// <summary>Filters stylized reflections trilinearly without an offline GGX convolution.</summary>
        Microsoft::WRL::ComPtr<ID3D11SamplerState> Sampler;
        /// <summary>Shares box projection metadata with the immutable probe shader binding.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> ProbeParameters;
        /// <summary>Uploads the explicit ink mode, source-feedback flag and artistic palette.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> InkParameters;
        /// <summary>Restores all native capture bindings on completion or cancellation.</summary>
        std::unique_ptr<Win32SceneInkGpuState> Previous;
        /// <summary>Stores the fixed production face edge; headless fixtures may request a smaller power of two.</summary>
        UINT Edge;
        /// <summary>Stores the complete mip-chain length.</summary>
        UINT MipCount=1;
        /// <summary>Identifies the currently private write cube.</summary>
        unsigned WriteIndex=0;
        /// <summary>Identifies the most recent complete public cube.</summary>
        unsigned PublishedIndex=1;
        /// <summary>Tracks the next capture direction in standard Direct3D cube face order.</summary>
        unsigned NextFace=0;
        /// <summary>Counts successfully rasterized faces for host budget reports.</summary>
        uint64_t CapturedFaces=0;
        /// <summary>Counts complete atomic publications rather than individual face writes.</summary>
        uint64_t PublishedCycles=0;
        /// <summary>Limits work using authoritative scene time across both presentation windows.</summary>
        double LastFaceSeconds=-1;
        /// <summary>Preserves the original D3D failure instead of publishing incomplete resources.</summary>
        static void Check(HRESULT result) {if(FAILED(result)) throw std::runtime_error("Ink reflection GPU operation failed: "+std::to_string(result));}
    public:
        /// <summary>Creates only raster resources, with production captures fixed to 128 squared pixels per face.</summary>
        Win32SceneInk(ID3D11Device* device,const Win32SceneInkConfiguration& configuration,UINT edge=128):Configuration(configuration),Edge(edge) {
            Configuration.Validate();if(!device || edge<4 || edge>128 || (edge&(edge-1))) throw std::invalid_argument("Ink requires a GPU and a power-of-two face edge between 4 and 128.");
            for(UINT size=edge;size>1;size>>=1) ++MipCount;
            D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=edge;texture.MipLevels=MipCount;texture.ArraySize=6;texture.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;texture.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE|D3D11_RESOURCE_MISC_GENERATE_MIPS;
            for(unsigned cube=0;cube<2;++cube) {
                Check(device->CreateTexture2D(&texture,nullptr,&Cubes[cube]));Check(device->CreateShaderResourceView(Cubes[cube].Get(),nullptr,&Views[cube]));
                for(unsigned face=0;face<6;++face) {D3D11_RENDER_TARGET_VIEW_DESC target{};target.Format=texture.Format;target.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;target.Texture2DArray.ArraySize=1;target.Texture2DArray.FirstArraySlice=face;Check(device->CreateRenderTargetView(Cubes[cube].Get(),&target,&Targets[cube][face]));}
            }
            texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT_D32_FLOAT;texture.BindFlags=D3D11_BIND_DEPTH_STENCIL;texture.MiscFlags=0;Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;Check(device->CreateTexture2D(&texture,nullptr,&depth));Check(device->CreateDepthStencilView(depth.Get(),nullptr,&Depth));
            D3D11_DEPTH_STENCIL_DESC depthState{};depthState.DepthEnable=TRUE;depthState.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depthState.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;Check(device->CreateDepthStencilState(&depthState,&DepthState));
            D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;Check(device->CreateBlendState(&blend,&Blend));
            D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;Check(device->CreateRasterizerState(&raster,&Rasterizer));
            D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;Check(device->CreateSamplerState(&sampler,&Sampler));
            D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=64;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;Check(device->CreateBuffer(&buffer,nullptr,&ProbeParameters));Check(device->CreateBuffer(&buffer,nullptr,&InkParameters));
        }
        /// <summary>Updates only explicit artist inputs; capture scheduling and already published faces remain intact.</summary>
        void Update(const Win32SceneInkConfiguration& configuration) {
            configuration.Validate();if(Previous) throw std::logic_error("Ink configuration cannot change during face capture.");
            if(configuration.Position!=Configuration.Position || configuration.BoundsMinimum!=Configuration.BoundsMinimum || configuration.BoundsMaximum!=Configuration.BoundsMaximum) NextFace=0;
            Configuration=configuration;
        }
        /// <summary>Exposes validated room and palette inputs without transferring their ownership.</summary>
        const Win32SceneInkConfiguration& GetConfiguration() const {return Configuration;}
        /// <summary>Uses a single room origin and artistic palette throughout one private six-face capture cycle.</summary>
        const Win32SceneInkConfiguration& GetCaptureConfiguration() const {return NextFace==0?Configuration:CycleConfiguration;}
        /// <summary>Reports the next standard +X,-X,+Y,-Y,+Z,-Z capture face.</summary>
        unsigned GetNextFace() const {return NextFace;}
        /// <summary>Reports exactly how many face submissions completed successfully.</summary>
        uint64_t GetCapturedFaces() const {return CapturedFaces;}
        /// <summary>Reports exactly how many complete mip-filtered cubes have become visible.</summary>
        uint64_t GetPublishedCycles() const {return PublishedCycles;}
        /// <summary>Allows one face at the configured cadence, without catch-up bursts after a stalled frame.</summary>
        bool IsFaceDue(double seconds) const {return LastFaceSeconds<0 || seconds-LastFaceSeconds>=Configuration.FaceIntervalSeconds;}
        /// <summary>Saves native bindings, clears one private face and prepares an ordinary raster depth pass.</summary>
        void BeginFace(ID3D11DeviceContext* context) {
            if(Previous) throw std::logic_error("Ink face capture is already active.");if(NextFace==0) CycleConfiguration=Configuration;Previous=std::make_unique<Win32SceneInkGpuState>(context);
            ID3D11ShaderResourceView* empty[2]{};context->PSSetShaderResources(24,2,empty);
            auto* target=Targets[WriteIndex][NextFace].Get();context->OMSetRenderTargets(1,&target,Depth.Get());const float clear[]={CycleConfiguration.Sky[0],CycleConfiguration.Sky[1],CycleConfiguration.Sky[2],1};context->ClearRenderTargetView(target,clear);context->ClearDepthStencilView(Depth.Get(),D3D11_CLEAR_DEPTH,1,0);
            const D3D11_VIEWPORT viewport{0,0,static_cast<float>(Edge),static_cast<float>(Edge),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(Rasterizer.Get());context->OMSetDepthStencilState(DepthState.Get(),0);context->OMSetBlendState(Blend.Get(),nullptr,0xffffffff);Bind(context,true);
        }
        /// <summary>Publishes mipmaps only after face six, then restores the original native output state.</summary>
        void EndFace(ID3D11DeviceContext* context,double seconds) {
            if(!Previous) throw std::logic_error("Ink face capture is not active.");context->OMSetRenderTargets(0,nullptr,nullptr);++CapturedFaces;LastFaceSeconds=seconds;
            if(++NextFace==6) {context->GenerateMips(Views[WriteIndex].Get());PublishedIndex=WriteIndex;PublishedConfiguration=CycleConfiguration;WriteIndex=1-WriteIndex;NextFace=0;++PublishedCycles;}
            Previous.reset();
        }
        /// <summary>Restores the caller without advancing a failed face or its publication generation.</summary>
        void CancelFace() {Previous.reset();}
        /// <summary>Binds the palette and only the complete cube; source capture explicitly suppresses reflective feedback.</summary>
        void Bind(ID3D11DeviceContext* context,bool capture) const {
            const auto& palette=capture?CycleConfiguration:Configuration;const auto& box=PublishedCycles?PublishedConfiguration:Configuration;
            const float ink[16]={1,capture?1.f:0.f,0,0,palette.Ambient[0],palette.Ambient[1],palette.Ambient[2],0,palette.ShadowColor[0],palette.ShadowColor[1],palette.ShadowColor[2],0,0,0,0,0};
            const float probe[16]={PublishedCycles && !capture?1.f:0.f,static_cast<float>(MipCount-1),0,0,box.Position[0],box.Position[1],box.Position[2],0,box.BoundsMinimum[0],box.BoundsMinimum[1],box.BoundsMinimum[2],0,box.BoundsMaximum[0],box.BoundsMaximum[1],box.BoundsMaximum[2],0};
            context->UpdateSubresource(InkParameters.Get(),0,nullptr,ink,0,0);context->UpdateSubresource(ProbeParameters.Get(),0,nullptr,probe,0,0);auto* paletteBuffer=InkParameters.Get();auto* boxBuffer=ProbeParameters.Get();auto* sampler=Sampler.Get();ID3D11ShaderResourceView* views[2]={PublishedCycles && !capture?Views[PublishedIndex].Get():nullptr,nullptr};
            context->PSSetConstantBuffers(13,1,&paletteBuffer);context->PSSetConstantBuffers(12,1,&boxBuffer);context->PSSetShaderResources(24,2,views);context->PSSetSamplers(10,1,&sampler);
        }
        /// <summary>Clears all realtime bindings before UI, unrelated cameras or explicit scene teardown.</summary>
        static void Unbind(ID3D11DeviceContext* context) {
            if(!context) return;ID3D11Buffer* empty=nullptr;ID3D11ShaderResourceView* views[2]{};ID3D11SamplerState* sampler=nullptr;context->PSSetConstantBuffers(13,1,&empty);context->PSSetConstantBuffers(12,1,&empty);context->PSSetShaderResources(24,2,views);context->PSSetSamplers(10,1,&sampler);
        }
    };
}
