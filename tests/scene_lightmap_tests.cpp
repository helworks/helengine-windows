#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include "platform/windows/win32/win32_scene_lightmap.hpp"
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
class SceneLightmapTests {
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
        HRESULT result=D3DCompile(Source.data(),Source.size(),"LightmapConsumer",macros,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
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
    std::array<float,4> Draw(bool enabled,bool zero=false,bool composite=false,bool multisample=false,float alpha=1,float uv=.25f) {
        Context->ClearState();Constants(11,{enabled?1.f:0.f,0,0,0});Constants(5,{uv,0,0,0});
        std::vector<float> lights(36,0);lights[0]=lights[1]=lights[2]=1;lights[4]=1;lights[8]=lights[9]=lights[10]=100;lights[14]=1;Constants(1,lights);
        Constants(3,{.1f,.3f,.8f,alpha});Constants(7,{0,0,0,0});
        std::vector<float> toon(44,0);toon[0]=toon[1]=8;toon[2]=4;toon[4]=toon[5]=1;toon[8]=toon[9]=0;toon[16]=0;toon[24]=toon[25]=0;toon[28]=1;toon[29]=16;toon[32]=toon[34]=toon[35]=1;toon[36]=toon[37]=toon[39]=1;Constants(8,toon);
        std::vector<float> atlas={zero?0.f:3.f,zero?0.f:2.f,zero?0.f:1.f,1,9,8,7,1};
        auto field=Texture(2,1,D3D11_BIND_SHADER_RESOURCE,atlas);Bind(23,field.Get());auto diffuse=Texture(1,1,D3D11_BIND_SHADER_RESOURCE,{1,1,1,1});Bind(0,diffuse.Get());
        D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampling.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;Check(Device->CreateSamplerState(&sampling,&sampler));auto* samplingBinding=sampler.Get();Context->PSSetSamplers(0,1,&samplingBinding);Context->PSSetSamplers(9,1,&samplingBinding);
        auto color=Texture(1,1,D3D11_BIND_SHADER_RESOURCE,{3,2,1,.75f});Bind(8,color.Get());
        auto output=Texture(1,1,D3D11_BIND_RENDER_TARGET);ComPtr<ID3D11RenderTargetView> target;Check(Device->CreateRenderTargetView(output.Get(),nullptr,&target));auto* targetBinding=target.Get();Context->OMSetRenderTargets(1,&targetBinding,nullptr);const float clear[4]={.07f,.07f,.07f,1};Context->ClearRenderTargetView(target.Get(),clear);
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> state;Check(Device->CreateRasterizerState(&raster,&state));Context->RSSetState(state.Get());D3D11_VIEWPORT viewport{0,0,1,1,0,1};Context->RSSetViewports(1,&viewport);Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto vsCode=Compile("FixtureVS","vs_5_0",multisample),psCode=Compile(composite?"CompositePS":enabled?"FixtureBakedPS":"FixturePS","ps_5_0",multisample);ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;Check(Device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(Device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));Context->VSSetShader(vs.Get(),nullptr,0);Context->PSSetShader(ps.Get(),nullptr,0);Context->Draw(3,0);
        D3D11_TEXTURE2D_DESC description{};output->GetDesc(&description);description.BindFlags=0;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> readback;Check(Device->CreateTexture2D(&description,nullptr,&readback));Context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));std::array<float,4> result;std::memcpy(result.data(),mapped.pData,16);Context->Unmap(readback.Get(),0);return result;
    }
public:
    /// <summary>Creates the headless device and appends a wrapper that calls actual production material shading.</summary>
    SceneLightmapTests() {
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&Device,nullptr,&Context));
        Source=std::string("#define HELENGINE_LIGHTMAP 1\n")+helengine::windows::PearlSurfaceShader+helengine::windows::CameraToonGeometryShader+helengine::windows::CameraToonCompositeShader;
        Source+=R"(
