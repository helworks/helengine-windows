#pragma once
#include <array>
#include <d3d11.h>
#include <stdexcept>
#include <wrl/client.h>

namespace helengine::windows {
    /// <summary>Owns immutable scene material blending and depth states for ordinary and toon geometry.</summary>
    class Win32MaterialOutputState {
        /// <summary>Stores opaque, forward alpha and toon alpha states with independent contour-mask write eligibility.</summary>
        std::array<Microsoft::WRL::ComPtr<ID3D11BlendState>, 4> Blends;
        /// <summary>Stores every authored depth-test/write combination without per-draw device allocations.</summary>
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11DepthStencilState>, 2>, 2> Depths;

      public:
        /// <summary>Builds the fixed-function states once while the scene device pipeline is initialized.</summary>
        void Initialize(ID3D11Device *device) {
            if (device == nullptr)
                throw std::invalid_argument("Material output states require a device.");
            D3D11_BLEND_DESC blend{};
            blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(device->CreateBlendState(&blend, Blends[0].ReleaseAndGetAddressOf())))
                throw std::runtime_error("Create opaque material output state failed.");
            blend.IndependentBlendEnable = TRUE;
            auto &color = blend.RenderTarget[0];
            color.BlendEnable = TRUE;
            color.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            color.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            color.BlendOp = D3D11_BLEND_OP_ADD;
            color.SrcBlendAlpha = D3D11_BLEND_ONE;
            color.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            color.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            if (FAILED(device->CreateBlendState(&blend, Blends[1].ReleaseAndGetAddressOf())))
                throw std::runtime_error("Create straight-alpha material output state failed.");
            color.SrcBlend = D3D11_BLEND_ONE;
            // Toon color already includes surface alpha. Integer part IDs, coverage and geometry auxiliaries remain
            // opaque-owned. Selected emission is already alpha weighted; accumulate it without erasing the bloom behind
            // clear glass.
            // Keep the underlying diffuse indirect term attenuated by glass transmittance in step with color.
            blend.RenderTarget[6] = color;
            // Separate integer ownership gives glass its own contour without replacing opaque silhouettes.
            blend.RenderTarget[7].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            auto &emission = blend.RenderTarget[3];
            emission = color;
            emission.DestBlend = D3D11_BLEND_ONE;
            emission.DestBlendAlpha = D3D11_BLEND_ONE;
            if (FAILED(device->CreateBlendState(&blend, Blends[2].ReleaseAndGetAddressOf())))
                throw std::runtime_error("Create premultiplied toon material output state failed.");
            // Visible glass excluded from contours must never erase an independently drawn proxy mask.
            blend.RenderTarget[7].RenderTargetWriteMask = 0;
            if (FAILED(device->CreateBlendState(&blend, Blends[3].ReleaseAndGetAddressOf())))
                throw std::runtime_error("Create contour-preserving glass output state failed.");
            for (std::size_t test = 0; test < 2; ++test)
                for (std::size_t write = 0; write < 2; ++write) {
                    D3D11_DEPTH_STENCIL_DESC depth{};
                    depth.DepthEnable = test != 0;
                    depth.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
                    depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
                    depth.StencilEnable = TRUE;
                    depth.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
                    depth.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
                    depth.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
                    depth.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
                    depth.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
                    depth.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
                    depth.BackFace = depth.FrontFace;
                    if (FAILED(device->CreateDepthStencilState(&depth, Depths[test][write].ReleaseAndGetAddressOf())))
                        throw std::runtime_error("Create material depth state failed.");
                }
        }

        /// <summary>Applies material alpha and depth intent; blended surfaces read depth without becoming opaque
        /// occluders.</summary>
        void Bind(ID3D11DeviceContext *context, bool alphaBlend, bool depthTest, bool depthWrite, bool toon, bool writeTransparentContours = true) const {
            if (context == nullptr)
                throw std::invalid_argument("Material output states require a context.");
            auto *blend = Blends[alphaBlend ? (toon ? (writeTransparentContours ? 2 : 3) : 1) : 0].Get();
            auto *depth = Depths[depthTest ? 1 : 0][!alphaBlend && depthWrite ? 1 : 0].Get();
            if (blend == nullptr || depth == nullptr)
                throw std::logic_error("Material output states must be initialized before drawing.");
            context->OMSetBlendState(blend, nullptr, 0xFFFFFFFFu);
            context->OMSetDepthStencilState(depth, 0);
        }

        /// <summary>Releases all scene material states with the owning renderer's other device resources.</summary>
        void Reset() {
            for (auto &blend : Blends)
                blend.Reset();
            for (auto &states : Depths)
                for (auto &depth : states)
                    depth.Reset();
        }
    };
} // namespace helengine::windows
