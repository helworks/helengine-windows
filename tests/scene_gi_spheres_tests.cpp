#include "platform/windows/win32/win32_scene_gi_spheres.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
/// <summary>Exercises the actual diagnostic pass on owned WARP textures without windows or captures.</summary>
class SceneGiSpheresTests {
    /// <summary>Owns the synthetic graphics device.</summary>
    ComPtr<ID3D11Device> Device;
    /// <summary>Submits fixture commands.</summary>
    ComPtr<ID3D11DeviceContext> Context;
    /// <summary>Retains native failure evidence.</summary>
    static void Check(HRESULT result) {if(FAILED(result)) throw std::runtime_error("D3D failure "+std::to_string(result));}
    /// <summary>Fails on an independently specified rendering contract.</summary>
    static void Require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
    /// <summary>Renders two overlapping real atlas probes and reads only the center fixture pixel.</summary>
    std::array<float,4> Draw(float metadata,bool enabled,bool reverseDepth=false,bool directional=false,bool zero=false,bool used=true) {
        D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=64;texture.MipLevels=texture.ArraySize=1;texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> output;Check(Device->CreateTexture2D(&texture,nullptr,&output));ComPtr<ID3D11RenderTargetView> target;Check(Device->CreateRenderTargetView(output.Get(),nullptr,&target));auto* binding=target.Get();
        texture.Format=DXGI_FORMAT_D32_FLOAT;texture.BindFlags=D3D11_BIND_DEPTH_STENCIL;ComPtr<ID3D11Texture2D> depth;Check(Device->CreateTexture2D(&texture,nullptr,&depth));ComPtr<ID3D11DepthStencilView> depthView;Check(Device->CreateDepthStencilView(depth.Get(),nullptr,&depthView));Context->OMSetRenderTargets(1,&binding,depthView.Get());
        const float background[]={.07f,.07f,.07f,1};Context->ClearRenderTargetView(target.Get(),background);Context->ClearDepthStencilView(depthView.Get(),D3D11_CLEAR_DEPTH,1,0);D3D11_VIEWPORT viewport{0,0,64,64,0,1};Context->RSSetViewports(1,&viewport);
        // Atlas width X*Y=1 and height Z*6=12. Cell zero is nearer unless projection reverses Z.
        std::vector<float> atlas(12*4);for(int lobe=0;lobe<6;++lobe) for(int cell=0;cell<2;++cell) {int i=(lobe*2+cell)*4;atlas[i]=(cell==0?.2f:.8f);atlas[i+1]=atlas[i];atlas[i+2]=atlas[i];atlas[i+3]=metadata;if(zero) atlas[i]=atlas[i+1]=atlas[i+2]=0;if(directional) {atlas[i]=lobe==5?1:0;atlas[i+1]=atlas[i+2]=0;}}
        texture.Width=1;texture.Height=12;texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data{atlas.data(),16,0};ComPtr<ID3D11Texture2D> source;Check(Device->CreateTexture2D(&texture,&data,&source));ComPtr<ID3D11ShaderResourceView> sourceView;Check(Device->CreateShaderResourceView(source.Get(),nullptr,&sourceView));
        const float parameters[]={-.5f,-.5f,.2f,1,1,1,.3f,0,1,1,2,0};D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(parameters);buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial{parameters,0,0};ComPtr<ID3D11Buffer> constants;Check(Device->CreateBuffer(&buffer,&initial,&constants));
        const float matrix[]={1,0,0,0,0,1,0,0,0,0,reverseDepth?-1.f:1.f,0,0,0,reverseDepth?1.f:0.f,1};
        D3D11_RASTERIZER_DESC rasterDescription{};rasterDescription.FillMode=D3D11_FILL_WIREFRAME;rasterDescription.CullMode=D3D11_CULL_FRONT;rasterDescription.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> sentinelRaster;Check(Device->CreateRasterizerState(&rasterDescription,&sentinelRaster));Context->RSSetState(sentinelRaster.Get());
        D3D11_DEPTH_STENCIL_DESC depthDescription{};depthDescription.DepthFunc=D3D11_COMPARISON_ALWAYS;
        ComPtr<ID3D11DepthStencilState> sentinelDepth;Check(Device->CreateDepthStencilState(&depthDescription,&sentinelDepth));Context->OMSetDepthStencilState(sentinelDepth.Get(),7);
        D3D11_BLEND_DESC blendDescription{};blendDescription.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED;
        ComPtr<ID3D11BlendState> sentinelBlend;Check(Device->CreateBlendState(&blendDescription,&sentinelBlend));const float blendFactors[]={.25f,.5f,.75f,1};Context->OMSetBlendState(sentinelBlend.Get(),blendFactors,0x12345678);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);auto* oldBuffer=constants.Get();Context->VSSetConstantBuffers(9,1,&oldBuffer);Context->PSSetConstantBuffers(9,1,&oldBuffer);Context->VSSetConstantBuffers(11,1,&oldBuffer);Context->VSSetShaderResources(17,1,sourceView.GetAddressOf());Context->PSSetShaderResources(17,1,sourceView.GetAddressOf());
        const UINT usageValues[]={used?1u:0u,used?1u:0u};D3D11_BUFFER_DESC usageDescription{};usageDescription.ByteWidth=sizeof(usageValues);usageDescription.BindFlags=D3D11_BIND_SHADER_RESOURCE;usageDescription.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;usageDescription.StructureByteStride=sizeof(UINT);D3D11_SUBRESOURCE_DATA usageData{usageValues,0,0};ComPtr<ID3D11Buffer> usageBuffer;Check(Device->CreateBuffer(&usageDescription,&usageData,&usageBuffer));ComPtr<ID3D11ShaderResourceView> usageView;Check(Device->CreateShaderResourceView(usageBuffer.Get(),nullptr,&usageView));auto* usageBinding=usageView.Get();Context->VSSetShaderResources(22,1,&usageBinding);
        helengine::windows::Win32SceneGiSpheres pass(Device.Get());pass.Draw(Context.Get(),matrix,sourceView.Get(),constants.Get(),2,usageView.Get(),enabled);
        ComPtr<ID3D11ShaderResourceView> restoredUsage;Context->VSGetShaderResources(22,1,&restoredUsage);Require(restoredUsage.Get()==usageView.Get(),"Sphere pass restores reader mask binding");
        D3D11_PRIMITIVE_TOPOLOGY topology;Context->IAGetPrimitiveTopology(&topology);Require(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Diagnostic restores topology");ComPtr<ID3D11Buffer> restored;Context->VSGetConstantBuffers(11,1,&restored);Require(restored.Get()==constants.Get(),"Diagnostic restores VS constant buffers");ComPtr<ID3D11ShaderResourceView> restoredView;Context->PSGetShaderResources(17,1,&restoredView);Require(restoredView.Get()==sourceView.Get(),"Diagnostic restores atlas resource binding");
        restored.Reset();Context->VSGetConstantBuffers(9,1,&restored);Require(restored.Get()==constants.Get(),"Diagnostic restores VS field constants");restored.Reset();Context->PSGetConstantBuffers(9,1,&restored);Require(restored.Get()==constants.Get(),"Diagnostic restores PS field constants");
        restoredView.Reset();Context->VSGetShaderResources(17,1,&restoredView);Require(restoredView.Get()==sourceView.Get(),"Diagnostic restores VS atlas binding");
        ComPtr<ID3D11RasterizerState> restoredRaster;Context->RSGetState(&restoredRaster);Require(restoredRaster.Get()==sentinelRaster.Get(),"Diagnostic restores rasterizer");
        ComPtr<ID3D11DepthStencilState> restoredDepth;UINT stencil;Context->OMGetDepthStencilState(&restoredDepth,&stencil);Require(restoredDepth.Get()==sentinelDepth.Get() && stencil==7,"Diagnostic restores depth and stencil reference");
        ComPtr<ID3D11BlendState> restoredBlend;float restoredFactors[4];UINT sampleMask;Context->OMGetBlendState(&restoredBlend,restoredFactors,&sampleMask);Require(restoredBlend.Get()==sentinelBlend.Get() && sampleMask==0x12345678 && std::memcmp(restoredFactors,blendFactors,sizeof(blendFactors))==0,"Diagnostic restores blend state, factors and sample mask");
        ComPtr<ID3D11VertexShader> restoredVertex;ComPtr<ID3D11PixelShader> restoredPixel;ComPtr<ID3D11GeometryShader> restoredGeometry;Context->VSGetShader(&restoredVertex,nullptr,nullptr);Context->PSGetShader(&restoredPixel,nullptr,nullptr);Context->GSGetShader(&restoredGeometry,nullptr,nullptr);Require(!restoredVertex && !restoredPixel && !restoredGeometry,"Diagnostic restores shaders");
        ComPtr<ID3D11RenderTargetView> restoredTarget;ComPtr<ID3D11DepthStencilView> restoredDepthView;Context->OMGetRenderTargets(1,&restoredTarget,&restoredDepthView);Require(restoredTarget.Get()==target.Get() && restoredDepthView.Get()==depthView.Get(),"Diagnostic retains output targets");
        UINT viewportCount=1;D3D11_VIEWPORT restoredViewport{};Context->RSGetViewports(&viewportCount,&restoredViewport);Require(viewportCount==1 && std::memcmp(&viewport,&restoredViewport,sizeof(viewport))==0,"Diagnostic retains viewport");
        Context->OMSetRenderTargets(0,nullptr,nullptr);texture.Width=texture.Height=64;texture.BindFlags=0;texture.Usage=D3D11_USAGE_STAGING;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;Check(Device->CreateTexture2D(&texture,nullptr,&staging));Context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));std::array<float,4> pixel;std::memcpy(pixel.data(),static_cast<const char*>(mapped.pData)+32*mapped.RowPitch+32*16,16);Context->Unmap(staging.Get(),0);Context->ClearState();return pixel;
    }
