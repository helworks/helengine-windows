#include "platform/windows/win32/win32_scene_gi_usage.hpp"
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
/// <summary>Tracks actual production GI reader shaders against synthetic visible surfaces on WARP.</summary>
class SceneGiUsageTests {
    /// <summary>Owns the headless graphics device.</summary>
    ComPtr<ID3D11Device> Device;
    /// <summary>Submits owned fixture commands.</summary>
    ComPtr<ID3D11DeviceContext> Context;
    /// <summary>Contains all eight free probes, deliberately black to test usage independently of illumination.</summary>
    ComPtr<ID3D11ShaderResourceView> Atlas;
    /// <summary>Stores exact two-by-two-by-two grid geometry.</summary>
    ComPtr<ID3D11Buffer> Field;
    /// <summary>Stores a fullscreen receiver quad in the standard position-normal-UV mesh layout.</summary>
    ComPtr<ID3D11Buffer> Mesh;
    /// <summary>Retains an opaque material texture used by both depth and reader passes.</summary>
    ComPtr<ID3D11ShaderResourceView> Diffuse;
    /// <summary>Retains point sampling with exact alpha.</summary>
    ComPtr<ID3D11SamplerState> Sampler;
    /// <summary>Preserves the failing native operation's HRESULT.</summary>
    static void Check(HRESULT result) {if(FAILED(result)) throw std::runtime_error("D3D failure "+std::to_string(result));}
    /// <summary>Reports independently specified reader contracts.</summary>
    static void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    /// <summary>Uploads fixed ABI data without inventing missing fixture values.</summary>
    ComPtr<ID3D11Buffer> Buffer(const void* bytes,UINT size,UINT flags) {D3D11_BUFFER_DESC description{};description.ByteWidth=size;description.BindFlags=flags;D3D11_SUBRESOURCE_DATA initial{bytes,0,0};ComPtr<ID3D11Buffer> resource;Check(Device->CreateBuffer(&description,&initial,&resource));return resource;}
    /// <summary>Draws a surface whose center lands exactly on one probe, with consistent orthographic clip depth.</summary>
    void Receiver(helengine::windows::Win32SceneGiUsage& pass,float z,float x=-.5f,float y=-.5f,float alpha=1) {
        float transforms[48]{};for(int matrix=0;matrix<3;++matrix) for(int diagonal=0;diagonal<4;++diagonal) transforms[matrix*16+diagonal*5]=1;
        transforms[3]=x;transforms[7]=y;transforms[11]=z;transforms[16+10]=.5f;transforms[16+11]=z*.5f;
        auto transform=Buffer(transforms,sizeof(transforms),D3D11_BIND_CONSTANT_BUFFER);auto* binding=transform.Get();Context->VSSetConstantBuffers(0,1,&binding);
        const float color[]={1,1,1,alpha};auto material=Buffer(color,sizeof(color),D3D11_BIND_CONSTANT_BUFFER);binding=material.Get();Context->PSSetConstantBuffers(3,1,&binding);
        auto* source=Diffuse.Get();Context->PSSetShaderResources(0,1,&source);auto* sampler=Sampler.Get();Context->PSSetSamplers(0,1,&sampler);
        const UINT stride=8*sizeof(float),offset=0;binding=Mesh.Get();Context->IASetVertexBuffers(0,1,&binding,&stride,&offset);pass.BindGeometry();Context->Draw(6,0);
    }
    /// <summary>Reads only the eight owned usage integers, without screenshots or desktop resources.</summary>
    std::array<UINT,8> Read(helengine::windows::Win32SceneGiUsage& pass) {
        ComPtr<ID3D11Resource> resource;pass.GetView()->GetResource(&resource);ComPtr<ID3D11Buffer> buffer;Check(resource.As(&buffer));D3D11_BUFFER_DESC description{};buffer->GetDesc(&description);description.Usage=D3D11_USAGE_STAGING;description.BindFlags=description.MiscFlags=description.StructureByteStride=0;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Buffer> staging;Check(Device->CreateBuffer(&description,nullptr,&staging));Context->CopyResource(staging.Get(),buffer.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));std::array<UINT,8> values;std::memcpy(values.data(),mapped.pData,sizeof(values));Context->Unmap(staging.Get(),0);return values;
    }
    /// <summary>Feeds the actual GPU-produced mask directly into production sphere rendering, without a CPU mask substitute.</summary>
    void SphereIntegration(helengine::windows::Win32SceneGiUsage& usage) {
        D3D11_TEXTURE2D_DESC description{};description.Width=description.Height=64;description.MipLevels=description.ArraySize=1;description.SampleDesc.Count=1;description.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;description.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> output;Check(Device->CreateTexture2D(&description,nullptr,&output));ComPtr<ID3D11RenderTargetView> target;Check(Device->CreateRenderTargetView(output.Get(),nullptr,&target));
        description.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;description.BindFlags=D3D11_BIND_DEPTH_STENCIL;ComPtr<ID3D11Texture2D> depth;Check(Device->CreateTexture2D(&description,nullptr,&depth));ComPtr<ID3D11DepthStencilView> depthView;Check(Device->CreateDepthStencilView(depth.Get(),nullptr,&depthView));auto* targetBinding=target.Get();Context->OMSetRenderTargets(1,&targetBinding,depthView.Get());const float background[]={.07f,.07f,.07f,1};Context->ClearRenderTargetView(target.Get(),background);Context->ClearDepthStencilView(depthView.Get(),D3D11_CLEAR_DEPTH,1,0);D3D11_VIEWPORT viewport{0,0,64,64,0,1};Context->RSSetViewports(1,&viewport);
        const float projection[]={1,0,0,0,0,1,0,0,0,0,.5f,0,0,0,0,1};helengine::windows::Win32SceneGiSpheres spheres(Device.Get());spheres.Draw(Context.Get(),projection,Atlas.Get(),Field.Get(),8,usage.GetView());
        description.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;description.BindFlags=0;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;Check(Device->CreateTexture2D(&description,nullptr,&staging));Context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));float used[4],unused[4];std::memcpy(used,static_cast<const char*>(mapped.pData)+48*mapped.RowPitch+16*16,16);std::memcpy(unused,static_cast<const char*>(mapped.pData)+48*mapped.RowPitch+48*16,16);Context->Unmap(staging.Get(),0);
        Require(used[0]==0 && used[1]==0 && used[2]==0 && used[3]==1,"Actual reader mask retains a black used probe sphere");Require(std::abs(unused[0]-.07f)<.001f && std::abs(unused[1]-.07f)<.001f,"Actual reader mask rejects a bright unused probe sphere");Context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    /// <summary>Runs both visibility stages for a camera, retaining another camera's previously recorded usage.</summary>
    void Camera(helengine::windows::Win32SceneGiUsage& pass,UINT samples,bool farSurface=false,float x=-.5f,float y=-.5f,float alpha=1) {pass.BeginCamera(1,1,samples);Receiver(pass,.5f,x,y,alpha);if(farSurface) Receiver(pass,1.5f,x,y);pass.BeginReaders();Receiver(pass,.5f,x,y,alpha);if(farSurface) Receiver(pass,1.5f,x,y);}
