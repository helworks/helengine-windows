#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

/// <summary>Exercises production contour tiles with owned integer MSAA attachments and no presentation surface.</summary>
class CameraToonOutlineTileTests {
    /// <summary>Owns the offscreen hardware device used by both supported sample counts.</summary>
    Microsoft::WRL::ComPtr<ID3D11Device> Device;
    /// <summary>Owns the immediate context used only by synthetic fixture draws.</summary>
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
    /// <summary>Matches the production eleven-vector toon constant-buffer layout.</summary>
    Microsoft::WRL::ComPtr<ID3D11Buffer> Constants;
    /// <summary>Uses a partial final tile to expose unsafe edge reads and false viewport-border contours.</summary>
    static constexpr UINT Width=193, Height=161, Columns=7, Rows=6;

    /// <summary>Preserves the failing Direct3D operation in the regression result.</summary>
    static void Check(HRESULT result,const char* operation) { if(FAILED(result)) throw std::runtime_error(operation); }
    /// <summary>Reports a precise violated contour invariant.</summary>
    static void Require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
    /// <summary>Compiles exact shipped contour helpers with only synthetic identity/probe entry points appended.</summary>
    Microsoft::WRL::ComPtr<ID3DBlob> Compile(const char* entry,const char* target,UINT samples) {
        std::string source=std::string(helengine::windows::PearlSurfaceShader)+helengine::windows::CameraToonGeometryShader+helengine::windows::CameraToonCompositeShader+R"(
uint FixturePS(float4 position:SV_POSITION,uint sampleIndex:SV_SampleIndex):SV_Target {
    int2 pixel=int2(position.xy);
    if(Style.z==0) return 7;
    if(Style.z==1) return pixel.x<64 ? 7 : 8;
    if(Style.z==2) return pixel.x<64 ? 7 : 0;
    if(Style.z==3) return all(pixel==int2(80,80)) && sampleIndex==2 ? 8 : 7;
    if(Style.z==5) return pixel.x>=82 ? 8 : 7;
    if(Style.z==6) return 7;
    if(Style.z==7) return pixel.x>=79 ? 7 : 0;
    if(Style.z==8) return pixel.x>=79 && pixel.x<=81 ? 7 : 0;
    return 0;
}
float HaloPS(float4 position:SV_POSITION):SV_Target { return HasNearbyOutline(int2(position.xy)*32); }
float4 NormalFixturePS(float4 position:SV_POSITION):SV_Target {
    if(Style.z==7) return float4(normalize(float3((position.x-80.5)*0.02,0,1)),1);
    return position.x<80 ? float4(0,0,1,1) : float4(1,0,0,1);
}
float CoverageFixturePS():SV_Target { return 1; }
float CreaseVisibilityPS():SV_Target { bool found;return NormalCreaseStroke(int2(Style.z==8 ? 81 : 82,80),int2(79,80),int2(1,0),found); }
)";
        std::string count=std::to_string(samples); D3D_SHADER_MACRO macros[]={{"TOON_SAMPLES",count.c_str()},{nullptr,nullptr}};
        Microsoft::WRL::ComPtr<ID3DBlob> binary,errors;
        HRESULT result=D3DCompile(source.data(),source.size(),"ProductionOutlineTiles",macros,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,&binary,&errors);
        if(FAILED(result)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Outline shader compilation failed");
        return binary;
    }
    /// <summary>Creates one explicitly sized owned render target, retaining shader readability where required.</summary>
    Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture(UINT width,UINT height,UINT samples,DXGI_FORMAT format) {
        D3D11_TEXTURE2D_DESC description{}; description.Width=width;description.Height=height;description.MipLevels=description.ArraySize=1;
        description.Format=format;description.SampleDesc.Count=samples;description.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; Check(Device->CreateTexture2D(&description,nullptr,&texture),"Create outline fixture texture"); return texture;
    }
    /// <summary>Draws a fullscreen triangle into the exact supplied target extent.</summary>
    void Draw(ID3D11Texture2D* texture,ID3D11PixelShader* shader,UINT width,UINT height) {
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;Check(Device->CreateRenderTargetView(texture,nullptr,&target),"Create outline fixture RTV");
        auto* output=target.Get();Context->OMSetRenderTargets(1,&output,nullptr);Context->PSSetShader(shader,nullptr,0);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};Context->RSSetViewports(1,&viewport);Context->Draw(3,0);
        Context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    /// <summary>Copies scalar tile/probe results from owned GPU storage without reading any window.</summary>
    std::vector<float> Read(ID3D11Texture2D* texture) {
        D3D11_TEXTURE2D_DESC description{};texture->GetDesc(&description);description.BindFlags=0;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;Check(Device->CreateTexture2D(&description,nullptr,&staging),"Create tile readback");Context->CopyResource(staging.Get(),texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read outline tiles");std::vector<float> values(Columns*Rows);
        for(UINT row=0;row<Rows;++row) std::memcpy(values.data()+row*Columns,static_cast<const char*>(mapped.pData)+row*mapped.RowPitch,Columns*sizeof(float));
        Context->Unmap(staging.Get(),0);return values;
    }

    /// <summary>Rejects foreground bleed and smooth boundary gradients while preserving a thin sharp crease with one valid flat neighbor.</summary>
    void TestCreaseVisibility(UINT samples) {
        auto vs=Compile("CompositeVS","vs_5_0",samples),ids=Compile("FixturePS","ps_5_0",samples),normals=Compile("NormalFixturePS","ps_5_0",samples);
        auto coverage=Compile("CoverageFixturePS","ps_5_0",samples),probe=Compile("CreaseVisibilityPS","ps_5_0",samples);
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;Check(Device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Create crease visibility vertex shader");
        Microsoft::WRL::ComPtr<ID3D11PixelShader> identityShader,normalShader,coverageShader,probeShader;
        Check(Device->CreatePixelShader(ids->GetBufferPointer(),ids->GetBufferSize(),nullptr,&identityShader),"Create foreground identity fixture");
        Check(Device->CreatePixelShader(normals->GetBufferPointer(),normals->GetBufferSize(),nullptr,&normalShader),"Create resolved face normal fixture");
        Check(Device->CreatePixelShader(coverage->GetBufferPointer(),coverage->GetBufferSize(),nullptr,&coverageShader),"Create owned geometry coverage fixture");
        Check(Device->CreatePixelShader(probe->GetBufferPointer(),probe->GetBufferSize(),nullptr,&probeShader),"Create production crease visibility probe");
        Context->VSSetShader(vertex.Get(),nullptr,0);Context->IASetInputLayout(nullptr);Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto parts=Texture(Width,Height,samples,DXGI_FORMAT_R32_UINT),normal=Texture(Width,Height,1,DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto covered=Texture(Width,Height,1,DXGI_FORMAT_R32_FLOAT),output=Texture(Columns,Rows,1,DXGI_FORMAT_R32_FLOAT);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> partView,normalView,coverageView;
        Check(Device->CreateShaderResourceView(parts.Get(),nullptr,&partView),"Create foreground part view");
        Check(Device->CreateShaderResourceView(normal.Get(),nullptr,&normalView),"Create crease normal view");
        Check(Device->CreateShaderResourceView(covered.Get(),nullptr,&coverageView),"Create crease coverage view");
        std::array<float,44> values{};values[1]=6;values[4]=Width;values[5]=Height;values[38]=values[39]=1;values[40]=0.70710678f;values[41]=1;
        auto* buffer=Constants.Get();Context->PSSetConstantBuffers(8,1,&buffer);
        Context->UpdateSubresource(Constants.Get(),0,nullptr,values.data(),0,0);
        Draw(normal.Get(),normalShader.Get(),Width,Height);Draw(covered.Get(),coverageShader.Get(),Width,Height);
        for(int mode=0;mode<4;++mode) {
            ID3D11ShaderResourceView* empty=nullptr;Context->PSSetShaderResources(9,1,&empty);Context->PSSetShaderResources(15,1,&empty);
            values[2]=mode==0 ? 6.0f : mode==1 ? 5.0f : mode==2 ? 7.0f : 8.0f;
            values[40]=mode==2 ? 0.99999848f : 0.70710678f;Context->UpdateSubresource(Constants.Get(),0,nullptr,values.data(),0,0);
            Draw(normal.Get(),normalShader.Get(),Width,Height);
            Draw(parts.Get(),identityShader.Get(),Width,Height);
            auto* view=partView.Get();Context->PSSetShaderResources(9,1,&view);view=normalView.Get();Context->PSSetShaderResources(15,1,&view);
            view=coverageView.Get();Context->PSSetShaderResources(10,1,&view);Draw(output.Get(),probeShader.Get(),Columns,Rows);
            auto result=Read(output.Get());
            if(mode==0) Require(result[0]>0.99f,"Visibility control lost the physical six-pixel crease footprint");
            else if(mode==1) Require(result[0]<0.0001f,"A visible crease extended through another part's foreground pixel");
            else if(mode==2) Require(result[0]<0.0001f,"A continuous smooth normal gradient near a part boundary received a crease");
            else Require(result[0]>0.99f,"A thin sharp normal crease lost its valid one-sided flat neighbor evidence");
        }
        ID3D11ShaderResourceView* empty=nullptr;Context->PSSetShaderResources(9,1,&empty);Context->PSSetShaderResources(10,1,&empty);Context->PSSetShaderResources(15,1,&empty);
        std::cout << "Crease foreground visibility, smooth boundary and thin edge " << samples << "x passed.\n";
    }
    /// <summary>Checks uniform walls, neighboring wall identities, silhouettes, minority MSAA samples and the bounded tile halo.</summary>
    void TestSamples(UINT samples) {
        auto vs=Compile("CompositeVS","vs_5_0",samples), fixture=Compile("FixturePS","ps_5_0",samples), tiles=Compile("OutlineTilePS","ps_5_0",samples), halo=Compile("HaloPS","ps_5_0",samples);
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;Microsoft::WRL::ComPtr<ID3D11PixelShader> fixtureShader,tileShader,haloShader;
        Check(Device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Create contour vertex shader");
        Check(Device->CreatePixelShader(fixture->GetBufferPointer(),fixture->GetBufferSize(),nullptr,&fixtureShader),"Create identity fixture shader");
        Check(Device->CreatePixelShader(tiles->GetBufferPointer(),tiles->GetBufferSize(),nullptr,&tileShader),"Create production tile shader");
        Check(Device->CreatePixelShader(halo->GetBufferPointer(),halo->GetBufferSize(),nullptr,&haloShader),"Create production halo probe");
        Context->VSSetShader(vertex.Get(),nullptr,0);Context->IASetInputLayout(nullptr);Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto parts=Texture(Width,Height,samples,DXGI_FORMAT_R32_UINT), mask=Texture(Columns,Rows,1,DXGI_FORMAT_R32_FLOAT), probe=Texture(Columns,Rows,1,DXGI_FORMAT_R32_FLOAT);
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> partView,maskView;Check(Device->CreateShaderResourceView(parts.Get(),nullptr,&partView),"Create part view");Check(Device->CreateShaderResourceView(mask.Get(),nullptr,&maskView),"Create tile view");
        for(int mode=0;mode<5;++mode) {
            std::array<float,44> constants{};constants[0]=16;constants[1]=16;constants[2]=static_cast<float>(mode);constants[4]=Width;constants[5]=Height;constants[39]=1;
            Context->UpdateSubresource(Constants.Get(),0,nullptr,constants.data(),0,0);auto* buffer=Constants.Get();Context->PSSetConstantBuffers(8,1,&buffer);
            ID3D11ShaderResourceView* empty=nullptr;Context->PSSetShaderResources(9,1,&empty);Context->PSSetShaderResources(16,1,&empty);
            Draw(parts.Get(),fixtureShader.Get(),Width,Height);auto* input=partView.Get();Context->PSSetShaderResources(9,1,&input);Draw(mask.Get(),tileShader.Get(),Columns,Rows);
            auto values=Read(mask.Get());
            for(UINT y=0;y<Rows;++y) for(UINT x=0;x<Columns;++x) {
                bool expected=(mode==1 || mode==2) ? x==1 : mode==3 && samples==4 && x==2 && y==2;
                Require((values[y*Columns+x]>0)==expected,mode==0 ? "Uniform fullscreen wall activated contour searches" : "Contour tile missed an interface or marked a uniform/border tile");
            }
            if(mode==1 || mode==2) {
                input=maskView.Get();Context->PSSetShaderResources(16,1,&input);Draw(probe.Get(),haloShader.Get(),Columns,Rows);values=Read(probe.Get());
                for(UINT y=0;y<Rows;++y) for(UINT x=0;x<Columns;++x) Require((values[y*Columns+x]>0)==(x<=2),"Contour halo missed adjacent tiles or reached uniform remote walls");
            }
        }
        std::cout << "Outline tiles " << samples << "x: uniform culling, wall corners, silhouettes, MSAA and halo passed.\n";
    }
public:
    /// <summary>Initializes a private hardware context with rasterization matching the supported production attachments.</summary>
    CameraToonOutlineTileTests() {
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&Device,nullptr,&Context),"Create offscreen outline device");
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=176;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;Check(Device->CreateBuffer(&buffer,nullptr,&Constants),"Create outline constants");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.MultisampleEnable=TRUE;raster.DepthClipEnable=TRUE;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> state;Check(Device->CreateRasterizerState(&raster,&state),"Create contour rasterizer");Context->RSSetState(state.Get());
    }
    /// <summary>Runs each identity invariant for single-sample and four-sample part attachments.</summary>
    void Run() { TestSamples(1);TestCreaseVisibility(1);TestSamples(4);TestCreaseVisibility(4); }
};

/// <summary>Runs only owned synthetic GPU fixtures and preserves the first failed invariant.</summary>
int main() { try { CameraToonOutlineTileTests tests;tests.Run();return 0; }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;} }
