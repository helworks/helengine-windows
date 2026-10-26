#include "directx11_preview_processor.hpp"
#include "directx11_context_scope.hpp"
#include "directx11_hresult_formatter.hpp"
#include <algorithm>
#include <cstring>
#include <d3dcompiler.h>
#include <stdexcept>
#include <string>

namespace helengine::windows {
    DirectX11PreviewProcessor::DirectX11PreviewProcessor(ID3D11Device* device,ID3D11DeviceContext* context) : Device(device) {
        if(device==nullptr || context==nullptr || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) {
            throw std::invalid_argument("An engine preview processor requires a device and its immediate context.");
        }
        Microsoft::WRL::ComPtr<ID3D11Device> contextDevice; context->GetDevice(&contextDevice);
        if(contextDevice.Get()!=device) { throw std::invalid_argument("Preview context and device must match."); }
        Check(context->QueryInterface(IID_PPV_ARGS(&Context)),"Access preview context state");
        Microsoft::WRL::ComPtr<ID3D11Device1> extended; Check(Device.As(&extended),"Access preview device state");
        auto level=Device->GetFeatureLevel();
        UINT flags=(Device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)!=0 ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
        Check(extended->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device1),nullptr,&State),"Create isolated preview state");
        const char* shader=R"(
            cbuffer Crop : register(b0) { float2 Extent; float2 Minimum; float2 Maximum; float2 Padding; };
            struct Fragment { float4 Position : SV_Position; float2 UV : TEXCOORD0; };
            Fragment VS(uint id : SV_VertexID) {
                float2 uv[3]={float2(0,0),float2(0,2),float2(2,0)};
                Fragment output; output.UV=uv[id]; output.Position=float4(output.UV.x*2-1,1-output.UV.y*2,0,1); return output;
            }
            Texture2D Image : register(t0); SamplerState LinearClamp : register(s0);
            float4 PS(Fragment input) : SV_Target { return Image.Sample(LinearClamp,clamp(input.UV*Extent,Minimum,Maximum)); }
        )";
        ShaderCompilation=std::make_unique<DirectX11ShaderCompilation>(std::vector<DirectX11ShaderRequest>{
            {shader,"EnginePreview","VS","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,{}},
            {shader,"EnginePreview","PS","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,{}}
        });
    }
    bool DirectX11PreviewProcessor::TryPrepare() {
        if(PipelineFailure) std::rethrow_exception(PipelineFailure);
        if(PipelinePrepared) return true;
        try {
            DirectX11ShaderCompilation::Bytecodes bytecodes;
            if(!ShaderCompilation->TryGetBytecodes(bytecodes)) return false;
            const auto& vertex=bytecodes[0]; const auto& pixel=bytecodes[1];
            Check(Device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&VertexShader),"Create preview vertex shader");
            Check(Device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&PixelShader),"Create preview pixel shader");
            D3D11_BUFFER_DESC buffer {}; buffer.ByteWidth=32; buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            Check(Device->CreateBuffer(&buffer,nullptr,&Constants),"Create preview crop constants");
            D3D11_SAMPLER_DESC sampler {}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD=D3D11_FLOAT32_MAX;
            Check(Device->CreateSamplerState(&sampler,&Sampler),"Create preview sampler");
            D3D11_RASTERIZER_DESC raster {}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
            Check(Device->CreateRasterizerState(&raster,&Rasterizer),"Create preview rasterizer");
            PipelinePrepared=true;
            ShaderCompilation.reset();
            return true;
        } catch(...) { PipelineFailure=std::current_exception(); throw; }
    }
    D3D11_TEXTURE2D_DESC DirectX11PreviewProcessor::Validate(DirectX11TexturePreview& preview,ID3D11Texture2D* source,UINT width,UINT height) const {
        if(source==nullptr || preview.Device.Get()!=Device.Get() || source==preview.Texture.Get()) {
            throw std::invalid_argument("Preview source and destination must be distinct resources on the processor's device.");
        }
        Microsoft::WRL::ComPtr<ID3D11Device> sourceDevice; source->GetDevice(&sourceDevice);
        D3D11_TEXTURE2D_DESC desc {}; source->GetDesc(&desc);
        if(sourceDevice.Get()!=Device.Get() || width==0 || height==0 || width>desc.Width || height>desc.Height
            || desc.ArraySize!=1 || desc.MipLevels!=1 || desc.SampleDesc.Count!=1
            || (desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM)) {
            throw std::invalid_argument("A preview source must be single-sample BGRA8/RGBA8 with valid content dimensions on the same device.");
        }
        return desc;
    }
    bool DirectX11PreviewProcessor::Update(DirectX11TexturePreview& preview,ID3D11Texture2D* source,UINT contentWidth,UINT contentHeight) {
        auto desc=Validate(preview,source,contentWidth,contentHeight);
        if(!TryPrepare()) return false;
        double aspect=static_cast<double>(contentWidth)/contentHeight;
        UINT width=(std::min)(preview.MaximumWidth,contentWidth);
        UINT height=(std::max)(1u,static_cast<UINT>(width/aspect));
        if(height>preview.MaximumHeight) { height=preview.MaximumHeight; width=(std::max)(1u,static_cast<UINT>(height*aspect)); }
        preview.SetDimensions(width,height);
        DirectX11ContextScope scope(Context.Get(),State.Get());
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView;
        if((desc.BindFlags&D3D11_BIND_SHADER_RESOURCE)==0) {
            D3D11_TEXTURE2D_DESC copy {};
            if(preview.SourceCopy) { preview.SourceCopy->GetDesc(&copy); }
            if(!preview.SourceCopy || copy.Width!=desc.Width || copy.Height!=desc.Height || copy.Format!=desc.Format) {
                auto copyDesc=desc; copyDesc.Usage=D3D11_USAGE_DEFAULT; copyDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                copyDesc.CPUAccessFlags=copyDesc.MiscFlags=0;
                Microsoft::WRL::ComPtr<ID3D11Texture2D> replacement;
                Check(Device->CreateTexture2D(&copyDesc,nullptr,&replacement),"Create shader-readable external texture copy");
                preview.SourceCopy=std::move(replacement);
            }
            Context->CopyResource(preview.SourceCopy.Get(),source); source=preview.SourceCopy.Get();
        } else { preview.ResetSourceCopy(); }
        Check(Device->CreateShaderResourceView(source,nullptr,&sourceView),"Sample external preview texture");
        const float crop[8] {static_cast<float>(contentWidth)/desc.Width,static_cast<float>(contentHeight)/desc.Height,
            0.5f/desc.Width,0.5f/desc.Height,(contentWidth-0.5f)/desc.Width,(contentHeight-0.5f)/desc.Height,0,0};
        Context->UpdateSubresource(Constants.Get(),0,nullptr,crop,0,0);
        auto* target=preview.Target.Get(); Context->OMSetRenderTargets(1,&target,nullptr);
        Context->OMSetBlendState(nullptr,nullptr,~0u); Context->OMSetDepthStencilState(nullptr,0);
        D3D11_VIEWPORT viewport {0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        Context->RSSetViewports(1,&viewport); Context->RSSetState(Rasterizer.Get());
        Context->IASetInputLayout(nullptr); Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->VSSetShader(VertexShader.Get(),nullptr,0); Context->PSSetShader(PixelShader.Get(),nullptr,0);
        auto* settings=Constants.Get(); auto* sample=Sampler.Get(); auto* image=sourceView.Get();
        Context->PSSetConstantBuffers(0,1,&settings); Context->PSSetSamplers(0,1,&sample); Context->PSSetShaderResources(0,1,&image);
        Context->Draw(3,0);
        ID3D11ShaderResourceView* unbound=nullptr; Context->PSSetShaderResources(0,1,&unbound);
        Context->OMSetRenderTargets(0,nullptr,nullptr);
        Context->Flush();
        return true;
    }
    void DirectX11PreviewProcessor::Check(HRESULT result,const char* operation) {
        if(FAILED(result)) { throw std::runtime_error(std::string(operation)+": "+DirectX11HResultFormatter::ToHex(result)); }
    }
}