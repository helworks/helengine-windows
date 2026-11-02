#include "platform/windows/win32/win32_scene_ink.hpp"
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <vector>
#include <d3dcompiler.h>
#include <array>
#include <cstring>
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
/// <summary>Exercises the real incremental reflection publication without desktop surfaces.</summary>
class SceneInkTests {
    /// <summary>Stops on a failed contract instead of allowing partial test success.</summary>
    static void Require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
    /// <summary>Compiles the actual ink lighting helpers into a one-pixel WARP material evaluation.</summary>
    static std::array<float,4> Shade(ID3D11Device* device,ID3D11DeviceContext* context,const std::string& expression) {
        const std::string source=std::string(helengine::windows::PearlSurfaceShader)+helengine::windows::CameraToonGeometryShader+"\nfloat4 TestVS(uint id:SV_VertexID):SV_POSITION {float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],0,1); }\nfloat4 TestPS():SV_Target {return "+expression+";}";
        ComPtr<ID3DBlob> vs,ps,error;HRESULT result=D3DCompile(source.data(),source.size(),"InkProduction",nullptr,nullptr,"TestVS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&error);
        if(FAILED(result)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Ink vertex compilation failed");
        result=D3DCompile(source.data(),source.size(),"InkProduction",nullptr,nullptr,"TestPS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&error);
        if(FAILED(result)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Ink pixel compilation failed");
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;Require(SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex)),"Test vertex allocation failed");Require(SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel)),"Test pixel allocation failed");
        D3D11_TEXTURE2D_DESC description{};description.Width=description.Height=description.MipLevels=description.ArraySize=description.SampleDesc.Count=1;description.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;description.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;Require(SUCCEEDED(device->CreateTexture2D(&description,nullptr,&target)),"Test target allocation failed");ComPtr<ID3D11RenderTargetView> view;Require(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&view)),"Test RTV allocation failed");auto* output=view.Get();context->OMSetRenderTargets(1,&output,nullptr);
        context->OMSetBlendState(nullptr,nullptr,0xffffffff);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);context->Draw(3,0);context->OMSetRenderTargets(0,nullptr,nullptr);
        description.BindFlags=0;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;Require(SUCCEEDED(device->CreateTexture2D(&description,nullptr,&staging)),"Test staging allocation failed");context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Test readback failed");std::array<float,4> value{};std::memcpy(value.data(),mapped.pData,16);context->Unmap(staging.Get(),0);return value;
    }
