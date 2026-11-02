#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
namespace helengine::windows {
    /// <summary>Restores every D3D11 binding touched by shared scene capture, including exceptional exits.</summary>
    class Win32SceneInkGpuState {
        /// <summary>Borrows the immediate context for this bounded render-thread scope.</summary>
        ID3D11DeviceContext* Context;
        /// <summary>Retains the eight output attachments without changing presentation ownership.</summary>
        ID3D11RenderTargetView* Targets[8]{};
        /// <summary>Retains the original depth attachment.</summary>
        ID3D11DepthStencilView* Depth=nullptr;
        /// <summary>Retains each pixel resource binding that authored material drawing can replace.</summary>
        ID3D11ShaderResourceView* Views[26]{};
        /// <summary>Retains engine and authored pixel constant buffers.</summary>
        ID3D11Buffer* PixelBuffers[14]{};
        /// <summary>Retains the transform buffer consumed by geometry projection.</summary>
        ID3D11Buffer* VertexBuffers[14]{};
        /// <summary>Retains authored texture and reserved engine samplers.</summary>
        ID3D11SamplerState* Samplers[16]{};
        /// <summary>Retains the active vertex shader without any scene-side mutation.</summary>
        Microsoft::WRL::ComPtr<ID3D11VertexShader> Vertex;
        /// <summary>Retains the active pixel shader before material overrides.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> Pixel;
        /// <summary>Retains the original standard mesh input layout.</summary>
        Microsoft::WRL::ComPtr<ID3D11InputLayout> Layout;
        /// <summary>Retains the rasterizer including scissor enablement and culling.</summary>
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
        /// <summary>Retains blend equations and alpha behavior.</summary>
        Microsoft::WRL::ComPtr<ID3D11BlendState> Blend;
        /// <summary>Retains depth tests and writes for the caller.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> DepthState;
        /// <summary>Retains the original indexed mesh buffer.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Indices;
        /// <summary>Retains both ordinary and lightmap vertex streams.</summary>
        ID3D11Buffer* Vertices[2]{};
        /// <summary>Retains the original stream element sizes.</summary>
        UINT Strides[2]{},Offsets[2]{};
        /// <summary>Retains index layout and element offset.</summary>
        DXGI_FORMAT IndexFormat=DXGI_FORMAT_UNKNOWN;
        /// <summary>Retains the byte offset of indexed geometry.</summary>
        UINT IndexOffset=0;
        /// <summary>Retains the original mesh primitive interpretation.</summary>
        D3D11_PRIMITIVE_TOPOLOGY Topology=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        /// <summary>Retains all caller viewport rectangles.</summary>
        D3D11_VIEWPORT Viewports[16]{};
        /// <summary>Tracks the exact caller viewport count.</summary>
        UINT ViewportCount=16;
        /// <summary>Retains the constant blend factor.</summary>
        float BlendFactor[4]{};
        /// <summary>Retains multisample write eligibility.</summary>
        UINT SampleMask=0;
        /// <summary>Retains stencil reference independently from its state object.</summary>
        UINT Stencil=0;
        /// <summary>Releases raw COM references acquired in one native Get call.</summary>
        template<class T,size_t N> static void Release(T* (&items)[N]) {for(auto*& item:items) {if(item) item->Release();item=nullptr;}}
    public:
        /// <summary>Snapshots only native GPU state; no camera queues, UI state or scene transforms are modified.</summary>
        explicit Win32SceneInkGpuState(ID3D11DeviceContext* context):Context(context) {
            Context->OMGetRenderTargets(8,Targets,&Depth);Context->PSGetShaderResources(0,26,Views);Context->PSGetConstantBuffers(0,14,PixelBuffers);Context->VSGetConstantBuffers(0,14,VertexBuffers);Context->PSGetSamplers(0,16,Samplers);
            Context->VSGetShader(&Vertex,nullptr,nullptr);Context->PSGetShader(&Pixel,nullptr,nullptr);Context->IAGetInputLayout(&Layout);Context->RSGetState(&Rasterizer);Context->RSGetViewports(&ViewportCount,Viewports);
            Context->OMGetBlendState(&Blend,BlendFactor,&SampleMask);Context->OMGetDepthStencilState(&DepthState,&Stencil);Context->IAGetVertexBuffers(0,2,Vertices,Strides,Offsets);Context->IAGetIndexBuffer(&Indices,&IndexFormat,&IndexOffset);Context->IAGetPrimitiveTopology(&Topology);
        }
        /// <summary>Unbinds capture outputs before restoring sampled resources and releases all borrowed COM references.</summary>
        ~Win32SceneInkGpuState() {
            ID3D11ShaderResourceView* emptyViews[26]{};
            Context->PSSetShaderResources(0,26,emptyViews);
            Context->OMSetRenderTargets(8,Targets,Depth);Context->PSSetShaderResources(0,26,Views);Context->PSSetConstantBuffers(0,14,PixelBuffers);Context->VSSetConstantBuffers(0,14,VertexBuffers);Context->PSSetSamplers(0,16,Samplers);
            Context->VSSetShader(Vertex.Get(),nullptr,0);Context->PSSetShader(Pixel.Get(),nullptr,0);Context->IASetInputLayout(Layout.Get());Context->RSSetState(Rasterizer.Get());Context->RSSetViewports(ViewportCount,Viewports);
            Context->OMSetBlendState(Blend.Get(),BlendFactor,SampleMask);Context->OMSetDepthStencilState(DepthState.Get(),Stencil);Context->IASetVertexBuffers(0,2,Vertices,Strides,Offsets);Context->IASetIndexBuffer(Indices.Get(),IndexFormat,IndexOffset);Context->IASetPrimitiveTopology(Topology);
            Release(Targets);Release(Views);Release(PixelBuffers);Release(VertexBuffers);Release(Samplers);Release(Vertices);if(Depth) Depth->Release();
        }
        /// <summary>Prevents copying a scope that owns native Get references.</summary>
        Win32SceneInkGpuState(const Win32SceneInkGpuState&)=delete;
        /// <summary>Prevents assigning independently restoring state scopes.</summary>
        Win32SceneInkGpuState& operator=(const Win32SceneInkGpuState&)=delete;
    };
}
