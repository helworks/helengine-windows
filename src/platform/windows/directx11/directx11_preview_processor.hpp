#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include "directx11_texture_preview.hpp"
#include "directx11_shader_compilation.hpp"

namespace helengine::windows {
    /// Shares an engine-owned GPU copy/reduction pass across authorized external texture previews on one immediate context.
    class DirectX11PreviewProcessor {
        /// Uses the common backend shader loader without retaining a context in any worker.
        std::unique_ptr<DirectX11ShaderCompilation> ShaderCompilation;
        /// Retains device setup failures rather than recreating partially initialized resources.
        std::exception_ptr PipelineFailure;
        /// Gates GPU processing until both shaders and all fixed resources are ready.
        bool PipelinePrepared=false;
        /// Retains the common resource device.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// Retains the immediate context used on the renderer's owning thread.
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> Context;
        /// Isolates this utility's shader bindings from all scene cameras and outputs.
        Microsoft::WRL::ComPtr<ID3DDeviceContextState> State;
        /// Generates the full-screen triangle without per-source vertex buffers.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
        /// Samples unlit color and alpha without applying composition blending.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
        /// Supplies content crop and valid pixel-center limits.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Constants;
        /// Shares linear sampling across all previews.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> Sampler;
        /// Disables culling for the generated triangle.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
        /// Rejects incompatible sources, devices and dimensions before modifying graphics state.
        D3D11_TEXTURE2D_DESC Validate(DirectX11TexturePreview& preview,ID3D11Texture2D* source,UINT width,UINT height) const;
        /// Reports the original HRESULT for failed graphics initialization or processing.
        static void Check(HRESULT result,const char* operation);
    public:
        /// Schedules common backend shader loading and creates isolated state on a matching context without opening a window.
        DirectX11PreviewProcessor(ID3D11Device* device,ID3D11DeviceContext* context);
        /// Polls readiness without waiting; creates device objects on the caller thread only after HLSL completes.
        bool TryPrepare();
        /// Returns false while loading without touching the cached image or caller state; returns true after GPU processing.
        bool Update(DirectX11TexturePreview& preview,ID3D11Texture2D* source,UINT contentWidth,UINT contentHeight);
    };
}