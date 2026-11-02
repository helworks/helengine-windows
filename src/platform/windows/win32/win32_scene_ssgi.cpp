#include "platform/windows/win32/win32_scene_ssgi.hpp"
#include "platform/windows/win32/win32_scene_ssgi_shader.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
namespace helengine::windows {
    void Win32SceneSsgi::Check(HRESULT result,const char* operation) {
        if(FAILED(result)) throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(result));
    }
    Win32SceneSsgi::Win32SceneSsgi(ID3D11Device* device):Device(device) {
        if(!device) throw std::invalid_argument("Shared SSGI requires a device.");
        std::string source=std::string(SceneSsgiShader)+SceneSsgiCaptureShader;
        Compilation=std::make_unique<DirectX11ShaderCompilation>(std::vector<DirectX11ShaderRequest>{
            {source,"SharedSsgi","SsgiCaptureVS","vs_5_0",0,{}},{source,"SharedSsgi","SsgiCapturePS","ps_5_0",0,{}},
            {source,"SharedSsgi","SsgiFullscreenVS","vs_5_0",0,{}},{source,"SharedSsgi","SsgiTracePS","ps_5_0",0,{}}});
    }
    bool Win32SceneSsgi::TryPrepare() {
        if(Failure) std::rethrow_exception(Failure);
        if(Prepared) return true;
        try {
            DirectX11ShaderCompilation::Bytecodes code;if(!Compilation->TryGetBytecodes(code)) return false;
            Check(Device->CreateVertexShader(code[0]->GetBufferPointer(),code[0]->GetBufferSize(),nullptr,&CaptureVertex),"Create SSGI capture VS");
            Check(Device->CreatePixelShader(code[1]->GetBufferPointer(),code[1]->GetBufferSize(),nullptr,&CapturePixel),"Create SSGI capture PS");
            Check(Device->CreateVertexShader(code[2]->GetBufferPointer(),code[2]->GetBufferSize(),nullptr,&FullscreenVertex),"Create SSGI fullscreen VS");
            Check(Device->CreatePixelShader(code[3]->GetBufferPointer(),code[3]->GetBufferSize(),nullptr,&TracePixel),"Create SSGI trace PS");
            D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
            Check(Device->CreateInputLayout(elements,3,code[0]->GetBufferPointer(),code[0]->GetBufferSize(),&Layout),"Create SSGI mesh layout");
            D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=36*sizeof(float);buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;Check(Device->CreateBuffer(&buffer,nullptr,&Parameters),"Create SSGI parameters");
            D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;Check(Device->CreateDepthStencilState(&depth,&GeometryDepth),"Create SSGI capture depth");
            depth.DepthEnable=FALSE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;Check(Device->CreateDepthStencilState(&depth,&TraceDepth),"Create SSGI trace depth");
            D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;Check(Device->CreateRasterizerState(&raster,&Rasterizer),"Create SSGI rasterizer");
            Prepared=true;Compilation.reset();return true;
        } catch(...) {Failure=std::current_exception();throw;}
    }
    void Win32SceneSsgi::Resize(UINT width,UINT height) {
        if(Width==width && Height==height) return;
        for(unsigned index=0;index<4;++index) {
            Targets[index].Reset();Views[index].Reset();Textures[index].Reset();
            D3D11_TEXTURE2D_DESC texture{};texture.Width=width;texture.Height=height;texture.MipLevels=texture.ArraySize=1;texture.SampleDesc.Count=1;
            texture.Format=index<2?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            Check(Device->CreateTexture2D(&texture,nullptr,&Textures[index]),"Create SSGI HDR attachment");
            Check(Device->CreateRenderTargetView(Textures[index].Get(),nullptr,&Targets[index]),"Create SSGI target");
            Check(Device->CreateShaderResourceView(Textures[index].Get(),nullptr,&Views[index]),"Create SSGI source view");
        }
        Depth.Reset();D3D11_TEXTURE2D_DESC texture{};texture.Width=width;texture.Height=height;texture.MipLevels=texture.ArraySize=1;texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;texture.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;Check(Device->CreateTexture2D(&texture,nullptr,&depth),"Create SSGI depth texture");Check(Device->CreateDepthStencilView(depth.Get(),nullptr,&Depth),"Create SSGI depth target");Width=width;Height=height;
    }
    void Win32SceneSsgi::ClearBindings(ID3D11DeviceContext* context) {
        ID3D11ShaderResourceView* none[4]{};context->PSSetShaderResources(18,4,none);ID3D11Buffer* buffer=nullptr;context->PSSetConstantBuffers(10,1,&buffer);
    }
    void Win32SceneSsgi::BeginCapture(ID3D11DeviceContext* context,UINT width,UINT height,const std::array<float,36>& parameters) {
        if(!Prepared || !context || width==0 || height==0) throw std::logic_error("SSGI capture requires a prepared pipeline and positive viewport.");
        ClearBindings(context);
        const double scale=std::min({0.5,960.0/width,540.0/height});
        Resize(std::max(1u,static_cast<UINT>(std::round(width*scale))),std::max(1u,static_cast<UINT>(std::round(height*scale))));
        auto values=parameters;values[16]=static_cast<float>(Width);values[17]=static_cast<float>(Height);values[18]=1;
        context->UpdateSubresource(Parameters.Get(),0,nullptr,values.data(),0,0);
        const float zero[4]{};for(unsigned index=0;index<4;++index) context->ClearRenderTargetView(Targets[index].Get(),zero);
        ID3D11RenderTargetView* targets[]={Targets[0].Get(),Targets[1].Get(),Targets[2].Get()};context->OMSetRenderTargets(3,targets,Depth.Get());context->ClearDepthStencilView(Depth.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(Width),static_cast<float>(Height),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(Rasterizer.Get());context->OMSetDepthStencilState(GeometryDepth.Get(),0);context->OMSetBlendState(nullptr,nullptr,0xffffffff);
    }
    void Win32SceneSsgi::BindCaptureGeometry(ID3D11DeviceContext* context) {
        context->IASetInputLayout(Layout.Get());context->VSSetShader(CaptureVertex.Get(),nullptr,0);context->PSSetShader(CapturePixel.Get(),nullptr,0);
        auto* parameters=Parameters.Get();context->PSSetConstantBuffers(10,1,&parameters);
        ID3D11Buffer* noVoxel=nullptr;ID3D11ShaderResourceView* noField=nullptr;context->PSSetConstantBuffers(9,1,&noVoxel);context->PSSetShaderResources(17,1,&noField);
    }
    void Win32SceneSsgi::EndCapture(ID3D11DeviceContext* context) {
        context->OMSetRenderTargets(0,nullptr,nullptr);context->OMSetDepthStencilState(TraceDepth.Get(),0);context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(FullscreenVertex.Get(),nullptr,0);context->PSSetShader(TracePixel.Get(),nullptr,0);
        ID3D11ShaderResourceView* source[]={Views[0].Get(),Views[1].Get(),Views[2].Get()};context->PSSetShaderResources(18,3,source);auto* target=Targets[3].Get();context->OMSetRenderTargets(1,&target,nullptr);auto* parameters=Parameters.Get();context->PSSetConstantBuffers(10,1,&parameters);context->Draw(3,0);context->OMSetRenderTargets(0,nullptr,nullptr);ClearBindings(context);
    }
    void Win32SceneSsgi::BindReceiver(ID3D11DeviceContext* context) {
        auto* parameters=Parameters.Get();context->PSSetConstantBuffers(10,1,&parameters);ID3D11ShaderResourceView* source[]={Views[0].Get(),Views[1].Get(),Views[2].Get(),Views[3].Get()};context->PSSetShaderResources(18,4,source);
        ID3D11Buffer* noVoxel=nullptr;ID3D11ShaderResourceView* noField=nullptr;context->PSSetConstantBuffers(9,1,&noVoxel);context->PSSetShaderResources(17,1,&noField);
    }
}
