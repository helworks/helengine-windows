#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include "platform/windows/win32/win32_camera_toon_pass.hpp"
#if defined(HELENGINE_PROBE_TESTS)
#include "platform/windows/win32/win32_scene_reflection_probe.hpp"
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

/// <summary>Checks the production metallic sun response on owned WARP targets without windows or capture.</summary>
class MetallicSunTests {
    /// <summary>Owns the headless software device used for deterministic material rendering.</summary>
    ComPtr<ID3D11Device> Device;
    /// <summary>Submits synthetic rendering commands and staging readbacks.</summary>
    ComPtr<ID3D11DeviceContext> Context;
    /// <summary>Runs a fullscreen triangle whose material attributes are supplied explicitly.</summary>
    ComPtr<ID3D11VertexShader> Vertex;
    /// <summary>Calls the ordinary production material shader with fixed synthetic geometry.</summary>
    ComPtr<ID3D11PixelShader> Ordinary;
    /// <summary>Calls the baked production material shader with the same synthetic geometry.</summary>
    ComPtr<ID3D11PixelShader> Baked;

#if defined(HELENGINE_PROBE_TESTS)
    /// <summary>Borrowed scene probe used only during explicit reflection fixture draws.</summary>
    const helengine::windows::Win32SceneReflectionProbe* FixtureProbe=nullptr;
#endif
    /// <summary>Sets an independently specified receiver normal without changing the production shader.</summary>
    std::array<float,3> FixtureNormal={0,0,1};
    /// <summary>Sets the receiver position for interior and exterior box-projection checks.</summary>
    std::array<float,3> FixturePosition={0,0,.5f};
    /// <summary>Sets the view direction for probe angular response checks.</summary>
    std::array<float,3> FixtureView={0,0,1};
    /// <summary>Sets the conductor tint and white-furnace reflectance explicitly.</summary>
    std::array<float,3> FixtureTint={.8f,.5f,.2f};
    /// <summary>Reads one actual camera parameter to detect stale material, contour or capture state.</summary>
    float BoundParameter(UINT index) {
        ComPtr<ID3D11Buffer> buffer;Context->PSGetConstantBuffers(8,1,&buffer);
        Require(buffer.Get()!=nullptr,"Registered geometry did not bind camera parameters.");
        D3D11_BUFFER_DESC description{};buffer->GetDesc(&description);
        description.BindFlags=0;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> readback;Check(Device->CreateBuffer(&description,nullptr,&readback));
        Context->CopyResource(readback.Get(),buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        float value=static_cast<const float*>(mapped.pData)[index];Context->Unmap(readback.Get(),0);return value;
    }

    /// <summary>Reads the independently registered conductor weight from the actual production camera buffer.</summary>
    float BoundMetallic() {return BoundParameter(27);}

    /// <summary>Checks public API validation, the real constant-buffer upload, legacy defaults and state isolation.</summary>
    void TestRegistration() {
        using helengine::windows::Win32CameraToonPass;
        Win32CameraToonPass pass(Device.Get(),0,0,3);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(!pass.TryPrepare()) {
            Require(std::chrono::steady_clock::now()<deadline,"Camera metallic API shader preparation timed out.");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        int metal=0,diffuse=0,unknown=0;
        pass.ConfigureMaterialEffects(&metal,0,0,false,false,1,1);
        pass.ConfigureMaterialEffects(&diffuse,0,0);
        pass.BindRegisteredGeometry(Context.Get(),&metal);
        Require(BoundMetallic()==1,"Registered metal did not reach the production GPU buffer.");
        pass.BindLightmapGeometry(Context.Get(),&metal);
        Require(BoundMetallic()==1,"Baked shader selection discarded the registered metallic response.");
        pass.BindRegisteredGeometry(Context.Get(),&diffuse);
        Require(BoundMetallic()==0,"Legacy material inherited metallic state.");
        pass.BindRegisteredGeometry(Context.Get(),&metal);
        pass.BindRegisteredGeometry(Context.Get(),&unknown);
        Require(BoundMetallic()==0,"Unregistered material inherited metallic state.");
        pass.BindRegisteredGeometry(Context.Get(),&unknown,true);
        Require(BoundParameter(37)==0,"Realtime ink outlined an unregistered room material.");
        pass.BindRegisteredGeometry(Context.Get(),&metal,true);
        Require(BoundParameter(37)==1,"Realtime ink suppressed an explicitly registered contour.");
        const float part=BoundParameter(3);
        pass.BindCaptureGeometry(Context.Get(),&metal);
        Require(BoundMetallic()==1 && BoundParameter(3)==0 && BoundParameter(37)==0,"Capture lost metallic eligibility or wrote outline identities.");
        pass.BindRegisteredGeometry(Context.Get(),&metal,true);
        Require(BoundParameter(3)==part+1,"Capture changed the presentation part-identifier allocator.");
        for(float invalid:{-.01f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
            bool rejected=false;
            try {pass.ConfigureMaterialEffects(&metal,0,0,false,false,1,invalid);}
            catch(const std::invalid_argument&) {rejected=true;}
            Require(rejected,"Material registration accepted a nonfinite or out-of-range metallic weight.");
            rejected=false;
            try {pass.BindGeometry(Context.Get(),0,0,false,false,1,invalid);}
            catch(const std::invalid_argument&) {rejected=true;}
            Require(rejected,"Geometry binding accepted a nonfinite or out-of-range metallic weight.");
        }
        pass.RemoveMaterialEffects(&metal);pass.BindRegisteredGeometry(Context.Get(),&metal);
        Require(BoundMetallic()==0,"Removed material retained metallic state.");
        std::cout<<"PASS metallic material API: registration, baked selection, legacy/unknown/removed defaults, finite range validation\n";
    }

    /// <summary>Preserves native failures instead of continuing with incomplete GPU resources.</summary>
    static void Check(HRESULT result) {
        if(FAILED(result)) throw std::runtime_error("Metallic fixture D3D failure "+std::to_string(result));
    }

    /// <summary>Reports disagreement with independently specified material and visibility contracts.</summary>
    static void Require(bool condition,const char* message) {
        if(!condition) throw std::runtime_error(message);
    }

    /// <summary>Uploads explicit IEEE32 constants to a production pixel-shader binding.</summary>
    void Constants(UINT slot,const std::vector<float>& values) {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth=static_cast<UINT>(values.size()*sizeof(float));
        description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{values.data(),0,0};
        ComPtr<ID3D11Buffer> buffer;
        Check(Device->CreateBuffer(&description,&data,&buffer));
        auto* binding=buffer.Get();Context->PSSetConstantBuffers(slot,1,&binding);
    }

    /// <summary>Creates an owned one-pixel float resource for material inputs, output or shadow visibility.</summary>
    ComPtr<ID3D11Texture2D> Texture(UINT flags,UINT samples,const std::vector<float>& values={}) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width=description.Height=description.MipLevels=description.ArraySize=1;
        description.SampleDesc.Count=samples;description.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        description.BindFlags=flags;
        D3D11_SUBRESOURCE_DATA data{values.data(),16,0};
        ComPtr<ID3D11Texture2D> texture;
        Check(Device->CreateTexture2D(&description,values.empty()?nullptr:&data,&texture));return texture;
    }

    /// <summary>Binds one explicit fixture texture with no hidden fallback resource.</summary>
    void Bind(UINT slot,ID3D11Texture2D* texture) {
        ComPtr<ID3D11ShaderResourceView> view;
        Check(Device->CreateShaderResourceView(texture,nullptr,&view));
        auto* binding=view.Get();Context->PSSetShaderResources(slot,1,&binding);
    }

    /// <summary>Compiles the actual material source, retaining HLSL diagnostics on failure.</summary>
    ComPtr<ID3DBlob> Compile(const std::string& source,const char* entry,const char* profile) {
        D3D_SHADER_MACRO macros[]={{"TOON_SAMPLES","1"},{nullptr,nullptr}};
        ComPtr<ID3DBlob> code,error;
        HRESULT result=D3DCompile(source.data(),source.size(),"MetallicSun",macros,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
        if(FAILED(result) && error) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));
        Check(result);return code;
    }

    /// <summary>Evaluates one material sample with independently controlled view, light, roughness and alpha.</summary>
    std::array<float,4> Draw(bool baked,UINT samples,float metallic,float roughness,float viewAngle=0,float lightZ=1,float alpha=1,bool shadowed=false,float viewLength=1) {
        Context->ClearState();
        std::vector<float> transforms(76,0);
        transforms[48]=std::sin(viewAngle)*viewLength;transforms[50]=.5f+std::cos(viewAngle)*viewLength;
#if defined(HELENGINE_PROBE_TESTS)
        if(FixtureProbe) for(int axis=0;axis<3;++axis) transforms[48+axis]=FixturePosition[axis]+FixtureView[axis];
#endif
        transforms[52]=1;Constants(0,transforms);
        Constants(5,{FixturePosition[0],FixturePosition[1],FixturePosition[2],0,FixtureNormal[0],FixtureNormal[1],FixtureNormal[2],0});
#if defined(HELENGINE_PROBE_TESTS)
        if(FixtureProbe) FixtureProbe->Bind(Context.Get());
#endif
        std::vector<float> lights(72,0);
        lights[0]=lights[1]=lights[2]=.2f;
        lights[4]=lightZ==0?0.f:1.f;
        lights[8]=lights[9]=lights[10]=1;lights[14]=-lightZ;Constants(1,lights);
        Constants(3,{FixtureTint[0],FixtureTint[1],FixtureTint[2],alpha});Constants(4,{roughness,0,0,0});Constants(7,{0,0,0,0});
        std::vector<float> toon(40,0);
        toon[2]=3;toon[4]=toon[5]=1;toon[27]=metallic;
        toon[16]=shadowed?1.f:0.f;Constants(8,toon);
        std::vector<float> shadow(100,0);
        shadow[0]=shadowed?1.f:0.f;shadow[1]=shadow[2]=1;
        shadow[6]=shadow[7]=shadow[8]=shadow[9]=1;
        for(int diagonal=0;diagonal<4;++diagonal) shadow[12+diagonal*5]=1;
        Constants(2,shadow);
        auto white=Texture(D3D11_BIND_SHADER_RESOURCE,1,{1,1,1,1});
        auto atlas=Texture(D3D11_BIND_SHADER_RESOURCE,1,{2,3,4,1});
        auto occluded=Texture(D3D11_BIND_SHADER_RESOURCE,1,{0,0,0,0});
        Bind(0,white.Get());Bind(6,white.Get());Bind(23,atlas.Get());Bind(1,occluded.Get());
        D3D11_SAMPLER_DESC sampling{};
        sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampling.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;Check(Device->CreateSamplerState(&sampling,&sampler));
        auto* samplerBinding=sampler.Get();
        for(UINT slot:{0u,1u,6u,9u}) Context->PSSetSamplers(slot,1,&samplerBinding);
        auto output=Texture(D3D11_BIND_RENDER_TARGET,samples);
        ComPtr<ID3D11RenderTargetView> target;Check(Device->CreateRenderTargetView(output.Get(),nullptr,&target));
        auto* targetBinding=target.Get();Context->OMSetRenderTargets(1,&targetBinding,nullptr);
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;
        raster.DepthClipEnable=TRUE;raster.MultisampleEnable=samples>1;
        ComPtr<ID3D11RasterizerState> state;Check(Device->CreateRasterizerState(&raster,&state));Context->RSSetState(state.Get());
        D3D11_VIEWPORT viewport{0,0,1,1,0,1};Context->RSSetViewports(1,&viewport);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->VSSetShader(Vertex.Get(),nullptr,0);Context->PSSetShader(baked?Baked.Get():Ordinary.Get(),nullptr,0);
        Context->Draw(3,0);Context->OMSetRenderTargets(0,nullptr,nullptr);
        if(samples>1) {
            auto resolved=Texture(0,1);Context->ResolveSubresource(resolved.Get(),0,output.Get(),0,DXGI_FORMAT_R32G32B32A32_FLOAT);output=resolved;
        }
        D3D11_TEXTURE2D_DESC description{};output->GetDesc(&description);
        description.BindFlags=0;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;Check(Device->CreateTexture2D(&description,nullptr,&readback));
        Context->CopyResource(readback.Get(),output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        std::array<float,4> result;std::memcpy(result.data(),mapped.pData,16);Context->Unmap(readback.Get(),0);
        for(float channel:result) Require(std::isfinite(channel),"Metallic shading produced a nonfinite channel.");
        return result;
    }

#if defined(HELENGINE_PROBE_TESTS)
    /// <summary>Builds complete explicit cube data with directional colors, increasing mip intensity, or a white furnace.</summary>
    static hellm::ReflectionProbe MakeProbe(int pattern) {
        hellm::ReflectionProbe probe;probe.FaceSize=16;probe.BoundsMin={-1,-1,-1};probe.BoundsMax={1,1,1};
        const float colors[6][3]={{2,0,0},{0,2,0},{0,0,2},{2,2,0},{2,0,2},{0,2,2}};
        for(UINT mip=0,size=16;size;mip++,size>>=1) {
            std::vector<float> pixels;
            for(UINT face=0;face<6;++face) for(UINT pixel=0;pixel<size*size;++pixel) {
                for(int channel=0;channel<3;++channel) pixels.push_back(pattern==0?colors[face][channel]:pattern==1?static_cast<float>(mip+1):1.f);
                pixels.push_back(1);
            }
            probe.Mips.push_back(std::move(pixels));
        }
        return probe;
    }
    /// <summary>Checks actual production cube upload, directional sampling, box projection, HDR, roughness, Fresnel, ownership and cleanup.</summary>
    void TestProbe() {
        using helengine::windows::Win32SceneReflectionProbe;
        Win32SceneReflectionProbe directional(Device.Get(),MakeProbe(0)),mips(Device.Get(),MakeProbe(1)),white(Device.Get(),MakeProbe(2)),missing(Device.Get(),{});
        const std::array<std::array<float,3>,6> axes={{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
        const float colors[6][3]={{2,0,0},{0,2,0},{0,0,2},{2,2,0},{2,0,2},{0,2,2}};
        for(UINT samples:{1u,4u}) for(bool baked:{false,true}) {
            FixtureProbe=&directional;FixturePosition={0,0,0};
            for(UINT face=0;face<6;++face) {
                FixtureNormal=FixtureView=axes[face];auto result=Draw(baked,samples,1,0,0,0);
                for(int channel=0;channel<3;++channel) Require(std::abs(result[channel]-colors[face][channel]*FixtureTint[channel])<.012f,"HDR cube face direction or conductor tint is incorrect.");
            }
            FixtureNormal=FixtureView={0,0,1};FixtureProbe=&mips;
            const auto smooth=Draw(baked,samples,1,0,0,0),rough=Draw(baked,samples,1,1,0,0);
            Require(rough[0]>smooth[0]*1.4f,"Authored roughness did not sample the prefiltered mip chain.");
            Require(rough[0]>1,"HDR scene radiance was clamped before composition.");
            FixtureProbe=&white;FixtureTint={1,1,1};
            for(float roughness:{0.f,.2f,.5f,1.f}) for(float angle:{0.f,.7f,1.4f}) {
                FixtureView={std::sin(angle),0,std::cos(angle)};auto result=Draw(baked,samples,1,roughness,0,0);
                for(int channel=0;channel<3;++channel) Require(result[channel]>=0 && result[channel]<=1.0001f,"GGX white furnace created energy.");
            }
            FixtureTint={.8f,.5f,.2f};FixtureView={0,0,1};auto front=Draw(baked,samples,1,0,0,0);
            FixtureView={.999f,0,.04471018f};auto grazing=Draw(baked,samples,1,0,0,0);
            Require(grazing[2]>front[2]*2,"Integrated conductor Fresnel did not increase toward grazing.");
            FixtureView={0,0,1};FixtureProbe=&directional;
            FixturePosition={.99f,0,0};FixtureView={-.6f,0,.8f};auto projected=Draw(baked,samples,1,0,0,0);
            FixturePosition={1.01f,0,0};auto exterior=Draw(baked,samples,1,0,0,0);
            Require(projected[0]>.5f && projected[2]<.03f && exterior[0]==0 && exterior[1]==0 && exterior[2]==0,"Box projection did not move the interior ray to +X or reject exterior receivers.");
            FixturePosition={0,0,0};FixtureView={0,0,1};FixtureProbe=&missing;
            auto absent=Draw(baked,samples,1,.3f,0,0);Require(absent[0]==0 && absent[1]==0 && absent[2]==0,"Missing probe created fake illumination.");
            FixtureProbe=&white;auto withProbeDiffuse=Draw(baked,samples,0,.3f,0,0);FixtureProbe=&missing;auto noProbeDiffuse=Draw(baked,samples,0,.3f,0,0);
            Require(withProbeDiffuse==noProbeDiffuse,"Probe changed nonmetal diffuse behavior.");
            FixtureProbe=&white;auto full=Draw(baked,samples,1,.3f,0,0),half=Draw(baked,samples,1,.3f,0,0,.5f);
            Require(std::abs(half[0]-full[0]*.5f)<.0001f && half[3]==.5f,"Probe broke premultiplied material alpha.");
        }
        directional.Bind(Context.Get());ComPtr<ID3D11ShaderResourceView> first,second;Context->PSGetShaderResources(24,1,&first);
        directional.Bind(Context.Get());Context->PSGetShaderResources(24,1,&second);Require(first.Get()==second.Get(),"Camera draws did not share the immutable cube resource.");
        Win32SceneReflectionProbe::Unbind(Context.Get());ComPtr<ID3D11ShaderResourceView> views[2];ComPtr<ID3D11Buffer> parameters;ComPtr<ID3D11SamplerState> sampler;
        Context->PSGetShaderResources(24,1,&views[0]);Context->PSGetShaderResources(25,1,&views[1]);Context->PSGetConstantBuffers(12,1,&parameters);Context->PSGetSamplers(10,1,&sampler);
        Require(!views[0] && !views[1] && !parameters && !sampler,"Probe bindings leaked into ordinary geometry or UI.");
        FixtureProbe=nullptr;FixturePosition={0,0,.5f};FixtureNormal=FixtureView={0,0,1};
        std::cout<<"PASS baked reflection: six cube faces, box projection/exterior rejection, roughness mips, HDR, tint, Fresnel, bounded white furnace, missing probe, diffuse, alpha, shared ownership and cleanup, 1x/4x ordinary/baked\n";
    }
#endif
public:
    /// <summary>Creates only a software GPU and wrappers around the real ordinary and baked entry points.</summary>
    MetallicSunTests() {
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&Device,nullptr,&Context));
        std::string source=std::string("#define HELENGINE_LIGHTMAP 1\n")+helengine::windows::PearlSurfaceShader+helengine::windows::CameraToonGeometryShader;
        source+=R"(
cbuffer ReflectionFixture : register(b5) {float4 ReceiverPosition;float4 ReceiverNormal;};
float4 FixtureVS(uint id:SV_VertexID):SV_Position {return float4(id==1?3:-1,id==2?3:-1,0,1);}
MeshOutput FixtureInput() {MeshOutput input=(MeshOutput)0;input.WorldPosition=ReceiverPosition.xyz;input.Normal=ReceiverNormal.xyz;input.UV=.5;input.LightmapUV=.5;return input;}
float4 FixturePS():SV_Target {return GeometryPS(FixtureInput()).Color;}
float4 FixtureBakedPS():SV_Target {return LightmapGeometryPS(FixtureInput()).Color;}
)";
        auto vertex=Compile(source,"FixtureVS","vs_5_0");
        auto ordinary=Compile(source,"FixturePS","ps_5_0");auto baked=Compile(source,"FixtureBakedPS","ps_5_0");
        Check(Device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&Vertex));
        Check(Device->CreatePixelShader(ordinary->GetBufferPointer(),ordinary->GetBufferSize(),nullptr,&Ordinary));
        Check(Device->CreatePixelShader(baked->GetBufferPointer(),baked->GetBufferSize(),nullptr,&Baked));
    }