cbuffer FixtureUv : register(b5) { float4 FixtureCoordinates; };
float4 FixtureVS(uint id:SV_VertexID):SV_Position {return float4(id==1?3:-1,id==2?3:-1,0,1);}
float4 FixturePS():SV_Target {MeshOutput input;input.Position=0;input.WorldPosition=float3(2,2,2);input.Normal=float3(0,-1,0);input.UV=0.5;input.LightmapUV=float2(FixtureCoordinates.x,.5);return ShadeGeometry(input,true).Color;}
float4 FixtureBakedPS():SV_Target {MeshOutput input=(MeshOutput)0;input.Normal=float3(0,-1,0);input.UV=.5;input.LightmapUV=float2(FixtureCoordinates.x,.5);return ShadeGeometryCore(input,true,true).Color;})";
    }
    /// <summary>Checks colored transport, white receiver response, no fake light, raw composition, alpha, and normal-mode isolation.</summary>
    void Run() {
        const float inversePi=1.f/3.14159265358979323846f;auto p=Draw(true);
        Require(std::abs(p[0]-.1f*3*inversePi)<.002f && std::abs(p[1]-.3f*2*inversePi)<.002f && std::abs(p[2]-.8f*inversePi)<.002f,"Baked RGB irradiance applies receiver albedo/pi exactly once and replaces adversarial ambient and sun");
        p=Draw(true,true);Require(p[0]==0 && p[1]==0 && p[2]==0,"Zero irradiance stays black despite ambient and direct sun");
        p=Draw(true,false,false,false,.5f);Require(std::abs(p[0]-.1f*1.5f*inversePi)<.002f && p[3]==.5f,"Baked diffuse preserves material alpha and premultiplication");
        p=Draw(true,false,false,false,0);Require(std::abs(p[0]-.07f)<.001f,"Alpha cutouts preserve silhouette");
        p=Draw(true,false,false,false,1,.75f);Require(std::abs(p[0]-.1f*9*inversePi)<.002f && std::abs(p[1]-.3f*8*inversePi)<.002f,"Independent UV1 bindings sample separate instance charts");
        p=Draw(false,true);Require(p[0]>.09f && p[1]>.29f && p[2]>.79f,"Disabled lightmap retains ordinary authored diffuse and ambient");
        // Compile both production geometry ABIs and ensure explicit bindings can be completely released for UI.
        Compile("GeometryVS","vs_5_0");Compile("LightmapGeometryVS","vs_5_0");Compile("LightmapGeometryPS","ps_5_0",true);Compile("LightmapPearlGeometryPS","ps_5_0");
        std::vector<float> corners={-1,-1,0, 0,0,1, .2f,.3f, 1,-1,0, 0,0,1, .4f,.3f, -1,1,0, 0,0,1, .2f,.6f};
        auto first=helengine::windows::Win32SceneLightmapDraw::Create(Device.Get(),corners,{{.1f,.1f},{.2f,.1f},{.1f,.2f}});
        auto second=helengine::windows::Win32SceneLightmapDraw::Create(Device.Get(),corners,{{.7f,.7f},{.8f,.7f},{.7f,.8f}});
        Require(first.Vertices.Get()!=second.Vertices.Get() && first.Uvs.Get()!=second.Uvs.Get() && first.Count==3,"Shared model instances own independent seam-expanded GPU streams");
        first.Bind(Context.Get());ComPtr<ID3D11Buffer> uvStream;UINT stride=0,offset=0;Context->IAGetVertexBuffers(1,1,&uvStream,&stride,&offset);
        Require(uvStream.Get()==first.Uvs.Get() && stride==8 && offset==0,"Instance UV1 uses a separate eight-byte vertex stream");
        D3D11_BUFFER_DESC description{};first.Vertices->GetDesc(&description);description.BindFlags=0;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> copy;Check(Device->CreateBuffer(&description,nullptr,&copy));Context->CopyResource(copy.Get(),first.Vertices.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(copy.Get(),0,D3D11_MAP_READ,0,&mapped));Require(std::memcmp(mapped.pData,corners.data(),corners.size()*sizeof(float))==0,"Seam expansion preserves original normals and UV0 byte for byte");Context->Unmap(copy.Get(),0);
        bool rejected=false;try {helengine::windows::Win32SceneLightmapDraw::Create(Device.Get(),corners,{{0,0}});} catch(const std::invalid_argument&) {rejected=true;}Require(rejected,"Mismatched corner streams fail before GPU upload");
        helengine::windows::Win32SceneLightmap::Unbind(Context.Get());
        ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> view;ComPtr<ID3D11SamplerState> sampler;
        Context->PSGetConstantBuffers(11,1,&buffer);Context->PSGetShaderResources(23,1,&view);Context->PSGetSamplers(9,1,&sampler);
        Require(!buffer && !view && !sampler,"Dedicated bindings release cleanly");

    }
};
/// <summary>Reports offscreen contract failures without swallowing shader diagnostics.</summary>
int main() {try {SceneLightmapTests tests;tests.Run();std::cout<<"PASS static lightmap: HDR RGB, albedo/pi once, ambient/sun replacement, alpha, instance UV1 charts, ordinary mode, geometry1x/4x, binding cleanup\n";return 0;} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
