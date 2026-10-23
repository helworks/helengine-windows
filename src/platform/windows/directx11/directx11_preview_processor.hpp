#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include "directx11_texture_preview.hpp"

namespace helengine::windows {
    /// Shares an engine-owned GPU copy/reduction pass across authorized external texture previews on one immediate context.
    class DirectX11PreviewProcessor {
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
        /// Prepares shaders and isolated state once on a matching immediate context; no windows are created.
        DirectX11PreviewProcessor(ID3D11Device* device,ID3D11DeviceContext* context);
        /// Crops to valid content, fits the per-preview bounds without upscaling, and retains the resulting RGBA image entirely on the GPU.
        void Update(DirectX11TexturePreview& preview,ID3D11Texture2D* source,UINT contentWidth,UINT contentHeight);
    };
}