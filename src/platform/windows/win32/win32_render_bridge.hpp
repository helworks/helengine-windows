#pragma once

#ifdef DrawText
#undef DrawText
#endif

#include <d3d11.h>
#include <wrl/client.h>

#include <DirectXMath.h>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#if __has_include("RenderManager2D.hpp")
class LightComponent;
class DirectionalLightComponent;
class RenderCommandListBuilder2D;

#include "Core.hpp"
#include "FontAsset.hpp"
#include "ICamera.hpp"
#include "IRenderVisitor2D.hpp"
#include "IDrawable3D.hpp"
#include "IRenderVisitor3D.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "IShaderRenderManager3D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "MaterialAsset.hpp"
#include "MaterialLayout.hpp"
#include "ModelAsset.hpp"
#include "RenderManager2D.hpp"
#include "RenderManager3D.hpp"
#include "ShaderAsset.hpp"
#include "ShaderStage.hpp"
#include "ShaderMaterialAsset.hpp"
#include "RuntimeMaterial.hpp"
#include "ShaderRuntimeMaterial.hpp"
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

        /// Stores the number of native bytes retained by the uploaded vertex buffer.
        std::size_t VertexBufferBytes = 0;

        /// Stores the number of native bytes retained by the uploaded index buffer.
        std::size_t IndexBufferBytes = 0;

        /// Stores the estimated source CPU bytes consumed by authored vertex arrays before upload.
        std::size_t SourceVertexBytes = 0;

        /// Stores the estimated source CPU bytes consumed by authored index arrays before upload.
        std::size_t SourceIndexBytes = 0;
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
    class Win32RenderManager3D : public RenderManager3D, public IRenderVisitor3D, public IShaderRenderManager3D {
    public:
        /// Creates the native renderer bridge for one DirectX11 bootstrap.
        explicit Win32RenderManager3D(DirectX11Bootstrap& bootstrap);

        /// Returns the number of uploaded texture resources currently cached by the Windows bridge.
        std::size_t GetTextureResourceCount() const;

        /// Returns the number of compiled material shader resources currently cached by the Windows bridge.
        std::size_t GetMaterialShaderResourceCount() const;

        /// Returns the number of authored material constant buffers currently cached by the Windows bridge.
        std::size_t GetMaterialConstantBufferCount() const;

        /// Returns the number of uploaded runtime models currently retaining native vertex or index buffers.
        std::size_t GetModelBufferCount() const;

        /// Returns the total native bytes currently retained by uploaded model vertex buffers.
        std::size_t GetModelVertexBufferBytes() const;

        /// Returns the total native bytes currently retained by uploaded model index buffers.
        std::size_t GetModelIndexBufferBytes() const;

        /// Returns the total native bytes currently retained by authored material constant buffers.
        std::size_t GetMaterialConstantBufferBytes() const;

        /// Builds a GPU-ready runtime model from raw mesh asset metadata.
        RuntimeModel* BuildModelFromRaw(ModelAsset* data) override;

        /// Builds a shader-backed runtime material from one packaged raw material asset path.
        RuntimeMaterial* BuildMaterialFromRawAsset(ContentManager* assetContentManager, std::string contentRootPath, std::string materialAssetPath) override;

        /// Builds a runtime material placeholder that keeps the packaged material identity.
        RuntimeMaterial* BuildMaterialFromRaw(ShaderMaterialAsset* materialAsset, ShaderAsset* shaderAsset) override;

        /// Returns the shader compile target consumed by the Windows DirectX11 bridge.
        ShaderCompileTarget get_ShaderCompileTarget() override;

        /// Invalidates cached shader resources that were built from one shader asset.
        void InvalidateShaderResources(std::string shaderAssetId, ShaderAsset* shaderAsset) override;

        /// Releases one runtime model previously created by the Windows renderer.
        void ReleaseModel(RuntimeModel* model) override;

        /// Releases one runtime material previously created by the Windows renderer.
        void ReleaseMaterial(RuntimeMaterial* material) override;

        /// Flushes any deferred Windows runtime asset releases.
        void FlushReleasedAssets() override;

        /// Releases Windows renderer-owned 3D resources.
        void Dispose() override;

        /// Draws every registered camera to the Windows back buffer in camera order.
        void Draw() override;

        /// Draws one queued mesh for the currently active camera.
        void Visit(IDrawable3D* drawable) override;

    private:
        /// Creates the shaders, input layout, and fixed pipeline state on first use.
        void EnsurePipelineState();

        /// Creates the shaders, constant buffers, and textures required by the directional shadow path.
        void EnsureShadowPipelineState();

        /// Creates the default sampler used by material texture bindings.
        void EnsureTextureSamplerState();

        /// Creates the small debug-triangle vertex buffer used to validate the native draw pipeline.
        void EnsureDebugTriangleBuffer();

        /// Builds a shader resource from one packaged shader asset and material program selection.
        Win32ShaderResource BuildShaderResource(ShaderMaterialAsset* materialAsset, ShaderAsset* shaderAsset);

        /// Builds Direct3D input elements from the vertex signature exposed by a shader program.
        std::vector<D3D11_INPUT_ELEMENT_DESC> BuildInputElements(ShaderAsset* shaderAsset, std::string vertexProgram, std::string variant, std::vector<std::string>& semanticStorage);

        /// Resolves one shader binary for the requested shader program, stage, and variant.
        ShaderBinaryAsset* GetShaderBinary(ShaderAsset* shaderAsset, std::string programName, ShaderStage stage, std::string variant);

        /// Resolves the DirectX input format for one shader vertex element format string.
        DXGI_FORMAT ResolveVertexElementFormat(std::string format) const;

        /// Clears and renders one camera directly into the main back buffer.
        void RenderCamera(ICamera* camera, bool clearColorBuffer);

        /// Copies the currently visible authored lights relevant to one camera into a render-ready list.
        std::vector<LightComponent*> SnapshotVisibleLights(ICamera* camera) const;

        /// Uploads the packed forward-light constant buffer used by built-in forward scene shaders.
        void PrepareForwardLightState(const std::vector<LightComponent*>& lights);

        /// Uploads the packed shadow constant buffer and bindings for the active directional shadow light.
        void PrepareShadowState(ICamera* camera, const std::vector<LightComponent*>& lights);

        /// Finds the first visible shadow-enabled directional light affecting the current camera.
        DirectionalLightComponent* FindPrimaryDirectionalShadowLight(const std::vector<LightComponent*>& lights) const;

        /// Renders the current scene depth from the active directional light into the shared shadow map.
        void RenderDirectionalShadowMap(ICamera* camera, DirectionalLightComponent* light);

        /// Draws one shadow-casting mesh into the active directional shadow map.
        void DrawDirectionalShadowCaster(IDrawable3D* drawable, ::float4x4& lightViewProjection);

        /// Returns whether one runtime material should contribute geometry to the directional shadow pass.
        bool ShouldMaterialCastShadows(RuntimeMaterial* material) const;

        /// Builds the light-space view-projection matrix used by the active directional shadow pass.
        ::float4x4 BuildDirectionalShadowViewProjection(ICamera* camera, DirectionalLightComponent* light) const;

        /// Draws one clip-space debug triangle to validate the native pipeline independently from scene transforms.
        void DrawDebugTriangle();

        /// Resolves the shader-backed runtime material required by the native DirectX11 material binding path.
        ShaderRuntimeMaterial* RequireShaderRuntimeMaterial(RuntimeMaterial* material);

        /// Applies the shader and resource bindings for one runtime material.
        void ApplyMaterial(RuntimeMaterial* material);

        /// Applies authored material constant-buffer payloads for one runtime material while leaving engine-managed buffers untouched.
        void BindMaterialConstantBuffers(RuntimeMaterial* material);

        /// Binds the resolved runtime material texture to the pixel shader slot consumed by the Windows forward path.
        void BindMaterialTexture(RuntimeMaterial* material);

        /// Resolves an uploaded shader resource view for one runtime texture.
        ID3D11ShaderResourceView* ResolveTextureResourceView(RuntimeTexture* texture) const;

        /// Resolves or creates one Direct3D constant buffer matching the requested shader slot and byte size.
        ID3D11Buffer* GetOrCreateMaterialConstantBuffer(int32_t slot, int32_t sizeInBytes);

        /// Returns whether one constant-buffer binding is owned by the renderer instead of material-authored property data.
        static bool IsEngineManagedConstantBufferBinding(std::string bindingName);

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

        /// Stores the built-in constant buffer consumed by forward-light scene shaders.
        Microsoft::WRL::ComPtr<ID3D11Buffer> ForwardLightConstantBuffer;

        /// Stores the built-in constant buffer consumed by shadow-aware scene shaders.
        Microsoft::WRL::ComPtr<ID3D11Buffer> ShadowConstantBuffer;

        /// Stores the default sampler state used by material texture bindings.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> TextureSamplerState;

        /// Stores the sampler state used by the directional shadow atlas texture.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> ShadowSamplerState;

        /// Stores the diagnostic vertex shader that bypasses vertex buffers and emits a fullscreen triangle.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> DiagnosticVertexShader;

        /// Stores the diagnostic pixel shader used to validate draw-call execution.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> DiagnosticPixelShader;

        /// Stores the minimal diagnostic input layout used to validate native vertex-buffer submission.
        Microsoft::WRL::ComPtr<ID3D11InputLayout> DiagnosticInputLayout;

        /// Stores the input layout for position/normal/uv mesh vertices.
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;

        /// Stores the input layout for the directional shadow depth pass.
        Microsoft::WRL::ComPtr<ID3D11InputLayout> ShadowInputLayout;

        /// Stores the constant buffer used for world and view-projection transforms.
        Microsoft::WRL::ComPtr<ID3D11Buffer> TransformBuffer;

        /// Stores the constant buffer used by the directional shadow depth pass.
        Microsoft::WRL::ComPtr<ID3D11Buffer> ShadowTransformBuffer;

        /// Stores a tiny clip-space vertex buffer used for renderer diagnostics.
        Microsoft::WRL::ComPtr<ID3D11Buffer> DebugTriangleBuffer;

        /// Stores the rasterizer state for solid back-face-culled drawing.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> RasterizerState;

        /// Stores the depth-stencil state for normal opaque 3D drawing.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> DepthStencilState;

        /// Stores the rasterizer state used by the directional shadow depth pass.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> ShadowRasterizerState;

        /// Stores the depth-stencil state used by the directional shadow depth pass.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> ShadowDepthStencilState;

        /// Stores the vertex shader used by the directional shadow depth pass.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> ShadowVertexShader;

        /// Stores the pixel shader used by the directional shadow depth pass.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> ShadowPixelShader;

        /// Stores the typeless texture that backs the directional shadow depth atlas.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> ShadowMapTexture;

        /// Stores the shader resource view used by forward shaders to sample the directional shadow atlas.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShadowMapShaderResourceView;

        /// Stores the depth-stencil view used while rendering the directional shadow atlas.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> ShadowMapDepthStencilView;

        /// Caches the active camera view-projection matrix while visiting one render queue.
        ::float4x4 CurrentViewProjection;

        /// Caches the active directional shadow view-projection matrix while the shadow pass visits the scene.
        ::float4x4 CurrentShadowViewProjection;

        /// Caches the active camera world-space position for the current 3D pass.
        ::float3 CurrentCameraPosition;

        /// Tracks whether the renderer is currently visiting drawables for the directional shadow pass.
        bool IsShadowPassActive = false;

        /// Caches uploaded shader resources by material id.
        std::unordered_map<std::string, std::unique_ptr<Win32ShaderResource>> MaterialShaderResources;

        /// Caches authored material constant buffers by slot and byte size.
        std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D11Buffer>> MaterialConstantBuffers;

        /// Tracks how many uploaded runtime models currently retain native vertex or index buffers.
        std::size_t LiveModelBufferCount = 0;

        /// Tracks the total native bytes currently retained by uploaded model vertex buffers.
        std::size_t LiveModelVertexBufferBytes = 0;

        /// Tracks the total native bytes currently retained by uploaded model index buffers.
        std::size_t LiveModelIndexBufferBytes = 0;

        /// Tracks the total native bytes currently retained by authored material constant buffers.
        std::size_t LiveMaterialConstantBufferBytes = 0;

    };

    /// Provides a native 2D renderer bridge that can draw packaged sprites, text, and UI shapes on Windows.
    class Win32RenderManager2D : public RenderManager2D, public IRenderVisitor2D {
    public:
        /// Creates the native 2D bridge for one DirectX11 bootstrap.
        explicit Win32RenderManager2D(DirectX11Bootstrap& bootstrap);

        /// Returns the number of uploaded texture resources currently cached by the Windows bridge.
        std::size_t GetTextureResourceCount() const;

        /// Returns the number of engine-owned uploaded texture resources currently cached by the Windows bridge.
        std::size_t GetEngineOwnedTextureResourceCount() const;

        /// Builds a placeholder runtime texture from raw asset metadata.
        RuntimeTexture* BuildTextureFromRaw(TextureAsset* data) override;

        /// Releases one runtime texture previously created by the Windows renderer.
        void ReleaseTexture(RuntimeTexture* texture) override;

        /// Releases one font asset previously materialized for the Windows renderer.
        void ReleaseFont(FontAsset* font) override;

        /// Flushes any deferred Windows runtime texture releases.
        void FlushReleasedTextures() override;

        /// Releases Windows renderer-owned 2D resources.
        void Dispose() override;

        /// Draws every queued 2D drawable for one camera.
        void RenderCamera(ICamera* camera);

        /// Visits one queued 2D drawable and lets it dispatch into the concrete draw methods.
        void Visit(IDrawable2D* drawable) override;

        /// Draws one sprite directly into the active camera viewport.
        void DrawSprite(ISpriteDrawable2D* sprite) override;

        /// Draws one text string directly into the active camera viewport.
        void DrawText(ITextDrawable2D* text) override;

        /// Draws one UI shape directly into the active camera viewport.
        void DrawRoundedRect(IRoundedRectDrawable2D* shape) override;

    private:
        /// Creates the DirectX11 shaders, buffers, and fixed pipeline state needed for 2D rendering.
        void EnsurePipelineState();

        /// Resolves one camera viewport against the current swap-chain size.
        D3D11_VIEWPORT ResolveViewport(ICamera* camera) const;

        /// Resolves an uploaded shader resource view for one runtime texture.
        ID3D11ShaderResourceView* ResolveTextureResourceView(RuntimeTexture* texture) const;

        /// Configures the DirectX11 state used by one textured quad draw.
        void PrepareTexturedQuadDraw(ID3D11ShaderResourceView* textureView);

        /// Draws one textured quad in window-space pixel coordinates.
        void DrawTexturedQuad(
            ID3D11ShaderResourceView* textureView,
            float x,
            float y,
            float width,
            float height,
            float4 sourceRect,
            byte4 color);

        /// Uploads quad vertices into the reusable dynamic buffer and issues one draw from the written range.
        void DrawQuadVertices(const void* vertices, UINT vertexCount);

        /// Draws one solid-color rectangle in window-space pixel coordinates.
        void DrawSolidRect(float x, float y, float width, float height, byte4 color);

        /// Configures the DirectX11 state used by one rounded-rect SDF draw.
        void PrepareRoundedRectDraw();

        /// Draws one rounded rectangle using the native signed-distance-field shader path.
        void DrawRoundedRectSdf(float4 bounds, float radius, float borderThickness, byte4 fillColor, byte4 borderColor, int32_t corners);

        /// Applies the currently active scissor rectangle, or falls back to the full viewport when clipping is inactive.
        void ApplyScissorRect();

        /// Pushes one clip rectangle onto the active stack after converting it into the current viewport's scissor space.
        void PushClipRect(float4 clipRect);

        /// Pops the most recent clip rectangle and restores the previous scissor state.
        void PopClipRect();

        /// Resolves one raw clip rectangle into a viewport-clamped Direct3D scissor rectangle.
        D3D11_RECT ResolveScissorRect(float4 clipRect) const;

        /// Stores the DirectX11 bootstrap used for texture uploads.
        DirectX11Bootstrap& Bootstrap;

        /// Stores the dynamic quad vertex buffer reused by every 2D draw call.
        Microsoft::WRL::ComPtr<ID3D11Buffer> QuadVertexBuffer;

        /// Stores the number of vertices allocated in the reusable 2D quad buffer.
        UINT QuadVertexCapacity = 0;

        /// Stores the next vertex slot to write during the current 2D camera pass.
        UINT QuadVertexCursor = 0;

        /// Stores the input layout used by the 2D quad shader pipeline.
        Microsoft::WRL::ComPtr<ID3D11InputLayout> QuadInputLayout;

        /// Stores the fixed 2D quad vertex shader.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> QuadVertexShader;

        /// Stores the fixed 2D quad pixel shader.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> QuadPixelShader;

        /// Stores the fixed rounded-rect vertex shader.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> RoundedRectVertexShader;

        /// Stores the fixed rounded-rect pixel shader.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> RoundedRectPixelShader;

        /// Stores the default sampler used by sprite and text draws.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> TextureSamplerState;

        /// Stores the alpha-blend state used by 2D UI draws.
        Microsoft::WRL::ComPtr<ID3D11BlendState> AlphaBlendState;

        /// Stores the rasterizer state used by 2D draws.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> RasterizerState;

        /// Stores the disabled-depth state used by 2D overlay draws.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> DepthStencilState;

        /// Stores the 1x1 white texture used for solid-color rectangle draws.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> WhiteTexture;

        /// Stores the shader resource view for the 1x1 white texture.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> WhiteShaderResourceView;

        /// Stores the rounded-rect constants consumed by the native SDF shader path.
        Microsoft::WRL::ComPtr<ID3D11Buffer> RoundedRectConstantBuffer;

        /// Stores the currently active 2D camera viewport.
        D3D11_VIEWPORT CurrentViewport {};

        /// Tracks whether a 2D camera pass is currently active.
        bool HasActiveViewport = false;

        /// Stores the currently active nested scissor stack for the 2D command stream.
        std::vector<D3D11_RECT> ClipRectStack;

        /// Stores the currently effective scissor rectangle when clipping is active.
        D3D11_RECT CurrentScissorRect {};

        /// Tracks whether the current 2D pass has an active clipped scissor rectangle.
        bool HasActiveClipRect = false;

        /// Reuses one generated 2D command-list builder so the Windows bridge does not recreate a leaking builder graph every frame.
        std::unique_ptr<RenderCommandListBuilder2D> CommandListBuilder;
    };
#endif
}

