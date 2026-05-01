#pragma once

#ifdef DrawText
#undef DrawText
#endif

#include <d3d11.h>
#include <wrl/client.h>

#include <DirectXMath.h>

#if __has_include("RenderManager2D.hpp")
#include "Core.hpp"
#include "ICamera.hpp"
#include "IDrawable3D.hpp"
#include "IRenderVisitor3D.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "MaterialAsset.hpp"
#include "ModelAsset.hpp"
#include "RenderManager2D.hpp"
#include "RenderManager3D.hpp"
#include "RuntimeMaterial.hpp"
#include "RuntimeModel.hpp"
#include "RuntimeTexture.hpp"
#include "ShaderAsset.hpp"
#include "TextureAsset.hpp"
#include "float4x4.hpp"
#endif

namespace helengine::windows {
    class DirectX11Bootstrap;

#if __has_include("RenderManager2D.hpp")
    /// Stores one uploaded mesh resource that can be drawn by the Windows DirectX11 bridge.
    class Win32RuntimeModel : public RuntimeModel {
    public:
        /// Stores the GPU vertex buffer built from the packaged mesh.
        Microsoft::WRL::ComPtr<ID3D11Buffer> VertexBuffer;

        /// Stores the GPU index buffer when the mesh uses indexed drawing.
        Microsoft::WRL::ComPtr<ID3D11Buffer> IndexBuffer;

        /// Stores the number of vertices referenced by the mesh.
        UINT VertexCount = 0;

        /// Stores the number of indices referenced by the mesh.
        UINT IndexCount = 0;

        /// Stores the DirectX index-buffer format for indexed draws.
        DXGI_FORMAT IndexFormat = DXGI_FORMAT_UNKNOWN;
    };

    /// Provides a minimal native 3D renderer bridge that draws all cameras onto the main back buffer in draw order.
    class Win32RenderManager3D : public RenderManager3D, public IRenderVisitor3D {
    public:
        /// Creates the native renderer bridge for one DirectX11 bootstrap.
        explicit Win32RenderManager3D(DirectX11Bootstrap& bootstrap);

        /// Builds a GPU-ready runtime model from raw mesh asset metadata.
        RuntimeModel* BuildModelFromRaw(ModelAsset* data) override;

        /// Builds a runtime material placeholder that keeps the packaged material identity.
        RuntimeMaterial* BuildMaterialFromRaw(MaterialAsset* materialAsset, ShaderAsset* shaderAsset) override;

        /// Draws every registered camera to the Windows back buffer in camera order.
        void Draw() override;

        /// Draws one queued mesh for the currently active camera.
        void Visit(IDrawable3D* drawable) override;

    private:
        /// Creates the shaders, input layout, and fixed pipeline state on first use.
        void EnsurePipelineState();

        /// Creates the small debug-triangle vertex buffer used to validate the native draw pipeline.
        void EnsureDebugTriangleBuffer();

        /// Clears and renders one camera directly into the main back buffer.
        void RenderCamera(ICamera* camera, bool clearColorBuffer);

        /// Draws one clip-space debug triangle to validate the native pipeline independently from scene transforms.
        void DrawDebugTriangle();

        /// Clears the back buffer to a solid fallback color when nothing else renders.
        void ClearBackBuffer(float red, float green, float blue, float alpha);

        /// Resolves a camera viewport against the current swap-chain size.
        D3D11_VIEWPORT ResolveViewport(ICamera* camera) const;

        /// Stores the DirectX11 bootstrap used for device access and presentation resources.
        DirectX11Bootstrap& Bootstrap;

        /// Stores the simple fixed vertex shader for the first Windows mesh pass.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;

        /// Stores the simple fixed pixel shader for the first Windows mesh pass.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;

        /// Stores the diagnostic vertex shader that bypasses vertex buffers and emits a fullscreen triangle.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> DiagnosticVertexShader;

        /// Stores the diagnostic pixel shader used to validate draw-call execution.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> DiagnosticPixelShader;

        /// Stores the minimal diagnostic input layout used to validate native vertex-buffer submission.
        Microsoft::WRL::ComPtr<ID3D11InputLayout> DiagnosticInputLayout;

        /// Stores the input layout for position/normal/uv mesh vertices.
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;

        /// Stores the constant buffer used for world and view-projection transforms.
        Microsoft::WRL::ComPtr<ID3D11Buffer> TransformBuffer;

        /// Stores a tiny clip-space vertex buffer used for renderer diagnostics.
        Microsoft::WRL::ComPtr<ID3D11Buffer> DebugTriangleBuffer;

        /// Stores the rasterizer state for solid back-face-culled drawing.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> RasterizerState;

        /// Stores the depth-stencil state for normal opaque 3D drawing.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> DepthStencilState;

        /// Caches the active camera view-projection matrix while visiting one render queue.
        ::float4x4 CurrentViewProjection;
    };

    /// Provides a minimal native 2D renderer bridge so the generated core can initialize on Windows.
    class Win32RenderManager2D : public RenderManager2D {
    public:
        /// Builds a placeholder runtime texture from raw asset metadata.
        RuntimeTexture* BuildTextureFromRaw(TextureAsset* data) override;

        /// Accepts a sprite draw request without issuing backend rendering yet.
        void DrawSprite(ISpriteDrawable2D* sprite) override;

        /// Accepts a text draw request without issuing backend rendering yet.
        void DrawText(ITextDrawable2D* text);

        /// Accepts a text draw request without issuing backend rendering yet when Win32 macros rename the base contract.
        void DrawTextA(ITextDrawable2D* text);

        /// Accepts a rounded-rectangle draw request without issuing backend rendering yet.
        void DrawRoundedRect(IRoundedRectDrawable2D* shape) override;
    };
#endif
}