public:
    /// <summary>Allocates a headless WARP device.</summary>
    SceneGiSpheresTests() {D3D_FEATURE_LEVEL level;Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&Device,&level,&Context));}
    /// <summary>Verifies intensity, occupancy, grayscale, directional lobes, depth, and untouched disabled behavior.</summary>
    void Run() {
        auto p=Draw(1,true);Require(std::abs(p[0]-.2f)<.005f,"Near probe retains published intensity");Require(p[0]==p[1] && p[1]==p[2],"Diagnostic is grayscale");
        p=Draw(1,true,true);Require(std::abs(p[0]-.8f)<.005f,"Depth wins independently of instance order");
        p=Draw(0,true);Require(std::abs(p[0]-.07f)<.001f,"Occupied probes emit no spheres");
        p=Draw(-1,true);Require(std::abs(p[0]-.07f)<.001f,"Mixed occupied probes emit no spheres");
        p=Draw(1,false);Require(std::abs(p[0]-.07f)<.001f,"Disabled pass leaves framebuffer unchanged");
        p=Draw(1,true,false,false,false,false);Require(std::abs(p[0]-.07f)<.001f,"Unused lit probes leave the framebuffer untouched");
        p=Draw(1,true,false,true);Require(p[0]>.1f && p[0]<.213f && p[0]==p[1] && p[1]==p[2],"Sphere normals select negative-Z irradiance and Rec709 luminance");
        p=Draw(1,true,false,false,true);Require(p[0]==0 && p[1]==0 && p[2]==0 && p[3]==1,"Used zero-irradiance probes remain visible and black");
    }
};
/// <summary>Reports GPU contract failures as an unsuccessful test process.</summary>
int main() {try {SceneGiSpheresTests tests;tests.Run();std::cout<<"PASS voxel spheres: published intensity, directional grayscale, occupancy, depth, disabled path, state restoration\n";return 0;} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
