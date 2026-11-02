#include "platform/windows/win32/win32_scene_gi_spheres.hpp"
#include "platform/windows/win32/win32_scene_gi_spheres_shader.hpp"
#include <d3dcompiler.h>
#include <cstring>
#include <stdexcept>
#include <string>
using Microsoft::WRL::ComPtr;
namespace helengine::windows {
    namespace {
        /// <summary>Preserves native diagnostic initialization failures rather than silently skipping the requested visualization.</summary>
        void CheckSpheres(HRESULT result,const char* operation) {if(FAILED(result)) throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(result));}
        /// <summary>Compiles the same bounded shader used by production and headless WARP validation.</summary>
        ComPtr<ID3DBlob> CompileSpheres(const char* entry,const char* profile) {
            ComPtr<ID3DBlob> code,error;
            HRESULT result=D3DCompile(SceneGiSpheresShader,std::strlen(SceneGiSpheresShader),"VoxelGiSpheres",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
            if(FAILED(result) && error) throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));
            CheckSpheres(result,"Compile voxel GI spheres");return code;
        }
    }
    Win32SceneGiSpheres::Win32SceneGiSpheres(ID3D11Device* device) {
        if(!device) throw std::invalid_argument("Voxel GI sphere diagnostics require a device.");
        auto vs=CompileSpheres("ProbeVS","vs_5_0"),ps=CompileSpheres("ProbePS","ps_5_0");
        CheckSpheres(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&Vertex),"Create probe VS");
        CheckSpheres(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&Pixel),"Create probe PS");
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=16*sizeof(float);buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;CheckSpheres(device->CreateBuffer(&buffer,nullptr,&Camera),"Create probe camera constants");
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=TRUE;depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;depth.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;CheckSpheres(device->CreateDepthStencilState(&depth,&Depth),"Create probe depth");
        D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;CheckSpheres(device->CreateBlendState(&blend,&Blend),"Create probe blend");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;raster.MultisampleEnable=TRUE;CheckSpheres(device->CreateRasterizerState(&raster,&Rasterizer),"Create probe rasterizer");
    }
    void Win32SceneGiSpheres::Draw(ID3D11DeviceContext* context,const float* viewProjection,ID3D11ShaderResourceView* atlas,ID3D11Buffer* fieldParameters,std::uint32_t cellCount,ID3D11ShaderResourceView* usage,bool enabled) {
        if(!enabled) return;
        if(!context || !viewProjection || !atlas || !fieldParameters || !cellCount || !usage) throw std::invalid_argument("Voxel sphere drawing requires the published atlas, matching receiver usage mask, parameters, camera and a positive cell count.");
        ComPtr<ID3D11InputLayout> layout;D3D11_PRIMITIVE_TOPOLOGY topology;context->IAGetInputLayout(&layout);context->IAGetPrimitiveTopology(&topology);
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;ComPtr<ID3D11GeometryShader> geometry;
        ID3D11ClassInstance* vsClasses[256]{};ID3D11ClassInstance* psClasses[256]{};ID3D11ClassInstance* gsClasses[256]{};UINT vsCount=256,psCount=256,gsCount=256;
        context->VSGetShader(&vertex,vsClasses,&vsCount);context->PSGetShader(&pixel,psClasses,&psCount);context->GSGetShader(&geometry,gsClasses,&gsCount);
        ComPtr<ID3D11Buffer> vsField,psField,vsCamera;context->VSGetConstantBuffers(9,1,&vsField);context->PSGetConstantBuffers(9,1,&psField);context->VSGetConstantBuffers(11,1,&vsCamera);
        ComPtr<ID3D11ShaderResourceView> previousUsage;context->VSGetShaderResources(22,1,&previousUsage);
        ComPtr<ID3D11ShaderResourceView> vsAtlas,psAtlas;context->VSGetShaderResources(17,1,&vsAtlas);context->PSGetShaderResources(17,1,&psAtlas);
        ComPtr<ID3D11RasterizerState> raster;context->RSGetState(&raster);ComPtr<ID3D11DepthStencilState> depth;UINT stencil;context->OMGetDepthStencilState(&depth,&stencil);ComPtr<ID3D11BlendState> blend;FLOAT factors[4];UINT mask;context->OMGetBlendState(&blend,factors,&mask);
        context->UpdateSubresource(Camera.Get(),0,nullptr,viewProjection,0,0);auto* camera=Camera.Get();
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(Vertex.Get(),nullptr,0);context->PSSetShader(Pixel.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        context->VSSetConstantBuffers(9,1,&fieldParameters);context->PSSetConstantBuffers(9,1,&fieldParameters);context->VSSetConstantBuffers(11,1,&camera);context->VSSetShaderResources(17,1,&atlas);context->PSSetShaderResources(17,1,&atlas);context->VSSetShaderResources(22,1,&usage);
        context->RSSetState(Rasterizer.Get());context->OMSetDepthStencilState(Depth.Get(),0);context->OMSetBlendState(Blend.Get(),nullptr,0xffffffff);context->DrawInstanced(192,cellCount,0,0);
        context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(topology);context->VSSetShader(vertex.Get(),vsClasses,vsCount);context->PSSetShader(pixel.Get(),psClasses,psCount);context->GSSetShader(geometry.Get(),gsClasses,gsCount);
        for(UINT index=0;index<vsCount;++index) vsClasses[index]->Release();for(UINT index=0;index<psCount;++index) psClasses[index]->Release();for(UINT index=0;index<gsCount;++index) gsClasses[index]->Release();
        auto* binding=vsField.Get();context->VSSetConstantBuffers(9,1,&binding);binding=psField.Get();context->PSSetConstantBuffers(9,1,&binding);binding=vsCamera.Get();context->VSSetConstantBuffers(11,1,&binding);
        auto* usageBinding=previousUsage.Get();context->VSSetShaderResources(22,1,&usageBinding);
        auto* view=vsAtlas.Get();context->VSSetShaderResources(17,1,&view);view=psAtlas.Get();context->PSSetShaderResources(17,1,&view);context->RSSetState(raster.Get());context->OMSetDepthStencilState(depth.Get(),stencil);context->OMSetBlendState(blend.Get(),factors,mask);
    }
}