    /// <summary>Checks diffuse compatibility, conductor tint, angular response, lobe width, darkness and visibility.</summary>
    void Run() {
        TestRegistration();
#if defined(HELENGINE_PROBE_TESTS)
        TestProbe();
#endif
        for(UINT samples:{1u,4u}) for(bool baked:{false,true}) {
            auto diffuse=Draw(baked,samples,0,.28f);
            const float expected=.8f*(baked?2.f/3.14159265f:.2f+1.f/3.14159265f);
            Require(std::abs(diffuse[0]-expected)<.0001f,"Metallic zero changed legacy diffuse or doubled baked sun.");
            auto aligned=Draw(baked,samples,1,.28f);
            auto angled=Draw(baked,samples,1,.28f,.5f);
            auto broad=Draw(baked,samples,1,.7f,.5f);
            auto broadAligned=Draw(baked,samples,1,.7f);
            Require(aligned[0]>diffuse[0]*2,"Metallic surface has no direct sun highlight.");
            Require(std::abs(aligned[0]/aligned[2]-4.f)<.01f,"Conductor Fresnel does not preserve the authored metal tint.");
            Require(aligned[0]>angled[0]*5,"The sun highlight does not respond to the viewing angle.");
            auto mixed=Draw(baked,samples,.5f,.28f);
            Require(std::abs(mixed[0]-(aligned[0]+diffuse[0])*.5f)<.0001f,"Partial metallic did not partition diffuse and conductor energy.");
            Require(broad[0]/broadAligned[0]>angled[0]/aligned[0]*5,"Authored roughness does not broaden the highlight.");
            auto off=Draw(baked,samples,1,.28f,0,0);
            auto behind=Draw(baked,samples,1,.28f,0,-1);
            auto shadowed=Draw(baked,samples,1,.28f,0,1,1,true);
            Require(off[0]==0 && behind[0]==0 && shadowed[0]==0,"Metal retained diffuse or a highlight without visible direct sun.");
            auto half=Draw(baked,samples,1,.28f,0,1,.5f);
            Require(std::abs(half[0]-aligned[0]*.5f)<.0001f && half[3]==.5f,"Metal shading broke premultiplied alpha.");
            Draw(baked,samples,1,0);Draw(baked,samples,1,1,3.14159265f);Draw(baked,samples,1,.28f,0,1,1,false,0);Draw(baked,samples,1,.28f,1.570795f);
            std::cout<<"PASS metallic sun "<<(baked?"baked":"ordinary")<<' '<<samples<<"x: diffuse="<<diffuse[0]<<", aligned="<<aligned[0]<<", angled="<<angled[0]<<", broad-angle="<<broad[0]<<", off/behind/shadow=0\n";
        }
    }
};

/// <summary>Exposes deterministic offscreen failures and shader compiler diagnostics to the invoking build.</summary>
int main() {
    try {MetallicSunTests tests;tests.Run();return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
