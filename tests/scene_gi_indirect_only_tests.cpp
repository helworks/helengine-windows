#include "platform/windows/win32/win32_scene_gi_shader.hpp"
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
/// <summary>Executes the real voxel geometry and composite shaders against owned WARP resources.</summary>
class SceneGiIndirectOnlyTests {
    /// <summary>Owns the headless software device.</summary>
    ComPtr<ID3D11Device> Device;
    /// <summary>Submits synthetic commands without a window or desktop capture.</summary>
    ComPtr<ID3D11DeviceContext> Context;
    /// <summary>Retains the complete production shader source with voxel transport enabled.</summary>
    std::string Source;
    /// <summary>Preserves native failures with their original HRESULT.</summary>
    static void Check(HRESULT result) {if(FAILED(result)) throw std::runtime_error("D3D failure "+std::to_string(result));}
    /// <summary>Rejects disagreement with a separately specified rendering contract.</summary>
    static void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    /// <summary>Compiles production entry points with the selected MSAA ABI.</summary>
    ComPtr<ID3DBlob> Compile(const char* entry,const char* profile,bool multisample=false) {
        D3D_SHADER_MACRO macros[]={{"TOON_SAMPLES",multisample?"4":"1"},{nullptr,nullptr}};ComPtr<ID3DBlob> code,error;
        HRESULT result=D3DCompile(Source.data(),Source.size(),"GiIndirectOnly",macros,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
        if(FAILED(result) && error) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));Check(result);return code;
    }
    /// <summary>Uploads explicit IEEE32 constants to the production binding slot.</summary>
    void Constants(UINT slot,const std::vector<float>& values) {
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=static_cast<UINT>(values.size()*4);buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA data{values.data(),0,0};ComPtr<ID3D11Buffer> resource;Check(Device->CreateBuffer(&buffer,&data,&resource));auto* binding=resource.Get();Context->PSSetConstantBuffers(slot,1,&binding);
    }
    /// <summary>Creates one owned floating-point texture, optionally initialized from row-major data.</summary>
    ComPtr<ID3D11Texture2D> Texture(UINT width,UINT height,UINT flags,const std::vector<float>& values={}) {
        D3D11_TEXTURE2D_DESC description{};description.Width=width;description.Height=height;description.MipLevels=description.ArraySize=1;description.SampleDesc.Count=1;description.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;description.BindFlags=flags;D3D11_SUBRESOURCE_DATA initial{values.data(),width*16,0};ComPtr<ID3D11Texture2D> resource;Check(Device->CreateTexture2D(&description,values.empty()?nullptr:&initial,&resource));return resource;
    }
    /// <summary>Binds a fixture texture without retaining hidden fallback data.</summary>
    void Bind(UINT slot,ID3D11Texture2D* texture) {ComPtr<ID3D11ShaderResourceView> view;Check(Device->CreateShaderResourceView(texture,nullptr,&view));auto* binding=view.Get();Context->PSSetShaderResources(slot,1,&binding);}
    /// <summary>Runs production material shading or raw composition with adversarial nonzero direct/effect inputs.</summary>
    std::array<float,4> Draw(bool enabled,bool zero=false,bool composite=false,bool multisample=false,float alpha=1) {
        Context->ClearState();Constants(9,{0,0,0,1,1,1,1,enabled?1.f:0.f,4,4,4,0});
        std::vector<float> lights(36,0);lights[0]=lights[1]=lights[2]=1;lights[4]=1;lights[8]=lights[9]=lights[10]=100;lights[14]=1;Constants(1,lights);
        Constants(3,{.1f,.3f,.8f,alpha});Constants(7,{5,6,7,10});
        std::vector<float> toon(44,0);toon[0]=toon[1]=8;toon[2]=4;toon[4]=toon[5]=1;toon[8]=toon[9]=1;toon[16]=1;toon[24]=toon[25]=1;toon[28]=1;toon[29]=16;toon[32]=toon[34]=toon[35]=1;toon[36]=toon[37]=toon[39]=1;Constants(8,toon);
        std::vector<float> atlas(16*24*4);for(std::size_t pixel=0;pixel<atlas.size()/4;++pixel) {atlas[pixel*4]=zero?0:3;atlas[pixel*4+1]=zero?0:2;atlas[pixel*4+2]=zero?0:1;atlas[pixel*4+3]=1;}
        auto field=Texture(16,24,D3D11_BIND_SHADER_RESOURCE,atlas);Bind(17,field.Get());auto diffuse=Texture(1,1,D3D11_BIND_SHADER_RESOURCE,{1,1,1,1});Bind(0,diffuse.Get());
        D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampling.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;Check(Device->CreateSamplerState(&sampling,&sampler));auto* samplingBinding=sampler.Get();Context->PSSetSamplers(0,1,&samplingBinding);
        auto color=Texture(1,1,D3D11_BIND_SHADER_RESOURCE,{3,2,1,.75f});Bind(8,color.Get());
        auto output=Texture(1,1,D3D11_BIND_RENDER_TARGET);ComPtr<ID3D11RenderTargetView> target;Check(Device->CreateRenderTargetView(output.Get(),nullptr,&target));auto* targetBinding=target.Get();Context->OMSetRenderTargets(1,&targetBinding,nullptr);const float clear[4]={.07f,.07f,.07f,1};Context->ClearRenderTargetView(target.Get(),clear);
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> state;Check(Device->CreateRasterizerState(&raster,&state));Context->RSSetState(state.Get());D3D11_VIEWPORT viewport{0,0,1,1,0,1};Context->RSSetViewports(1,&viewport);Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto vsCode=Compile("FixtureVS","vs_5_0",multisample),psCode=Compile(composite?"CompositePS":"FixturePS","ps_5_0",multisample);ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;Check(Device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(Device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));Context->VSSetShader(vs.Get(),nullptr,0);Context->PSSetShader(ps.Get(),nullptr,0);Context->Draw(3,0);
        D3D11_TEXTURE2D_DESC description{};output->GetDesc(&description);description.BindFlags=0;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> readback;Check(Device->CreateTexture2D(&description,nullptr,&readback));Context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));std::array<float,4> result;std::memcpy(result.data(),mapped.pData,16);Context->Unmap(readback.Get(),0);return result;
    }
