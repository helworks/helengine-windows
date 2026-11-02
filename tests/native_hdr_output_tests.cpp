#include <d3d11.h>
#include <sstream>
#include <chrono>
#include <thread>
#include "platform/windows/win32/win32_camera_toon_pass.hpp"
#include "aces_sdr_reference.hpp"
#include "platform/windows/directx11/directx11_aces_sdr_lut.hpp"
#include "platform/windows/win32/win32_camera_toon_effects_configuration.hpp"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include "platform/windows/win32/win32_ui_output_shader.hpp"

/// Exercises the production output transform using owned offscreen float pixels, never screen content.
class NativeHdrOutputTests {
    /// Owns the deterministic software graphics device.
    Microsoft::WRL::ComPtr<ID3D11Device> Device;
    /// Submits synthetic shader evaluations without creating a window.
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
    /// Holds the same embedded reference data used by the production composition pass.
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> AcesView;
    /// Holds a clamped linear sampler matching the production LUT filtering.
    Microsoft::WRL::ComPtr<ID3D11SamplerState> AcesSampler;
    /// Stops on a broken graphics contract without masking individual failures.
    static void Check(HRESULT result,const char* operation) {if(FAILED(result)) throw std::runtime_error(operation);}
    /// Checks numerical output with a float tolerance suitable for shader math.
    static void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    /// Compiles the actual composite function and evaluates one explicit display/output configuration.
    std::array<float,4> Evaluate(const std::string& expression,bool hdr,bool ui=false,float sdrExposure=1.0f) {
        std::string source=std::string("#define TOON_SAMPLES 1\n")+helengine::windows::PearlSurfaceShader+helengine::windows::CameraToonGeometryShader+helengine::windows::CameraToonCompositeShader;
        source+="\nfloat4 OutputTestVS(uint id:SV_VertexID):SV_Position {float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],0,1);}\nfloat4 OutputTestPS():SV_Target {return "+expression+";}";
        bool rounded=ui && expression=="Rounded";
        if(ui) source=helengine::windows::NativeUiQuadShader+"\nPSInput OutputTestVS(uint id:SV_VertexID) {float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};PSInput o;o.Position=float4(p[id],0,1);o.UV=float2(.5,.5);o.Color=float4(.5,.5,.5,.5);return o;}";
        if(rounded) source=helengine::windows::NativeUiRoundedRectShader+"\nstruct TestVertex {float4 Position:SV_Position;float2 Local:TEXCOORD0;};TestVertex OutputTestVS(uint id:SV_VertexID) {float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};TestVertex o;o.Position=float4(p[id],0,1);o.Local=float2(1,0);return o;}";
        Microsoft::WRL::ComPtr<ID3DBlob> vs,ps,error;
        HRESULT result=D3DCompile(source.data(),source.size(),"OutputProduction",nullptr,nullptr,"OutputTestVS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&error);
        if(FAILED(result)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Output VS compilation failed");
        result=D3DCompile(source.data(),source.size(),"OutputProduction",nullptr,nullptr,rounded?"PSMain":ui?expression.c_str():"OutputTestPS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&error);
        if(FAILED(result)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Output PS compilation failed");
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
        Check(Device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Create output test vertex shader");
        Check(Device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel),"Create output test pixel shader");
        helengine::windows::CameraToonEffectsConfiguration configuration;
        configuration.SdrExposure=sdrExposure;
        float values[44]{};values[28]=1;values[29]=configuration.OutputExposure(hdr);values[30]=hdr?1.f:0.f;
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(values);buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};data.pSysMem=values;Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
        Check(Device->CreateBuffer(&buffer,&data,&constants),"Create output test constants");auto* params=constants.Get();Context->PSSetConstantBuffers(8,1,&params);
        if(ui) {
            static_assert(D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>13,"Native UI register must be a public D3D11 slot");
            float output[4]={hdr?1.f:0.f,3,0,0};
            std::memcpy(values,output,sizeof(output));Context->UpdateSubresource(constants.Get(),0,nullptr,values,0,0);Context->PSSetConstantBuffers(13,1,&params);
        }
        Microsoft::WRL::ComPtr<ID3D11Buffer> roundedParameters;
        if(rounded) {
            float roundedValues[16]={0,0,2,2,0,0,1,15,1,1,1,1,0,0,0,0};D3D11_BUFFER_DESC description{};description.ByteWidth=sizeof(roundedValues);description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=roundedValues;Check(Device->CreateBuffer(&description,&initial,&roundedParameters),"Create actual rounded-rectangle constants");auto* bound=roundedParameters.Get();Context->PSSetConstantBuffers(0,1,&bound);
        }
        D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=texture.MipLevels=texture.ArraySize=texture.SampleDesc.Count=1;
        texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> atlas;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> atlasView;Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
        if(ui) {
            auto description=texture;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;float texel[4]={1,1,1,.25f};D3D11_SUBRESOURCE_DATA texelData{};texelData.pSysMem=texel;texelData.SysMemPitch=16;
            Check(Device->CreateTexture2D(&description,&texelData,&atlas),"Create owned UI atlas");Check(Device->CreateShaderResourceView(atlas.Get(),nullptr,&atlasView),"Create UI atlas view");
            D3D11_SAMPLER_DESC filter{};filter.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;filter.AddressU=filter.AddressV=filter.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;filter.MaxLOD=D3D11_FLOAT32_MAX;
            Check(Device->CreateSamplerState(&filter,&sampler),"Create UI atlas sampler");auto* resource=atlasView.Get();auto* state=sampler.Get();Context->PSSetShaderResources(0,1,&resource);Context->PSSetSamplers(0,1,&state);
        }
        auto* aces=AcesView.Get(); auto* acesSampler=AcesSampler.Get();
        Context->PSSetShaderResources(29,1,&aces);Context->PSSetSamplers(11,1,&acesSampler);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> target;Check(Device->CreateTexture2D(&texture,nullptr,&target),"Create output float target");
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;Check(Device->CreateRenderTargetView(target.Get(),nullptr,&view),"Create output test view");
        auto* render=view.Get();Context->OMSetRenderTargets(1,&render,nullptr);Context->OMSetBlendState(nullptr,nullptr,~0u);
        D3D11_VIEWPORT viewport{0,0,1,1,0,1};Context->RSSetViewports(1,&viewport);Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->VSSetShader(vertex.Get(),nullptr,0);Context->PSSetShader(pixel.Get(),nullptr,0);Context->Draw(3,0);Context->OMSetRenderTargets(0,nullptr,nullptr);
        texture.BindFlags=0;texture.Usage=D3D11_USAGE_STAGING;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;Check(Device->CreateTexture2D(&texture,nullptr,&staging),"Create output test staging");
        Context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map output test pixel");std::array<float,4> value{};
        std::memcpy(value.data(),mapped.pData,sizeof(value));Context->Unmap(staging.Get(),0);return value;
    }
    /// Exercises real 1x/4x composition and binding restoration with owned premultiplied HDR attachments.
    void CompositeReference(UINT samples,float alpha) {
        Context->ClearState();
        helengine::windows::Win32CameraToonPass pass(Device.Get(),0,0,3);
        helengine::windows::CameraToonEffectsConfiguration effects;effects.ToneMappingEnabled=true;
        pass.ConfigureEffects(effects);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!pass.TryPrepare()) {
            Require(std::chrono::steady_clock::now()<deadline,"Production ACES composite preparation timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        D3D11_VIEWPORT viewport{0,0,4,4,0,1};
        pass.Begin(Context.Get(),viewport,samples);
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> scene;
        Context->OMGetRenderTargets(1,&scene,nullptr);
        const float linear[4]={.18f*alpha,.18f*alpha,.18f*alpha,alpha};
        Context->ClearRenderTargetView(scene.Get(),linear);
        D3D11_TEXTURE2D_DESC description{};description.Width=description.Height=4;
        description.MipLevels=description.ArraySize=description.SampleDesc.Count=1;
        description.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;description.BindFlags=D3D11_BIND_RENDER_TARGET;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
        Check(Device->CreateTexture2D(&description,nullptr,&target),"Create production SDR fixture target");
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
        Check(Device->CreateRenderTargetView(target.Get(),nullptr,&view),"Create production SDR fixture view");
        const float clear[4]{};Context->ClearRenderTargetView(view.Get(),clear);
        auto* sentinel=AcesView.Get();auto* sentinelSampler=AcesSampler.Get();
        Context->PSSetShaderResources(29,1,&sentinel);Context->PSSetSamplers(11,1,&sentinelSampler);
        pass.Composite(Context.Get(),view.Get(),nullptr,viewport);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> restored;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> restoredSampler;
        Context->PSGetShaderResources(29,1,&restored);Context->PSGetSamplers(11,1,&restoredSampler);
        Require(restored.Get()==sentinel && restoredSampler.Get()==sentinelSampler,"Composite leaked ACES resource bindings");
        description.Usage=D3D11_USAGE_STAGING;description.BindFlags=0;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        Check(Device->CreateTexture2D(&description,nullptr,&staging),"Create production SDR fixture readback");
        Context->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read production SDR fixture");
        std::array<float,4> actual{};std::memcpy(actual.data(),mapped.pData,sizeof(actual));Context->Unmap(staging.Get(),0);
        Require(std::abs(actual[0]-.3491876f*alpha)<.002f,"Production composite missed ACES, encoded twice, or transformed premultiplied RGB");
        Require(std::abs(actual[3]-alpha)<.001f,"Production ACES composite changed coverage");
    }

public:
    /// Creates only WARP and application-owned offscreen resources.
    NativeHdrOutputTests() {Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&Device,nullptr,&Context),"Create output WARP device");
        AcesView=helengine::windows::DirectX11AcesSdrLut::Create(Device.Get());
        D3D11_SAMPLER_DESC description{};description.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        description.AddressU=description.AddressV=description.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        description.MaxLOD=D3D11_FLOAT32_MAX;
        Check(Device->CreateSamplerState(&description,&AcesSampler),"Create ACES test sampler");
    }
    /// Requires unclipped HDR, independent UI reference white, bounded highlights and unchanged SDR transfer.
    void Run() {
        for(UINT samples:{1u,4u}) for(float alpha:{1.f,.37f,0.f}) CompositeReference(samples,alpha);
        std::cout<<"PASS production ACES composition: 1x/4x, opaque/translucent/empty, preserved bindings\n";
        auto scene=Evaluate("float4(DisplayRadiance(float3(2,1,.5)),1)",true);
        Require(scene[0]>1 && scene[0]<5,"HDR scene output was clipped to SDR");
        Require(std::abs(scene[1]/scene[0]-.5f)<.0001f && std::abs(scene[2]/scene[0]-.25f)<.0001f,"HDR shoulder changed scene chroma");
        auto reference=Evaluate("float4(DisplayRadiance(float3(1,.5,.25)),1)",true);
        Require(reference[0]==1 && reference[1]==.5f && reference[2]==.25f,"Scene reference white was multiplied by Windows UI white");
        auto peak=Evaluate("float4(DisplayRadiance(float3(100000,50000,25000)),1)",true);
        Require(peak[0]<=5 && peak[0]>4.99f,"Scene shoulder exceeded the conservative 400-nit limit");
        auto grayScene=Evaluate("float4(DisplayRadiance(float3(.18,.18,.18)),1)",false);
        Require(std::abs(grayScene[0]-.3491876f)<.002f,"SDR must match the official OCIO ACES 2.0 100-nit sRGB reference");
        float maximum=0;
        for(const auto& sample:AcesSdrReference) {
            std::ostringstream expression;expression.precision(9);
            expression<<"float4(DisplayRadiance(float3("<<sample[0]<<","<<sample[1]<<","<<sample[2]<<")),.37)";
            auto actual=Evaluate(expression.str(),false);
            for(int channel=0;channel<3;++channel) {
                float error=std::abs(actual[channel]-sample[channel+3]);maximum=std::max(maximum,error);
                Require(error<.025f,"GPU ACES output exceeds the validated LUT interpolation budget");
            }
            Require(std::abs(actual[3]-.37f)<.000001f,"ACES output modified alpha");
        }
        std::cout<<"PASS official ACES 2.0 CPU references: "<<std::size(AcesSdrReference)<<" GPU cases, max encoded error "<<maximum<<'\n';
        auto calibratedHdr=Evaluate("float4(DisplayRadiance(float3(2,1,.5)),1)",true,false,.12f);
        Require(calibratedHdr==scene,"SDR conversion must not change HDR scene output");
        auto calibratedUi=Evaluate("PSMain",false,true);
        Require(calibratedUi[0]==.5f && calibratedUi[3]==.125f,"SDR conversion must not dim taskbar UI or alter alpha");
        auto ui=Evaluate("PSMain",true,true);
        Require(std::abs(ui[0]-.642123f)<.0001f && ui[3]==.125f,"UI scRGB white/alpha conversion changed coverage or used the scene reference");
        auto gray=Evaluate("FontGrayscalePS",true,true);
        Require(std::abs(gray[0]-.642123f)<.0001f && gray[3]==.125f,"Grayscale glyph coverage was gamma transformed");
        auto clearType=Evaluate("FontClearTypePS",true,true);
        Require(std::abs(clearType[0]-.3210615f)<.0001f && clearType[3]==.125f,"ClearType coverage was transformed before linear UI color");
        auto uiSdr=Evaluate("PSMain",false,true);
        Require(uiSdr[0]==.5f && uiSdr[3]==.125f,"SDR UI transfer or alpha changed");
        auto roundedEdge=Evaluate("Rounded",true,true);
        Require(roundedEdge[0]==3 && std::abs(roundedEdge[3]-.5f)<.0001f,"SDF coverage was gamma transformed instead of remaining in alpha");
        Microsoft::WRL::ComPtr<ID3DBlob> rounded,error;
        const auto& roundedSource=helengine::windows::NativeUiRoundedRectShader;
        Check(D3DCompile(roundedSource.data(),roundedSource.size(),"OutputRoundedRect",nullptr,nullptr,"PSMain","ps_4_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&rounded,&error),"Compile actual rounded-rectangle HDR shader");
        std::cout<<"PASS SDR conversion: official OCIO ACES 2.0 display transform, unchanged HDR/UI\n";
        std::cout<<"PASS native HDR scene: scRGB headroom, 80-nit reference, hue, 400-nit shoulder and separate SDR conversion\n";
        std::cout<<"PASS native UI output: Windows 240-nit white, inverse sRGB, grayscale/ClearType coverage, SDR and actual rounded shader\n";
    }
};
/// Returns the first numerical or graphics failure from the offscreen HDR fixture.
int main() {try{NativeHdrOutputTests test;test.Run();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
