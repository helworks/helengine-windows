#include "platform/windows/win32/win32_scene_ssgi_shader.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>
#include "platform/windows/win32/win32_scene_ssgi.hpp"
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
using Microsoft::WRL::ComPtr;
/// <summary>Runs the actual screen-space transport and receiver HLSL on owned WARP resources.</summary>
class SceneSsgiTests {
    /// <summary>Owns the headless software device.</summary>
    ComPtr<ID3D11Device> Device;
    /// <summary>Submits fixture draws without a window or swapchain.</summary>
    ComPtr<ID3D11DeviceContext> Context;
    /// <summary>Preserves native failures with a precise HRESULT.</summary>
    static void Check(HRESULT result) { if(FAILED(result)) throw std::runtime_error("D3D failure "+std::to_string(result)); }
    /// <summary>Compiles the same HLSL entry points consumed by production.</summary>
    ComPtr<ID3DBlob> Compile(const std::string& source,const char* entry,const char* profile) {
        ComPtr<ID3DBlob> code,error;
        HRESULT result=D3DCompile(source.data(),source.size(),"SsgiTests",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
        if(FAILED(result) && error) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));
        Check(result);return code;
    }
    /// <summary>Uploads IEEE32 constants to the shared production ABI.</summary>
    void Constants(const std::vector<float>& data) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(data.size()*4);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{data.data(),0,0};ComPtr<ID3D11Buffer> buffer;Check(Device->CreateBuffer(&desc,&initial,&buffer));
        auto* binding=buffer.Get();Context->PSSetConstantBuffers(10,1,&binding);
    }
    /// <summary>Creates a deterministic 32x32 floating-point attachment.</summary>
    ComPtr<ID3D11Texture2D> Texture(const std::vector<float>& pixels,UINT flags) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=32;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.SampleDesc.Count=1;desc.BindFlags=flags;
        D3D11_SUBRESOURCE_DATA initial{pixels.data(),32*16,0};ComPtr<ID3D11Texture2D> texture;Check(Device->CreateTexture2D(&desc,pixels.empty()?nullptr:&initial,&texture));return texture;
    }
    /// <summary>Draws the actual trace or receiver shader against independently authored visible surface data.</summary>
    std::array<float,4> Draw(bool empty,bool backface,bool mismatch,bool grayscale,bool noRadiance=false,bool wrongNormal=false,bool lateralMismatch=false,int blockerScenario=0,bool perspective=false) {
        std::vector<float> position(32*32*4),normal(position.size()),radiance(position.size());
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
            int i=(y*32+x)*4;position[i]=(x+.5f)/16-1;position[i+1]=1-(y+.5f)/16;position[i+2]=.65f;position[i+3]=empty?0:1;
            normal[i+2]=backface?1:-1;normal[i+3]=1;radiance[i]=noRadiance?0:3;radiance[i+1]=noRadiance?0:1;radiance[i+3]=1;
        }
        int center=(16*32+16)*4;position[center+2]=.3f;position[center+3]=1;normal[center+2]=1;
        if(perspective) for(int i=0;i<32*32*4;i+=4) {position[i]*=position[i+2]+2;position[i+1]*=position[i+2]+2;}
        if(lateralMismatch) position[center]+=.5f;
        if(blockerScenario>0) {
            std::fill(position.begin(),position.end(),0.0f);std::fill(normal.begin(),normal.end(),0.0f);std::fill(radiance.begin(),radiance.end(),0.0f);
            position[center]=.03125f;position[center+1]=-.03125f;position[center+2]=.3f;position[center+3]=1;normal[center+2]=1;
            int source=(27*32+10)*4;position[source]=-.34375f;position[source+1]=-.71875f;position[source+2]=.545f;position[source+3]=1;normal[source+2]=-1;radiance[source]=3;radiance[source+1]=1;
            if(blockerScenario==2) {int blocker=(21*32+13)*4;position[blocker]=-.15625f;position[blocker+1]=-.34375f;position[blocker+2]=.445f;position[blocker+3]=1;normal[blocker+2]=-1;}
            if(perspective) {
                std::fill(position.begin(),position.end(),0.0f);std::fill(normal.begin(),normal.end(),0.0f);std::fill(radiance.begin(),radiance.end(),0.0f);
                position[center]=.071875f;position[center+1]=-.071875f;position[center+2]=.3f;position[center+3]=1;normal[center+2]=1;
                source=(20*32+14)*4;position[source]=-.23859375f;position[source+1]=-.71578125f;position[source+2]=.545f;position[source+3]=1;normal[source+2]=-1;radiance[source]=3;radiance[source+1]=1;
                if(blockerScenario==2) {int blocker=(18*32+15)*4;position[blocker]=-.07640625f;position[blocker+1]=-.38203125f;position[blocker+2]=.445f;position[blocker+3]=1;normal[blocker+2]=-1;}
            }
        }
        auto p=Texture(position,D3D11_BIND_SHADER_RESOURCE),n=Texture(normal,D3D11_BIND_SHADER_RESOURCE),r=Texture(radiance,D3D11_BIND_SHADER_RESOURCE);
        std::vector<float> constants(36);constants[0]=constants[5]=constants[10]=constants[15]=1;
        if(perspective) {constants[10]=10.0f/9.9f;constants[11]=2*(10.0f/9.9f)-1.0f/9.9f;constants[14]=1;constants[15]=2;}
        constants[16]=constants[17]=32;constants[18]=1;constants[19]=grayscale?1:0;
        constants[22]=-2;constants[26]=1;constants[28]=0;constants[29]=0;constants[30]=-1;
        constants[32]=3;constants[33]=1;
        Constants(constants);
        ComPtr<ID3D11ShaderResourceView> views[4];Check(Device->CreateShaderResourceView(p.Get(),nullptr,&views[0]));Check(Device->CreateShaderResourceView(n.Get(),nullptr,&views[1]));Check(Device->CreateShaderResourceView(r.Get(),nullptr,&views[2]));
        ID3D11ShaderResourceView* bindings[]={views[0].Get(),views[1].Get(),views[2].Get()};Context->PSSetShaderResources(18,3,bindings);
        auto output=Texture({},D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);ComPtr<ID3D11RenderTargetView> target;Check(Device->CreateRenderTargetView(output.Get(),nullptr,&target));auto* binding=target.Get();Context->OMSetRenderTargets(1,&binding,nullptr);
        auto vsCode=Compile(helengine::windows::SceneSsgiShader,"SsgiFullscreenVS","vs_5_0"),psCode=Compile(helengine::windows::SceneSsgiShader,"SsgiTracePS","ps_5_0");
        ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;Check(Device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs));Check(Device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps));Context->VSSetShader(vs.Get(),nullptr,0);Context->PSSetShader(ps.Get(),nullptr,0);Context->Draw(3,0);
        Context->OMSetRenderTargets(0,nullptr,nullptr);Check(Device->CreateShaderResourceView(output.Get(),nullptr,&views[3]));binding=nullptr;
        auto receiver=Texture({},D3D11_BIND_RENDER_TARGET);ComPtr<ID3D11RenderTargetView> receiverTarget;Check(Device->CreateRenderTargetView(receiver.Get(),nullptr,&receiverTarget));binding=receiverTarget.Get();Context->OMSetRenderTargets(1,&binding,nullptr);
        auto* gi=views[3].Get();Context->PSSetShaderResources(21,1,&gi);
        std::string source=helengine::windows::SceneSsgiShader;
        source+="float4 ReceiverPS(float4 pixel:SV_POSITION):SV_Target {float3 p=float3("+std::string(perspective?".071875,-.071875":".03125,-.03125")+","+std::string(mismatch?".1":".3")+");float3 e=SampleSharedSsgi(p,float3(0,0,"+std::string(wrongNormal?"-1":"1")+"));return float4(SsgiDisplay(e),1);}";
        auto receiverCode=Compile(source,"ReceiverPS","ps_5_0");Check(Device->CreatePixelShader(receiverCode->GetBufferPointer(),receiverCode->GetBufferSize(),nullptr,ps.ReleaseAndGetAddressOf()));Context->PSSetShader(ps.Get(),nullptr,0);Context->Draw(3,0);
        D3D11_TEXTURE2D_DESC desc{};receiver->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;Check(Device->CreateTexture2D(&desc,nullptr,&read));Context->CopyResource(read.Get(),receiver.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped));std::array<float,4> result;std::memcpy(result.data(),static_cast<const char*>(mapped.pData)+16*mapped.RowPitch+16*16,16);Context->Unmap(read.Get(),0);
        ID3D11ShaderResourceView* none[4]{};Context->PSSetShaderResources(18,4,none);Context->OMSetRenderTargets(0,nullptr,nullptr);return result;
    }
    /// <summary>Rejects zero transport, invented illumination and chromatic diagnostic output.</summary>
    static void Require(bool condition,const char* contract) {if(!condition) throw std::runtime_error(contract);}
    /// <summary>Reads an owned HDR texture pixel without screenshots, windows or image files.</summary>
    std::array<float,4> ReadView(ID3D11ShaderResourceView* view,UINT x,UINT y) {
        ComPtr<ID3D11Resource> resource;view->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;Check(resource.As(&texture));
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);Require(desc.Format==DXGI_FORMAT_R32G32B32A32_FLOAT,"Fixture reads full precision source geometry");
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;Check(Device->CreateTexture2D(&desc,nullptr,&staging));Context->CopyResource(staging.Get(),texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));std::array<float,4> result;std::memcpy(result.data(),static_cast<const char*>(mapped.pData)+y*mapped.RowPitch+x*16,16);Context->Unmap(staging.Get(),0);return result;
    }
    /// <summary>Exercises actual capture shader depth ownership and confirms both output bindings retain the exact same immutable sources.</summary>
    void CapturePipeline() {
        helengine::windows::Win32SceneSsgi pass(Device.Get());
        for(int attempt=0;attempt<1000 && !pass.TryPrepare();++attempt) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        Require(pass.TryPrepare(),"SSGI async pipeline must prepare");
        std::array<float,36> parameters{};parameters[0]=parameters[5]=parameters[10]=parameters[15]=1;parameters[22]=-2;parameters[26]=1;parameters[30]=1;parameters[32]=parameters[33]=parameters[34]=3;
        pass.BeginCapture(Context.Get(),32,32,parameters);
        D3D11_VIEWPORT viewport{};UINT count=1;Context->RSGetViewports(&count,&viewport);Require(viewport.Width==16 && viewport.Height==16,"Small viewports use half-resolution capture");
        std::vector<float> transform(56);for(int matrix=0;matrix<3;++matrix) for(int diagonal=0;diagonal<4;++diagonal) transform[matrix*16+diagonal*5]=1;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(transform.size()*4);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{transform.data(),0,0};ComPtr<ID3D11Buffer> transforms;Check(Device->CreateBuffer(&desc,&initial,&transforms));auto* buffer=transforms.Get();Context->VSSetConstantBuffers(0,1,&buffer);Context->PSSetConstantBuffers(0,1,&buffer);
        const float color[4]={1,.25f,0,1};desc.ByteWidth=16;initial.pSysMem=color;ComPtr<ID3D11Buffer> material;Check(Device->CreateBuffer(&desc,&initial,&material));buffer=material.Get();Context->PSSetConstantBuffers(3,1,&buffer);
        std::vector<float> white(32*32*4,1);auto diffuse=Texture(white,D3D11_BIND_SHADER_RESOURCE);ComPtr<ID3D11ShaderResourceView> diffuseView;Check(Device->CreateShaderResourceView(diffuse.Get(),nullptr,&diffuseView));auto* diffuseBinding=diffuseView.Get();Context->PSSetShaderResources(0,1,&diffuseBinding);
        D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;ComPtr<ID3D11SamplerState> sampler;Check(Device->CreateSamplerState(&samplerDesc,&sampler));auto* samplerBinding=sampler.Get();Context->PSSetSamplers(0,1,&samplerBinding);
        // First drawable is the red far wall. The second drawable is an unlit foreground receiver in a different presentation layer.
        const float vertices[]={-1,-1,.65f,0,0,-1,0,0, -1,1,.65f,0,0,-1,0,0, 1,-1,.65f,0,0,-1,0,0,
            1,-1,.65f,0,0,-1,0,0, -1,1,.65f,0,0,-1,0,0, 1,1,.65f,0,0,-1,0,0,
            -.12f,-.12f,.3f,0,0,1,0,0, -.12f,.12f,.3f,0,0,1,0,0, .12f,-.12f,.3f,0,0,1,0,0,
            .12f,-.12f,.3f,0,0,1,0,0, -.12f,.12f,.3f,0,0,1,0,0, .12f,.12f,.3f,0,0,1,0,0};
        desc.ByteWidth=sizeof(vertices);desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;initial.pSysMem=vertices;ComPtr<ID3D11Buffer> mesh;Check(Device->CreateBuffer(&desc,&initial,&mesh));buffer=mesh.Get();UINT stride=32,offset=0;Context->IASetVertexBuffers(0,1,&buffer,&stride,&offset);Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pass.BindCaptureGeometry(Context.Get());Context->Draw(6,0);pass.BindCaptureGeometry(Context.Get());Context->Draw(6,6);pass.EndCapture(Context.Get());pass.BindReceiver(Context.Get());
        ComPtr<ID3D11ShaderResourceView> firstPosition,firstNormal,firstRadiance,firstIndirect;Context->PSGetShaderResources(18,1,&firstPosition);Context->PSGetShaderResources(19,1,&firstNormal);Context->PSGetShaderResources(20,1,&firstRadiance);Context->PSGetShaderResources(21,1,&firstIndirect);
        auto receiver=ReadView(firstPosition.Get(),8,8);auto wall=ReadView(firstPosition.Get(),2,2);Require(std::abs(receiver[2]-.3f)<.00001f && std::abs(wall[2]-.65f)<.00001f,"Actual capture depth must contain both submitted output geometries, nearest first");
        helengine::windows::Win32SceneSsgi::ClearBindings(Context.Get());pass.BindReceiver(Context.Get());ComPtr<ID3D11ShaderResourceView> secondPosition,secondIndirect;Context->PSGetShaderResources(18,1,&secondPosition);Context->PSGetShaderResources(21,1,&secondIndirect);Require(firstPosition==secondPosition && firstIndirect==secondIndirect,"Separate output receivers must bind one identical shared capture and result");
        auto receiverOutput=Texture({},D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);ComPtr<ID3D11RenderTargetView> receiverTarget;Check(Device->CreateRenderTargetView(receiverOutput.Get(),nullptr,&receiverTarget));auto* target=receiverTarget.Get();Context->OMSetRenderTargets(1,&target,nullptr);
        std::string query=std::string(helengine::windows::SceneSsgiShader)+"float4 Query(float4 p:SV_POSITION):SV_Target{return p.x<8?SsgiRadiance.Load(int3(2,2,0)):float4(SampleSharedSsgi(float3(.0625,-.0625,.3),float3(0,0,1)),1);}";
        auto queryCode=Compile(query,"Query","ps_5_0");ComPtr<ID3D11PixelShader> queryPixel;Check(Device->CreatePixelShader(queryCode->GetBufferPointer(),queryCode->GetBufferSize(),nullptr,&queryPixel));Context->PSSetShader(queryPixel.Get(),nullptr,0);Context->Draw(3,0);Context->OMSetRenderTargets(0,nullptr,nullptr);
        ComPtr<ID3D11ShaderResourceView> queryView;Check(Device->CreateShaderResourceView(receiverOutput.Get(),nullptr,&queryView));
        auto direct=ReadView(queryView.Get(),2,2),bounce=ReadView(queryView.Get(),8,8);
        Require(std::abs(direct[0]-3.0f/3.14159265f)<.002f && std::abs(direct[1]-.75f/3.14159265f)<.002f,"Actual source capture must contain authored sun times albedo, without ambient or indirect feedback");
        Require(bounce[0]>.001f && bounce[0]>bounce[1]*3,"Actual foreground receiver must receive red bounce from the other submitted geometry");
        // A new capture must start from authored geometry, never a prior result.
        pass.BeginCapture(Context.Get(),3840,2160,parameters);Context->RSGetViewports(&count,&viewport);Require(viewport.Width==960 && viewport.Height==540,"4K capture must be capped at 960x540");
        helengine::windows::Win32SceneSsgi::ClearBindings(Context.Get());Context->OMSetRenderTargets(0,nullptr,nullptr);
        std::string toon=std::string(helengine::windows::PearlSurfaceShader)+helengine::windows::CameraToonGeometryShader+helengine::windows::CameraToonCompositeShader;
        for(const auto& request:helengine::windows::BuildCameraToonShaderRequests(toon)) {
            std::vector<D3D_SHADER_MACRO> macros;for(const auto& macro:request.Macros) macros.push_back({macro.Name.c_str(),macro.Value.c_str()});macros.push_back({nullptr,nullptr});ComPtr<ID3DBlob> code,error;
            HRESULT result=D3DCompile(toon.data(),toon.size(),"ActualToonSsgiVariants",macros.data(),nullptr,request.Entry.c_str(),request.Profile.c_str(),D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);if(FAILED(result)&&error) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));Check(result);
        }
    }