public:
    /// <summary>Creates exact grid, black atlas, opaque material and mesh fixtures.</summary>
    SceneGiUsageTests() {
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&Device,nullptr,&Context));
        const float parameters[]={-1,-1,0,1,1,1,1,0,2,2,2,0};Field=Buffer(parameters,sizeof(parameters),D3D11_BIND_CONSTANT_BUFFER);
        std::vector<float> pixels(4*12*4);for(unsigned lobe=0;lobe<6;++lobe) for(unsigned cell=0;cell<8;++cell) {unsigned pixel=(lobe*2+cell/4)*4+cell%4;pixels[pixel*4]=pixels[pixel*4+1]=pixels[pixel*4+2]=cell==0?0.f:3.f;pixels[pixel*4+3]=1;}
        D3D11_TEXTURE2D_DESC texture{};texture.Width=4;texture.Height=12;texture.MipLevels=texture.ArraySize=1;texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA initial{pixels.data(),4*16,0};ComPtr<ID3D11Texture2D> atlas;Check(Device->CreateTexture2D(&texture,&initial,&atlas));Check(Device->CreateShaderResourceView(atlas.Get(),nullptr,&Atlas));
        const float white[]={1,1,1,1};texture.Width=texture.Height=1;initial={white,16,0};ComPtr<ID3D11Texture2D> diffuse;Check(Device->CreateTexture2D(&texture,&initial,&diffuse));Check(Device->CreateShaderResourceView(diffuse.Get(),nullptr,&Diffuse));
        D3D11_SAMPLER_DESC sampling{};sampling.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sampling.AddressU=sampling.AddressV=sampling.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampling.MaxLOD=D3D11_FLOAT32_MAX;Check(Device->CreateSamplerState(&sampling,&Sampler));
        const float vertices[]={-1,-1,0,0,0,1,0,0, -1,1,0,0,0,1,0,1, 1,-1,0,0,0,1,1,0, 1,-1,0,0,0,1,1,0, -1,1,0,0,0,1,0,1, 1,1,0,0,0,1,1,1};Mesh=Buffer(vertices,sizeof(vertices),D3D11_BIND_VERTEX_BUFFER);
    }
    /// <summary>Checks visible positive-weight taps, zero-energy usage, camera union, overdraw, alpha and frame reset.</summary>
    void Run() {
        helengine::windows::Win32SceneGiUsage pass(Device.Get(),8);
        for(UINT samples:{1u,4u}) {
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);auto* sentinel=Field.Get();Context->VSSetConstantBuffers(0,1,&sentinel);auto* source=Diffuse.Get();Context->PSSetShaderResources(0,1,&source);
            pass.Begin(Context.Get(),Atlas.Get(),Field.Get());Camera(pass,samples,true);pass.End();auto used=Read(pass);Require(used[0]==1,"Visible black receiver must mark its positive-weight probe");for(int i=1;i<8;++i) Require(used[i]==0,"Zero-weight and depth-occluded receiver probes must remain unused");
            D3D11_PRIMITIVE_TOPOLOGY topology;Context->IAGetPrimitiveTopology(&topology);ComPtr<ID3D11Buffer> restored;Context->VSGetConstantBuffers(0,1,&restored);ComPtr<ID3D11ShaderResourceView> restoredView;Context->PSGetShaderResources(0,1,&restoredView);Require(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST && restored.Get()==Field.Get() && restoredView.Get()==Diffuse.Get(),"Reader tracking restores caller pipeline bindings");
            SphereIntegration(pass);
            pass.Begin(Context.Get(),Atlas.Get(),Field.Get());Camera(pass,samples);Camera(pass,samples,false,.5f,.5f);pass.End();used=Read(pass);Require(used[0]==1 && used[3]==1,"Both original cameras contribute to one union without clearing the first camera");
            pass.Begin(Context.Get(),Atlas.Get(),Field.Get());Camera(pass,samples,false,.5f,.5f);pass.End();used=Read(pass);Require(used[0]==0 && used[3]==1,"Moved receivers remove stale usage on the next draw");
            pass.Begin(Context.Get(),Atlas.Get(),Field.Get());Camera(pass,samples,false,-.5f,-.5f,0);pass.End();used=Read(pass);for(UINT value:used) Require(value==0,"Discarded alpha surfaces consume no probes");
        }
    }
};
/// <summary>Exits unsuccessfully on any actual GPU reader contract failure.</summary>
int main() {try {SceneGiUsageTests tests;tests.Run();std::cout<<"PASS GI usage: actual-mask sphere integration, used black/unused lit, positive weights, occlusion, camera union, stale removal, alpha,1x/4x, context restoration\n";return 0;} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
