#include "platform/windows/win32/win32_scene_gi_shader.hpp"
#include "platform/windows/directx11/directx11_shader_request.hpp"
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include "helgi/VoxelGi.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace helengine::windows {
    /// <summary>Verifies shared field sampling and the actual toon variants on an owned WARP device without a window.</summary>
    class SceneGiShaderTests {
        /// <summary>Owns an offscreen software graphics device independent of the desktop.</summary>
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// <summary>Submits deterministic fixture draws and readbacks.</summary>
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
        /// <summary>Generates one full-screen triangle without vertex buffers.</summary>
        Microsoft::WRL::ComPtr<ID3D11VertexShader> Vertex;
        /// <summary>Calls the production sampling or visibility helper.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> Pixel;
        /// <summary>Stores one linear HDR output pixel.</summary>
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Output;
        /// <summary>Receives an exact float copy for CPU assertions.</summary>
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Readback;
        /// <summary>Attaches the owned output texture to the fixture.</summary>
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> Target;

        /// <summary>Preserves graphics failures with their HRESULT.</summary>
        static void Check(HRESULT result) {
            if(FAILED(result)) throw std::runtime_error("GI offscreen graphics failure: "+std::to_string(result));
        }
        /// <summary>Compiles real production HLSL and includes diagnostics when compilation fails.</summary>
        static Microsoft::WRL::ComPtr<ID3DBlob> Compile(const std::string& source,const char* entry,const char* profile) {
            Microsoft::WRL::ComPtr<ID3DBlob> binary,error;
            HRESULT result=D3DCompile(source.data(),source.size(),"SceneGiTests",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&binary,&error);
            if(FAILED(result) && error) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));
            Check(result);return binary;
        }
        /// <summary>Creates and binds one immutable test constant buffer.</summary>
        void BindConstants(UINT slot,const void* bytes,UINT size) {
            D3D11_BUFFER_DESC description{};description.ByteWidth=size;description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{bytes,0,0};Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
            Check(Device->CreateBuffer(&description,&initial,&buffer));auto* binding=buffer.Get();Context->PSSetConstantBuffers(slot,1,&binding);
        }
        /// <summary>Uploads an explicit atlas and invokes either sampling or a segment visibility query.</summary>
        helgi::Vec3 Draw(const helgi::Config& config,const std::vector<float>& atlas,helgi::Vec3 position,helgi::Vec3 normal,bool enabled=true,bool visibility=false) {
            const UINT width=config.CountX*config.CountY,height=config.CountZ*6;
            const float parameters[12]={config.Minimum.X,config.Minimum.Y,config.Minimum.Z,enabled?1.0f:0.0f,
                (config.Maximum.X-config.Minimum.X)/config.CountX,(config.Maximum.Y-config.Minimum.Y)/config.CountY,(config.Maximum.Z-config.Minimum.Z)/config.CountZ,0,
                static_cast<float>(config.CountX),static_cast<float>(config.CountY),static_cast<float>(config.CountZ),0};
            const float query[8]={position.X,position.Y,position.Z,visibility?1.0f:0.0f,normal.X,normal.Y,normal.Z,0};
            BindConstants(9,parameters,sizeof(parameters));BindConstants(10,query,sizeof(query));
            D3D11_TEXTURE2D_DESC texture{};texture.Width=width;texture.Height=height;texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial{atlas.data(),width*16,0};Microsoft::WRL::ComPtr<ID3D11Texture2D> source;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            Check(Device->CreateTexture2D(&texture,&initial,&source));Check(Device->CreateShaderResourceView(source.Get(),nullptr,&view));auto* binding=view.Get();Context->PSSetShaderResources(17,1,&binding);
            Context->Draw(3,0);Context->CopyResource(Readback.Get(),Output.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(Readback.Get(),0,D3D11_MAP_READ,0,&mapped));float result[4];std::memcpy(result,mapped.pData,sizeof(result));Context->Unmap(Readback.Get(),0);
            return {result[0],result[1],result[2]};
        }
        /// <summary>Asserts a linear-color result with explicit floating-point tolerance.</summary>
        static void Expect(helgi::Vec3 actual,helgi::Vec3 expected,const char* contract) {
            if(std::abs(actual.X-expected.X)>0.002f || std::abs(actual.Y-expected.Y)>0.002f || std::abs(actual.Z-expected.Z)>0.002f) throw std::runtime_error(std::string(contract)+": actual "+std::to_string(actual.X)+","+std::to_string(actual.Y)+","+std::to_string(actual.Z));
        }
        /// <summary>Writes the kernel's canonical occupied-plane metadata into one synthetic voxel.</summary>
        static void Plane(std::vector<float>& atlas,unsigned int x,unsigned int y,unsigned int z,float offset,bool mixed=false) {
            for(unsigned int lobe=0;lobe<6;++lobe) {
                const std::size_t index=((lobe*4+z)*16+x+4*y)*4;
                atlas[index]=lobe==1?offset:0;atlas[index+1]=lobe==0?1.0f:0.0f;atlas[index+2]=0;atlas[index+3]=mixed?-1.0f:0.0f;
            }
        }
    public:
        /// <summary>Creates a one-pixel fixture with no window, swap chain or desktop capture.</summary>
        SceneGiShaderTests() {
            Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&Device,nullptr,&Context));
            std::string source=SceneGiShader;
            source+=R"(cbuffer Query:register(b10){float4 Position;float4 Normal;};
