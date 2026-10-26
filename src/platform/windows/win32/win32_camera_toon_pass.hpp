#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <array>
#include <unordered_map>
#include "platform/windows/win32/win32_camera_toon_effects_configuration.hpp"
#include "platform/windows/directx11/directx11_shader_compilation.hpp"

namespace helengine::windows {
    /// Owns one camera's viewport-local toon attachments, geometry shading and edge composition.
    class Win32CameraToonPass {
    public:
        /// Starts background compilation and validates physical-pixel settings without blocking the calling thread.
        Win32CameraToonPass(ID3D11Device* device, float outsidePixels, float insidePixels, int bands);

        /// Cancels outstanding CPU compilation without waiting for a worker or retaining this pass in a callback.
        ~Win32CameraToonPass();

        /// Polls compilation without waiting and creates device resources only after all shaders succeed.
        bool TryPrepare();

        /// Validates and replaces the camera's explicit outline and lighting settings.
        void Configure(float outsidePixels, float insidePixels, int bands);

        /// Replaces validated optional effects without altering the legacy outline configuration.
        void ConfigureEffects(const CameraToonEffectsConfiguration& configuration);

        /// Uploads explicit RGBA pixels and generates filtered environment mips on the rendering context.
        void SetEnvironment(ID3D11DeviceContext* context, uint32_t width, uint32_t height, const uint8_t* rgbaBytes, std::size_t byteCount);

        /// Borrows one exact material identity and stores its explicit reflection and selected-emission weights.
        void ConfigureMaterialEffects(const void* material, float reflectivity, float bloomWeight, bool pearlSurface = false);

        /// Removes a borrowed material identity before its lifetime ends without dereferencing it.
        void RemoveMaterialEffects(const void* material);

        /// Binds only the weights registered for this exact draw instance; unregistered materials receive zero.
        void BindRegisteredGeometry(ID3D11DeviceContext* context, const void* material);

        /// Recreates matching attachments on resize and binds cleared camera-local MRT geometry targets.
        void Begin(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport, UINT samples);

        /// Allocates one visible-part identity and binds mesh shading while retaining transform, material and lights.
        void BindGeometry(ID3D11DeviceContext* context, float reflectivity = 0.0f, float bloomWeight = 0.0f, bool pearlSurface = false);

        /// Resolves coverage and composites geometry/edges into the original viewport before UI rendering.
        void Composite(ID3D11DeviceContext* context, ID3D11RenderTargetView* destination, ID3D11DepthStencilView* destinationDepth, const D3D11_VIEWPORT& viewport);

    private:
        /// Throws with the precise Direct3D operation when native resource creation fails.
        static void Check(HRESULT result, const char* operation);
        /// Creates immutable device resources on the renderer thread from a complete successfully compiled shader set.
        void InitializePipeline(const DirectX11ShaderCompilation::Bytecodes& bytecodes);
        /// Allocates viewport-sized color/part/coverage/depth attachments with the requested sample count.
        void Resize(UINT width, UINT height, UINT samples);

        /// Uploads the complete camera parameter ABI for geometry, blur or final composition.
        void UploadParameters(ID3D11DeviceContext* context, float reflectivity = 0.0f, float bloomWeight = 0.0f, float blurX = 0.0f, float blurY = 0.0f);

        /// Owns cancellation of CPU-only background compilation until device resources are prepared.
        std::unique_ptr<DirectX11ShaderCompilation> ShaderCompilation;
        /// Retains any compilation or device-creation failure so later polls cannot silently retry broken state.
        std::exception_ptr PipelineFailure;
        /// Allows render state changes only after every immutable GPU resource has been created successfully.
        bool PipelinePrepared = false;
        /// Stores independently switchable optional effect settings, initially preserving legacy rendering.
        CameraToonEffectsConfiguration Effects;
        /// Borrows exact material identities for the lifetime of this camera registration.
        std::unordered_map<const void*,std::array<float,3>> MaterialEffects;
        /// Owns a mipmapped filtered approximation of the camera's environment.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> EnvironmentView;
        /// Records the maximum explicit environment LOD for standard roughness filtering.
        float EnvironmentMaximumLod = 0.0f;
        /// Samples environment mips and intermediate glow with linear filtering.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> EffectSampler;
        /// Runs a bounded nine-tap separable filter on the selected HDR emission attachment.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> BlurPixelShader;
        /// Stores selected emitted radiance independently from ordinary RGBA8 color.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> EmittedLight;
        /// Stores resolved HDR emission without ever resolving integer part identities.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> ResolvedEmittedLight;
        /// Receives selected emitted radiance as the fourth geometry attachment.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> EmittedLightTarget;
        /// Samples resolved selected emission for reduced-size filtering.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> EmittedLightView;
        /// Receives the reduced horizontal glow filter.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> BloomHorizontalTarget;
        /// Samples horizontal glow while filtering vertically.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> BloomHorizontalView;
        /// Receives the reduced vertical glow filter.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> BloomVerticalTarget;
        /// Samples the complete reduced glow during final premultiplied composition.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> BloomVerticalView;
        /// Retains every additional shader resource slot touched by environment and glow.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PreviousEffectViews[3];
        /// Retains the reserved linear effect sampler for following cameras.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> PreviousEffectSampler;