public:
    /// <summary>Restores an output after capture sampled the same resource without input/output binding hazards.</summary>
    static void RestoreOutputHazard(ID3D11Device* device, ID3D11DeviceContext* context) {
        D3D11_TEXTURE2D_DESC description {};
        description.Width=description.Height=2; description.MipLevels=description.ArraySize=description.SampleDesc.Count=1;
        description.Format=DXGI_FORMAT_R8G8B8A8_UNORM; description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11RenderTargetView> target; ComPtr<ID3D11ShaderResourceView> view;
        Require(SUCCEEDED(device->CreateTexture2D(&description,nullptr,&texture)),"Hazard fixture texture failed");
        Require(SUCCEEDED(device->CreateRenderTargetView(texture.Get(),nullptr,&target)),"Hazard fixture output failed");
        Require(SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&view)),"Hazard fixture input failed");
        auto* output=target.Get(); context->OMSetRenderTargets(1,&output,nullptr);
        ComPtr<ID3D11InfoQueue> queue; device->QueryInterface(IID_PPV_ARGS(queue.GetAddressOf()));
        if(queue) queue->ClearStoredMessages();
        {
            helengine::windows::Win32SceneInkGpuState saved(context);
            context->OMSetRenderTargets(0,nullptr,nullptr);
            auto* input=view.Get(); context->PSSetShaderResources(0,1,&input);
        }
        ComPtr<ID3D11RenderTargetView> restored; context->OMGetRenderTargets(1,&restored,nullptr);
        Require(restored.Get()==target.Get(),"Capture failed to restore the original output");
        if(queue) {
            for(UINT64 index=0;index<queue->GetNumStoredMessages();++index) {
                SIZE_T size=0; Require(SUCCEEDED(queue->GetMessage(index,nullptr,&size)),"Validation message sizing failed");
                std::vector<unsigned char> storage(size); auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data());
                Require(SUCCEEDED(queue->GetMessage(index,message,&size)),"Validation message read failed");
                Require(message->Severity>D3D11_MESSAGE_SEVERITY_WARNING,"Capture restoration caused a Direct3D binding hazard");
            }
        }
        context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    /// <summary>Checks rate limiting, atomic complete-cube publication and preserved output state on WARP.</summary>
    static void Run() {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        wchar_t debug[8] {}; const UINT flags=GetEnvironmentVariableW(L"HELENGINE_D3D11_DEBUG",debug,8)==1 && debug[0]==L'1' ? D3D11_CREATE_DEVICE_DEBUG : 0;
        Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP creation failed");
        RestoreOutputHazard(device.Get(),context.Get());
        helengine::windows::Win32SceneInkConfiguration config;
        config.Position={0,0,0};config.BoundsMinimum={-10,-10,-10};config.BoundsMaximum={10,10,10};
        config.Ambient={.1f,.15f,.2f};config.ShadowColor={.15f,.1f,.2f};config.Sky={.3f,.5f,.7f};
        helengine::windows::Win32SceneInk ink(device.Get(),config,8);
        D3D11_VIEWPORT expected{2,3,11,13,0,1};context->RSSetViewports(1,&expected);
        D3D11_SAMPLER_DESC samplerDescription{};samplerDescription.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;samplerDescription.AddressU=samplerDescription.AddressV=samplerDescription.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;samplerDescription.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sentinelSampler;Require(SUCCEEDED(device->CreateSamplerState(&samplerDescription,&sentinelSampler)),"Sentinel sampler allocation failed");auto* sentinel=sentinelSampler.Get();context->PSSetSamplers(15,1,&sentinel);
        ink.BeginFace(context.Get());ID3D11SamplerState* emptySampler=nullptr;context->PSSetSamplers(15,1,&emptySampler);ink.CancelFace();ComPtr<ID3D11SamplerState> restoredSampler;context->PSGetSamplers(15,1,&restoredSampler);
        Require(restoredSampler.Get()==sentinel,"Capture failed to restore authored sampler slot 15");
        Require(ink.GetNextFace()==0 && ink.GetCapturedFaces()==0,"Cancelled face advanced private publication state");
        Require(ink.GetPublishedCycles()==0,"A partial cube was published before capture");
        for(unsigned face=0;face<6;++face) {
            const double time=face*.151;
            Require(ink.IsFaceDue(time),"Due face rejected");
            Require(ink.GetNextFace()==face,"Cubemap face order changed");
            ink.BeginFace(context.Get());ink.EndFace(context.Get(),time);
            Require(!ink.IsFaceDue(time+.01),"More than one face allowed inside interval");
            Require(ink.GetPublishedCycles()==(face==5?1:0),"Partial cube leaked into the published reflection");
            D3D11_VIEWPORT actual{};UINT count=1;context->RSGetViewports(&count,&actual);
            Require(actual.TopLeftX==expected.TopLeftX && actual.Width==expected.Width,"Capture did not restore viewport");
        }
        Require(ink.GetCapturedFaces()==6,"Capture budget counters lost faces");
        ink.Bind(context.Get(),false);
        ComPtr<ID3D11ShaderResourceView> first;context->PSGetShaderResources(24,1,&first);
        Require(first.Get()!=nullptr,"Complete cube not bound");
        ink.BeginFace(context.Get());ink.EndFace(context.Get(),1.0);
        ink.Bind(context.Get(),false);ComPtr<ID3D11ShaderResourceView> second;context->PSGetShaderResources(24,1,&second);
        Require(first.Get()==second.Get(),"New partial cycle replaced complete cube");
        auto changed=config;changed.Position={1,0,0};ink.Update(changed);
        Require(ink.GetNextFace()==0,"A new capture origin retained faces from a different room configuration");
        changed.Ambient={.2f,.25f,.3f};ink.BeginFace(context.Get());ink.EndFace(context.Get(),1.16);ink.Update(changed);
        Require(ink.GetNextFace()==1,"Palette updates starved the incremental face cycle");
        ink.Bind(context.Get(),false);
        auto oldPixels=Shade(device.Get(),context.Get(),"float4(ReflectionProbeCube.SampleLevel(ReflectionProbeSampler,float3(1,0,0),3).rgb,1)");
        Require(std::abs(oldPixels[0]-.3f)<.001f && std::abs(oldPixels[1]-.5f)<.001f && std::abs(oldPixels[2]-.7f)<.001f,"Published mip pixels changed during an incomplete new-origin cycle");
        ComPtr<ID3D11Buffer> metadata;context->PSGetConstantBuffers(12,1,&metadata);D3D11_BUFFER_DESC metadataDescription{};metadata->GetDesc(&metadataDescription);metadataDescription.Usage=D3D11_USAGE_STAGING;metadataDescription.BindFlags=0;metadataDescription.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> readback;Require(SUCCEEDED(device->CreateBuffer(&metadataDescription,nullptr,&readback)),"Metadata readback allocation failed");context->CopyResource(readback.Get(),metadata.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"Metadata map failed");const float oldOrigin=static_cast<const float*>(mapped.pData)[4];context->Unmap(readback.Get(),0);
        Require(oldOrigin==0,"New-origin metadata was paired with old-origin cubemap pixels");
        for(unsigned face=1;face<6;++face) {ink.BeginFace(context.Get());ink.EndFace(context.Get(),1.16+face*.151);}
        ink.Bind(context.Get(),false);context->CopyResource(readback.Get(),metadata.Get());Require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"Published metadata map failed");const float newOrigin=static_cast<const float*>(mapped.pData)[4];context->Unmap(readback.Get(),0);
        Require(newOrigin==1 && ink.GetPublishedCycles()==2,"Complete new-origin cube did not publish its matching box metadata");
        auto bands=Shade(device.Get(),context.Get(),"float4(InkBand(.1),InkBand(.5),InkBand(.9),1)");
        Require(bands[0]==0 && bands[1]==.5f && bands[2]==1,"Ink diffuse did not produce exactly three authored bands");
        auto shadow=Shade(device.Get(),context.Get(),"float4(InkLightColor(float3(1,1,1),1,0),1)");
        Require(std::abs(shadow[0]-.15f)<.0001f && std::abs(shadow[1]-.1f)<.0001f && std::abs(shadow[2]-.2f)<.0001f,"Occluded ink light discarded its colored shadow palette");
        auto brightShadow=Shade(device.Get(),context.Get(),"float4(InkLightColor(float3(12,12,12),1,0),1)");
        for(unsigned channel=0;channel<3;++channel) Require(std::abs(brightShadow[channel]-shadow[channel])<.0001f,"HDR direct radiance amplified the artistic shadow fill");
        auto absentLight=Shade(device.Get(),context.Get(),"float4(InkLightColor(float3(0,0,0),1,0),1)");
        Require(absentLight[0]==0 && absentLight[1]==0 && absentLight[2]==0,"An absent light created artificial shadow illumination");
        auto tintedShadow=Shade(device.Get(),context.Get(),"float4(InkLightColor(float3(12,6,0),1,0),1)");
        Require(std::abs(tintedShadow[0]-.15f)<.0001f && std::abs(tintedShadow[1]-.05f)<.0001f && tintedShadow[2]==0,"Bounded shadow fill discarded the active light chroma");
        auto dimShadow=Shade(device.Get(),context.Get(),"float4(InkLightColor(float3(.5,.5,.5),1,0),1)");
        Require(std::abs(dimShadow[0]-.075f)<.0001f,"A dim light failed to fade the artistic shadow fill");
        auto brightLit=Shade(device.Get(),context.Get(),"float4(InkLightColor(float3(12,6,0),1,1),1)");
        Require(brightLit[0]==12 && brightLit[1]==6 && brightLit[2]==0,"Bounded shadow fill clamped fully lit scene radiance");
        auto highlight=Shade(device.Get(),context.Get(),"float4(InkMetallicHighlight(float3(0,0,1),float3(0,0,1),float3(.8,.5,.2),0,float4(1,1,1,0),float4(0,0,-1,0),1),1)");
        Require(highlight[0]>.1f && highlight[0]<=.7f && highlight[1]<highlight[0] && highlight[2]<highlight[1],"Ink metal highlight lost its bounded conductor tint");
        auto rough=Shade(device.Get(),context.Get(),"float4(InkMetallicHighlight(float3(0,0,1),float3(0,0,1),float3(1,1,1),1,float4(1,1,1,0),float4(0,0,-1,0),1),1)");
        Require(rough[0]<=.7f && rough[0]>.1f,"Rough ink metal produced unbounded GGX energy");
        std::cout<<"PASS ink shader: exact three bands, colored hard shadow and bounded tinted metal highlights\n";
        std::cout<<"PASS ink cube: rate limit, six-face atomic publication, double buffering, output state restoration\n";
    }
};
/// <summary>Returns the first headless resource or scheduling failure to the caller.</summary>
int main() {try{SceneInkTests::Run();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}