public:
    /// <summary>Creates a fixed-size offscreen fixture independent of all desktop state.</summary>
    SceneSsgiTests() {
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&Device,nullptr,&Context));
        D3D11_VIEWPORT viewport{0,0,32,32,0,1};Context->RSSetViewports(1,&viewport);Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;ComPtr<ID3D11RasterizerState> state;Check(Device->CreateRasterizerState(&raster,&state));Context->RSSetState(state.Get());
    }
    /// <summary>Tests visible bounce, absence of miss/backface/feedback light, receiver mismatch and grayscale.</summary>
    void Run() {
        auto bounce=Draw(false,false,false,false);Require(bounce[0]>.01f && bounce[0]>bounce[1]*2,"Visible red wall must produce colored indirect bounce");
        auto miss=Draw(true,false,false,false);Require(miss[0]<.00001f,"Screen misses must contribute zero");
        auto reverse=Draw(false,true,false,false);Require(reverse[0]<.00001f,"Backfacing emitters must contribute zero");
        auto rejected=Draw(false,false,true,false);Require(rejected[0]<.00001f,"Different receiver position must reject foreground transport");
        auto black=Draw(false,false,false,false,true);Require(black[0]<.00001f,"Zero source radiance must produce zero indirect, without source feedback");
        auto wrongNormal=Draw(false,false,false,false,false,true);Require(wrongNormal[0]<.00001f,"Opposite receiver normal must reject neighboring transport");
        auto lateral=Draw(false,false,false,false,false,false,true);Require(lateral[0]<.00001f,"Lateral position discontinuity must reject neighboring transport");
        auto gray=Draw(false,false,false,true);Require(gray[0]>.001f && std::abs(gray[0]-gray[1])<.000001f && std::abs(gray[1]-gray[2])<.000001f,"Diagnostic must show indirect luminance only");
        auto opening=Draw(false,false,false,false,false,false,false,1);Require(opening[0]>.001f,"Unoccluded sparse source must bounce along ray seven");
        auto blocked=Draw(false,false,false,false,false,false,false,2);Require(blocked[0]<.00001f,"One-pixel black blocker between world-step endpoints must prevent bright bounce behind it");
        auto perspectiveOpening=Draw(false,false,false,false,false,false,false,1,true);Require(perspectiveOpening[0]>.001f,"Translated perspective rays must gather the visible source");
        auto perspectiveBlocked=Draw(false,false,false,false,false,false,false,2,true);Require(perspectiveBlocked[0]<.00001f,"Perspective-correct crossed-cell intervals must stop at the black foreground blocker");
        CapturePipeline();
        std::cout<<"PASS shared SSGI: actual capture depth, shared output views, cap, toon variants, visible bounce, misses, emitter orientation, receiver position, zero source, grayscale\n";
    }
};
/// <summary>Returns a failing process status for any real shader or transport regression.</summary>
int main(){try{SceneSsgiTests tests;tests.Run();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