        /// Retains the graphics device shared by the renderer's output windows.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// Produces standard position/normal/UV geometry in the existing mesh layout.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> GeometryVertexShader;
        /// Writes stepped material color, exact part identity and geometric sample coverage together.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> GeometryPixelShader;
        /// Shades only opted-in shell surfaces with angular pearl color and a light-dependent glossy lobe.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PearlGeometryPixelShader;
        /// Generates a fullscreen triangle without transient vertex buffers.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> CompositeVertexShader;
        /// Integrates bounded physical-pixel silhouettes and centered visible-part contours.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> CompositePixelShader;
        /// Reads integer sample identities directly when geometry uses four samples.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> CompositeMultisamplePixelShader;
        /// Matches the renderer's fixed 32-byte mesh vertex layout.
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;
        /// Carries physical-pixel widths, lighting bands and viewport dimensions to shaders.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Parameters;
        /// Stores multisampled geometry color when coverage is enabled.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Color;
        /// Stores camera-local unsigned part identities without resolving or averaging samples.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Parts;
        /// Stores geometric sample ownership independently of material alpha and part identity.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Coverage;
        /// Stores resolved color for composition and is also the 1x geometry color target.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> ResolvedColor;
        /// Stores exact resolved geometric coverage for subpixel silhouette reconstruction.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> ResolvedCoverage;
        /// Binds the local color target for geometry rendering.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> ColorTarget;
        /// Binds the integer part identity target for geometry rendering.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> PartTarget;
        /// Binds the geometric sample mask alongside material color and integer part identities.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> CoverageTarget;
        /// Samples the resolved material color.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ColorView;
        /// Reads unaveraged part identities from the local 1x or 4x attachment.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PartView;
        /// Samples geometric coverage without interpreting authored surface transparency.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CoverageView;
        /// Owns depth testing independently from preceding and following cameras.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> DepthTarget;
        /// Enables opaque geometry MRT writes independent of inherited UI blend state.
        Microsoft::WRL::ComPtr<ID3D11BlendState> GeometryBlend;
        /// Accumulates premultiplied local color over either transparent or opaque destination pixels.
        Microsoft::WRL::ComPtr<ID3D11BlendState> CompositeBlend;
        /// Disables depth reads/writes during fullscreen composition.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> CompositeDepth;
        /// Disables culling and inherited scissor state for composition.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> CompositeRasterizer;
        /// Retains the caller's blend state across the local opaque geometry pass.
        Microsoft::WRL::ComPtr<ID3D11BlendState> PreviousBlend;
        /// Retains the caller's constant blend factor without changing subsequent camera behavior.
        float PreviousBlendFactor[4] {};
        /// Retains the caller's sample write mask for restoration after composition.
        UINT PreviousSampleMask = 0;
        /// Retains the caller's reserved pixel constant-buffer slot while the toon pass owns it.
        Microsoft::WRL::ComPtr<ID3D11Buffer> PreviousParameters;
        /// Retains the caller's first reserved shader-resource binding across composition.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PreviousColorView;
        /// Retains the second reserved shader-resource binding across composition.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PreviousPartView;
        /// Retains the caller's third reserved shader-resource binding across composition.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PreviousCoverageView;
        /// Retains the original depth test state before camera-local geometry changes it.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> PreviousDepth;
        /// Retains the original rasterizer before camera-local composition changes it.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> PreviousRasterizer;
        /// Retains the vertex shader from before this camera's geometry overrides.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> PreviousVertexShader;
        /// Retains the pixel shader from before this camera's geometry overrides.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PreviousPixelShader;
        /// Retains the caller's input layout before mesh binding changes it.
        Microsoft::WRL::ComPtr<ID3D11InputLayout> PreviousLayout;
        /// Retains the caller's primitive topology before geometry rendering.
        D3D11_PRIMITIVE_TOPOLOGY PreviousTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        /// Retains the caller's stencil comparison reference.
        UINT PreviousStencilReference = 0;
        /// Reserves zero for background and allocates one exact identity per submitted mesh piece.
        UINT PartIdentifier = 0;
        /// Retains the destination viewport origin while geometry renders in local coordinates.
        float ViewportX = 0.0f;
        /// Retains the destination viewport's vertical origin for composition.
        float ViewportY = 0.0f;
        /// Tracks current attachment dimensions to replace resources only when required.
        UINT Width = 0;
        /// Tracks current attachment height independently from viewport screen position.
        UINT Height = 0;
        /// Tracks the attachment sample count for 1x/4x resource compatibility.
        UINT Samples = 0;
        /// Stores the outward silhouette dilation radius in physical viewport pixels.
        float OutsidePixels = 0.0f;
        /// Stores the total centered internal stroke width in physical viewport pixels.
        float InsidePixels = 0.0f;
        /// Stores the number of diffuse light levels used by this camera only.
        int Bands = 3;
    };
}
