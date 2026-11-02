#include "platform/windows/win32/win32_scene_gi_usage.hpp"
#include "platform/windows/win32/win32_scene_gi_shader.hpp"
#include "platform/windows/win32/win32_scene_gi_usage_shader.hpp"
#include <d3dcompiler.h>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace helengine::windows {
    namespace {
        /// <summary>Preserves native resource and context-state failures instead of displaying a stale mask.</summary>
        void CheckUsage(HRESULT result,const char* operation) {if(FAILED(result)) throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(result));}
        /// <summary>Compiles the production sampler with bounded reader recording only for the explicit diagnostic.</summary>
        ComPtr<ID3DBlob> CompileUsage(const char* entry,const char* profile) {
            std::string source=std::string("#define HELENGINE_GI_TRACK_READERS 1\n")+SceneGiShader+SceneGiUsageShader;ComPtr<ID3DBlob> code,error;
            HRESULT result=D3DCompile(source.data(),source.size(),"VoxelGiReaders",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
            if(FAILED(result) && error) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));CheckUsage(result,"Compile voxel reader shader");return code;
        }
    }
    Win32SceneGiUsage::Win32SceneGiUsage(ID3D11Device* device,std::uint32_t cellCount):Device(device) {
        if(!device || !cellCount || cellCount>262144) throw std::invalid_argument("Voxel usage requires a device and a valid bounded field cell count.");
        ComPtr<ID3D11Device1> device1;CheckUsage(Device.As(&device1),"Query diagnostic context-state device");D3D_FEATURE_LEVEL level=device->GetFeatureLevel(),selected;
        CheckUsage(device1->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selected,&DiagnosticState),"Create isolated reader context state");
        std::vector<UINT> zeros(cellCount,0);D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=cellCount*sizeof(UINT);buffer.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;buffer.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;buffer.StructureByteStride=sizeof(UINT);D3D11_SUBRESOURCE_DATA initial{zeros.data(),0,0};
        CheckUsage(device->CreateBuffer(&buffer,&initial,&Mask),"Create reader usage mask");CheckUsage(device->CreateUnorderedAccessView(Mask.Get(),nullptr,&WriteView),"Create reader usage UAV");CheckUsage(device->CreateShaderResourceView(Mask.Get(),nullptr,&ReadView),"Create reader usage SRV");
        auto vs=CompileUsage("ReaderVS","vs_5_0"),depth=CompileUsage("ReaderDepthPS","ps_5_0"),reader=CompileUsage("ReaderUsagePS","ps_5_0");
        CheckUsage(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&Vertex),"Create reader vertex shader");CheckUsage(device->CreatePixelShader(depth->GetBufferPointer(),depth->GetBufferSize(),nullptr,&DepthPixel),"Create reader depth shader");CheckUsage(device->CreatePixelShader(reader->GetBufferPointer(),reader->GetBufferSize(),nullptr,&ReaderPixel),"Create reader usage shader");
        D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};CheckUsage(device->CreateInputLayout(elements,3,vs->GetBufferPointer(),vs->GetBufferSize(),&Layout),"Create reader mesh layout");
        D3D11_DEPTH_STENCIL_DESC state{};state.DepthEnable=TRUE;state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;state.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;CheckUsage(device->CreateDepthStencilState(&state,&WriteDepth),"Create reader visibility depth");state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;state.DepthFunc=D3D11_COMPARISON_EQUAL;CheckUsage(device->CreateDepthStencilState(&state,&EqualDepth),"Create reader equality depth");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.FrontCounterClockwise=TRUE;raster.DepthClipEnable=TRUE;CheckUsage(device->CreateRasterizerState(&raster,&Rasterizer),"Create reader camera rasterizer");raster.MultisampleEnable=TRUE;CheckUsage(device->CreateRasterizerState(&raster,&MultisampleRasterizer),"Create multisample reader camera rasterizer");
    }
    Win32SceneGiUsage::~Win32SceneGiUsage() {End();}
    void Win32SceneGiUsage::Resize(UINT width,UINT height,UINT samples) {
        if(width==Width && height==Height && samples==Samples) return;
        if(!width || !height || (samples!=1 && samples!=4)) throw std::invalid_argument("Reader capture requires the actual positive camera viewport and one or four samples.");
        D3D11_TEXTURE2D_DESC texture{};texture.Width=width;texture.Height=height;texture.MipLevels=texture.ArraySize=1;texture.SampleDesc.Count=samples;texture.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;texture.BindFlags=D3D11_BIND_DEPTH_STENCIL;ComPtr<ID3D11Texture2D> depth;ComPtr<ID3D11DepthStencilView> view;
        CheckUsage(Device->CreateTexture2D(&texture,nullptr,&depth),"Create full-resolution reader depth");CheckUsage(Device->CreateDepthStencilView(depth.Get(),nullptr,&view),"Create full-resolution reader depth view");Depth=std::move(view);Width=width;Height=height;Samples=samples;
    }
    void Win32SceneGiUsage::Begin(ID3D11DeviceContext* context,ID3D11ShaderResourceView* atlas,ID3D11Buffer* parameters) {
        if(ActiveContext || !context || !atlas || !parameters) throw std::invalid_argument("Reader Begin requires an idle pass and the current complete field bindings.");
        CheckUsage(context->QueryInterface(IID_PPV_ARGS(&ActiveContext)),"Query isolated reader context");ActiveContext->SwapDeviceContextState(DiagnosticState.Get(),&PreviousState);Atlas=atlas;Field=parameters;
        ID3D11UnorderedAccessView* none=nullptr;ActiveContext->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,1,&none,nullptr);const UINT zero[4]{};ActiveContext->ClearUnorderedAccessViewUint(WriteView.Get(),zero);Reading=false;
    }
    void Win32SceneGiUsage::BeginCamera(float width,float height,UINT samples) {
        if(!ActiveContext) throw std::logic_error("Reader camera capture requires Begin.");
        if(!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 || width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) throw std::invalid_argument("Reader capture requires a finite positive camera viewport within device dimensions.");
        Resize(static_cast<UINT>(std::ceil(width)),static_cast<UINT>(std::ceil(height)),samples);Reading=false;
        ID3D11UnorderedAccessView* none=nullptr;ActiveContext->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,Depth.Get(),0,1,&none,nullptr);ActiveContext->ClearDepthStencilView(Depth.Get(),D3D11_CLEAR_DEPTH,1,0);ActiveContext->OMSetDepthStencilState(WriteDepth.Get(),0);ActiveContext->OMSetBlendState(nullptr,nullptr,0xffffffff);ActiveContext->RSSetState(samples==4?MultisampleRasterizer.Get():Rasterizer.Get());
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};ActiveContext->RSSetViewports(1,&viewport);ActiveContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    void Win32SceneGiUsage::BeginReaders() {
        if(!ActiveContext || !Depth) throw std::logic_error("Reader tracking requires a completed camera depth prepass.");Reading=true;auto* output=WriteView.Get();ActiveContext->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,Depth.Get(),0,1,&output,nullptr);ActiveContext->OMSetDepthStencilState(EqualDepth.Get(),0);
    }
    void Win32SceneGiUsage::BindGeometry() {
        if(!ActiveContext || !Depth) throw std::logic_error("Reader geometry requires an active camera capture.");ActiveContext->IASetInputLayout(Layout.Get());ActiveContext->VSSetShader(Vertex.Get(),nullptr,0);ActiveContext->PSSetShader(Reading?ReaderPixel.Get():DepthPixel.Get(),nullptr,0);auto* parameters=Field.Get();auto* atlas=Atlas.Get();ActiveContext->PSSetConstantBuffers(9,1,&parameters);ActiveContext->PSSetShaderResources(17,1,&atlas);
    }
    void Win32SceneGiUsage::End() {
        if(!ActiveContext) return;
        ActiveContext->SwapDeviceContextState(PreviousState.Get(),DiagnosticState.ReleaseAndGetAddressOf());PreviousState.Reset();ActiveContext.Reset();Atlas.Reset();Field.Reset();Reading=false;
    }
    ID3D11ShaderResourceView* Win32SceneGiUsage::GetView() const {return ReadView.Get();}
}