public:
    /// <summary>Creates the headless device and appends a wrapper that calls actual production material shading.</summary>
    SceneGiIndirectOnlyTests() {
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&Device,nullptr,&Context));
        Source=std::string(helengine::windows::PearlSurfaceShader)+"\n#define HELENGINE_VOXEL_GI 1\n"+helengine::windows::SceneGiShader+helengine::windows::CameraToonGeometryShader+helengine::windows::CameraToonCompositeShader;
        Source+=R"(
float4 FixtureVS(uint id:SV_VertexID):SV_Position {return float4(id==1?3:-1,id==2?3:-1,0,1);}
float4 FixturePS():SV_Target {MeshOutput input;input.Position=0;input.WorldPosition=float3(2,2,2);input.Normal=float3(0,-1,0);input.UV=0.5;return ShadeGeometry(input,true).Color;})";
    }
    /// <summary>Checks colored transport, white receiver response, no fake light, raw composition, alpha, and normal-mode isolation.</summary>
    void Run() {
        const float inversePi=1.f/3.14159265358979323846f;auto p=Draw(true);Require(std::abs(p[0]-3*inversePi)<.002f && std::abs(p[1]-2*inversePi)<.002f && std::abs(p[2]-inversePi)<.002f,"Indirect-only keeps RGB irradiance/pi without receiver albedo, direct sun, pearl or emission");
        p=Draw(true,true);Require(p[0]==0 && p[1]==0 && p[2]==0,"Zero transport remains black despite direct light and emission");
        p=Draw(true,false,false,false,.5f);Require(std::abs(p[0]-1.5f*inversePi)<.002f && p[3]==.5f,"Indirect-only preserves material alpha and premultiplication");
        p=Draw(true,false,false,false,0);Require(std::abs(p[0]-.07f)<.001f,"Alpha cutouts retain the existing geometry silhouette");
        for(bool multisample:{false,true}) {p=Draw(true,false,true,multisample);Require(p[0]==3 && p[1]==2 && p[2]==1 && p[3]==.75f,"Raw composition bypasses tone mapping, exposure, AO, outlines and bloom for both sample ABIs");}
        p=Draw(false,true);Require(p[0]>1 && p[1]>1 && p[2]>1,"Disabled mode retains ordinary material/direct/emission shading");
    }
};
/// <summary>Reports offscreen contract failures without swallowing shader diagnostics.</summary>
int main() {try {SceneGiIndirectOnlyTests tests;tests.Run();std::cout<<"PASS voxel indirect-only: RGB irradiance/pi, white receiver, zero/direct isolation, alpha, raw1x/4x composition, disabled behavior\n";return 0;} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
