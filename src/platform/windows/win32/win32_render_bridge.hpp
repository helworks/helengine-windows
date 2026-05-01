#pragma once

#ifdef DrawText
#undef DrawText
#endif

#include <d3d11.h>
#include <wrl/client.h>

#include <DirectXMath.h>
#include <memory>
#include <unordered_map>
#include <vector>

#if __has_include("RenderManager2D.hpp")
#include "Core.hpp"
#include "ICamera.hpp"
#include "IDrawable3D.hpp"
#include "IRenderVisitor3D.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "MaterialAsset.hpp"
#include "MaterialLayout.hpp"
#include "ModelAsset.hpp"
#include "RenderManager2D.hpp"
#include "RenderManager3D.hpp"
#include "ShaderAsset.hpp"
#include "RuntimeMaterial.hpp"
#include "RuntimeModel.hpp"
#include "RuntimeTexture.hpp"
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

    /// Stores one compiled shader pair and matching input layout for a Windows material.
    class Win32ShaderResource {
    public:
        /// Stores the compiled vertex shader.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;

        /// Stores the compiled pixel shader.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;

        /// Stores the input layout matched to the shader vertex signature.
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;
    };

    /// Stores one uploaded texture resource that can be bound by a Windows material.
    class Win32TextureResource {
    public:
        /// Stores the GPU texture backing the uploaded asset.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;

        /// Stores the shader resource view used for sampling.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
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

        /// Creates the default sampler used by material texture bindings.
        void EnsureTextureSamplerState();

        /// Creates the small debug-triangle vertex buffer used to validate the native draw pipeline.
        void EnsureDebugTriangleBuffer();

        /// Builds a shader resource from one packaged shader asset and material program selection.
        Win32ShaderResource BuildShaderResource(MaterialAsset* materialAsset, ShaderAsset* shaderAsset);

        /// Builds Direct3D input elements from the vertex signature exposed by a shader program.
        std::vector<D3D11_INPUT_ELEMENT_DESC> BuildInputElements(ShaderAsset* shaderAsset, std::string vertexProgram, std::string variant, std::vector<std::string>& semanticStorage);

        /// Resolves one shader binary for the requested shader program, stage, and variant.
        ShaderBinaryAsset* GetShaderBinary(ShaderAsset* shaderAsset, std::string programName, ShaderStage stage, std::string variant);

        /// Resolves the DirectX input format for one shader vertex element format string.
        DXGI_FORMAT ResolveVertexElementFormat(std::string format) const;

        /// Clears and renders one camera directly into the main back buffer.
        void RenderCamera(ICamera* camera, bool clearColorBuffer);

        /// Draws one clip-space debug triangle to validate the native pipeline independently from scene transforms.
        void DrawDebugTriangle();

        /// Applies the shader and resource bindings for one runtime material.
        void ApplyMaterial(RuntimeMaterial* material);

        /// Binds one runtime material texture to the pixel shader if the texture has been uploaded.
        void BindMaterialTexture(RuntimeMaterial* material, int32_t bindingIndex, int32_t slot);

        /// Resolves an uploaded shader resource view for one runtime texture.
        ID3D11ShaderResourceView* ResolveTextureResourceView(RuntimeTexture* texture) const;

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

        /// Stores the default sampler state used by material texture bindings.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> TextureSamplerState;

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

        /// Caches uploaded shader resources by material id.
        std::unordered_map<std::string, std::unique_ptr<Win32ShaderResource>> MaterialShaderResources;

    };

    /// Provides a minimal native 2D renderer bridge so the generated core can initialize on Windows.
    class Win32RenderManager2D : public RenderManager2D {
    public:
        /// Creates the native 2D bridge for one DirectX11 bootstrap.
        explicit Win32RenderManager2D(DirectX11Bootstrap& bootstrap);

        /// Builds a placeholder runtime texture from raw asset metadata.
        RuntimeTexture* BuildTextureFromRaw(TextureAsset* data) override;

        /// Accepts a sprite draw request without issuing backend rendering yet.
        void DrawSprite(ISpriteDrawable2D* sprite) override;

        /// Accepts a text draw request without issuing backend rendering yet.
        void DrawText(ITextDrawable2D* text);

        /// Accepts a rounded-rectangle draw request without issuing backend rendering yet.
        void DrawRoundedRect(IRoundedRectDrawable2D* shape) override;

    private:
        /// Stores the DirectX11 bootstrap used for texture uploads.
        DirectX11Bootstrap& Bootstrap;
    };
#endif
}