float4 VS(uint id:SV_VertexID):SV_Position{return float4(id==1?3:-1,id==2?3:-1,0,1);}
float4 PS():SV_Target{return Position.w>0.5?float4(GiVisible(Position.xyz,Normal.xyz)?1:0,0,0,1):float4(SampleSceneGi(Position.xyz,Normal.xyz),1);})";
            auto vs=Compile(source,"VS","vs_5_0"),ps=Compile(source,"PS","ps_5_0");
            Check(Device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&Vertex));Check(Device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&Pixel));
            D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=texture.MipLevels=texture.ArraySize=1;texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_RENDER_TARGET;
            Check(Device->CreateTexture2D(&texture,nullptr,&Output));Check(Device->CreateRenderTargetView(Output.Get(),nullptr,&Target));texture.BindFlags=0;texture.Usage=D3D11_USAGE_STAGING;texture.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(Device->CreateTexture2D(&texture,nullptr,&Readback));
            D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;Microsoft::WRL::ComPtr<ID3D11RasterizerState> state;Check(Device->CreateRasterizerState(&raster,&state));Context->RSSetState(state.Get());
            D3D11_VIEWPORT viewport{0,0,1,1,0,1};Context->RSSetViewports(1,&viewport);auto* target=Target.Get();Context->OMSetRenderTargets(1,&target,nullptr);Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);Context->VSSetShader(Vertex.Get(),nullptr,0);Context->PSSetShader(Pixel.Get(),nullptr,0);
        }
        /// <summary>Verifies HDR, lobe orientation, disabled and out-of-volume behavior, blockers, openings and CPU agreement.</summary>
        void Run() {
            helgi::Config config{{0,0,0},{4,4,4},4,4,4,8};
            std::vector<float> atlas(4*4*4*6*4);
            for(std::size_t pixel=0;pixel<atlas.size()/4;++pixel) {atlas[pixel*4]=3;atlas[pixel*4+1]=2;atlas[pixel*4+2]=1;atlas[pixel*4+3]=1;}
            Expect(Draw(config,atlas,{1,1,1},{1,0,0}),{3,2,1},"HDR irradiance");
            Expect(Draw(config,atlas,{1,1,1},{1,0,0},false),{0,0,0},"Disabled field");
            Expect(Draw(config,atlas,{8,1,1},{1,0,0}),{0,0,0},"Outside volume");
            // Each directional lobe has a different red value, so sign and normalization errors are observable.
            for(unsigned int lobe=0;lobe<6;++lobe) for(unsigned int row=0;row<4;++row) for(unsigned int column=0;column<16;++column) atlas[((lobe*4+row)*16+column)*4]=static_cast<float>(lobe+1);
            Expect(Draw(config,atlas,{2,2,2},{-1,0,0}),{2,2,1},"Negative X lobe");
            const float diagonal=static_cast<float>(1/std::sqrt(2.0));
            Expect(Draw(config,atlas,{1,1,1},{diagonal,diagonal,0}),{2,2,1},"Positive lobe weighting");
            // A complete occupied wall blocks the segment even when destination probes contain bright irradiance.
            for(unsigned int lobe=0;lobe<6;++lobe) for(unsigned int z=0;z<4;++z) for(unsigned int y=0;y<4;++y) atlas[((lobe*4+z)*16+2+4*y)*4+3]=0;
            Expect(Draw(config,atlas,{0.5f,1.5f,1.5f},{3.5f,1.5f,1.5f},true,true),{0,0,0},"Occupied wall visibility");
            for(unsigned int lobe=0;lobe<6;++lobe) atlas[((lobe*4+1)*16+2+4)*4+3]=1;
            Expect(Draw(config,atlas,{0.5f,1.5f,1.5f},{3.5f,1.5f,1.5f},true,true),{1,0,0},"Open window visibility");
            for(std::size_t pixel=0;pixel<atlas.size()/4;++pixel) atlas[pixel*4+3]=1;
            for(unsigned int lobe=0;lobe<6;++lobe) atlas[(lobe*4*16+1)*4+3]=0;
            Expect(Draw(config,atlas,{0.5f,0.5f,0.5f},{1.5f,1.5f,0.5f},true,true),{0,0,0},"Conservative diagonal boundary tie");
            for(std::size_t pixel=0;pixel<atlas.size()/4;++pixel) atlas[pixel*4+3]=1;
            for(unsigned int z=0;z<4;++z) for(unsigned int y=0;y<2;++y) for(unsigned int x=0;x<4;++x) Plane(atlas,x,y,z,1);
            Expect(Draw(config,atlas,{1.5f,1,1.5f},{0,1,0}),{3,2,1},"Boundary-aligned conservative surface exit");
            helgi::Config thinCells=config;thinCells.Maximum.Y=0.4f;
            for(unsigned int z=0;z<4;++z) for(unsigned int y=0;y<2;++y) for(unsigned int x=0;x<4;++x) Plane(atlas,x,y,z,0.1f);
            Expect(Draw(thinCells,atlas,{1.5f,0.1f,1.5f},{0,1,0}),{3,2,1},"Anisotropic conservative surface exit");
            for(unsigned int z=0;z<4;++z) for(unsigned int y=0;y<2;++y) for(unsigned int x=0;x<4;++x) Plane(atlas,x,y,z,1);
            for(unsigned int z=0;z<4;++z) for(unsigned int x=0;x<4;++x) Plane(atlas,x,2,z,2.5f);
            Expect(Draw(config,atlas,{1.5f,1,1.5f},{0,1,0}),{0,0,0},"Distinct contiguous roof plane blocks surface exit");
            for(unsigned int z=0;z<4;++z) for(unsigned int x=0;x<4;++x) {
                Plane(atlas,x,2,z,1);
                atlas[((0*4+z)*16+x+4*2)*4+1]=-1;
                atlas[((1*4+z)*16+x+4*2)*4]=-1;
            }
            Expect(Draw(config,atlas,{1.5f,1,1.5f},{0,1,0}),{3,2,1},"Equivalent opposite-sign occupied plane");
            for(unsigned int z=0;z<4;++z) for(unsigned int x=0;x<4;++x) Plane(atlas,x,1,z,1,true);
            Expect(Draw(config,atlas,{1.5f,1,1.5f},{0,1,0}),{0,0,0},"Roof sharing receiver voxel conservatively blocks surface exit");
            // Already empty query points retain their exact CPU position; conservative surface exit is tested separately.
            helgi::VoxelGi solver(config);
            solver.SetContributor(1,1,{{{0,0.1f,0},{0,0.1f,4},{4,0.1f,0},{0.8f,0.15f,0.05f}},{{4,0.1f,0},{0,0.1f,4},{4,0.1f,4},{0.8f,0.15f,0.05f}}});
            solver.SetSun({{0,1,0},{5,5,5}});while(solver.GetStats().UpdatePending) solver.AdvanceUpdate(65536);
            bool lit=false;
            for(int x=1;x<=3;++x) for(int z=1;z<=3;++z) {
                helgi::Vec3 position{static_cast<float>(x),2.1f,static_cast<float>(z)},normal{0,-1,0};
                auto expected=solver.SampleIrradiance(position,normal);auto actual=Draw(config,solver.GetField().AtlasRgba,position,normal);
                Expect(actual,expected,"Published CPU and GPU sampling");lit=lit||actual.X>0;
            }
            if(!lit) throw std::runtime_error("CPU/GPU fixture produced no bounce signal.");
            // Compile all actual production entries with GI enabled, including the pearl path and multisample variants.
            std::string production=std::string(PearlSurfaceShader)+"\n#define HELENGINE_VOXEL_GI 1\n"+SceneGiShader+CameraToonGeometryShader+CameraToonCompositeShader;
            for(const auto& request:BuildCameraToonShaderRequests(production)) {
                std::vector<D3D_SHADER_MACRO> macros;for(const auto& macro:request.Macros) macros.push_back({macro.Name.c_str(),macro.Value.c_str()});macros.push_back({nullptr,nullptr});
                Microsoft::WRL::ComPtr<ID3DBlob> binary,error;HRESULT result=D3DCompile(request.Source.data(),request.Source.size(),"ProductionToonGi",macros.data(),nullptr,request.Entry.c_str(),request.Profile.c_str(),request.Flags,0,&binary,&error);
                if(FAILED(result) && error) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));Check(result);
            }
        }
    };
}

/// <summary>Runs offscreen transport-consumer contracts without opening or capturing any user window.</summary>
int main() {
    try {helengine::windows::SceneGiShaderTests tests;tests.Run();std::cout<<"Scene GI HDR, lobes, wall/window, CPU/GPU and production shader contracts passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
