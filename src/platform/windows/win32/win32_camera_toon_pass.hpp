#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <array>
#include <unordered_map>
#include "platform/windows/win32/win32_camera_toon_effects_configuration.hpp"
#include "platform/windows/win32/win32_camera_toon_targets.hpp"
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

        /// Composites only the exterior union silhouette in display-space accent RGB; bounds limit sampling work.
        void ConfigureSelection(float red, float green, float blue, float radius, float left, float top, float right, float bottom);


        /// Uploads explicit RGBA pixels and generates filtered environment mips on the rendering context.
        void SetEnvironment(ID3D11DeviceContext* context, uint32_t width, uint32_t height, const uint8_t* rgbaBytes, std::size_t byteCount);

        /// Registers exact material styling; outlineOnly selects the existing auxiliary contour mask without surface shading. Outline scale ranges from zero to four.
        void ConfigureMaterialEffects(const void* material, float reflectivity, float bloomWeight, bool pearlSurface = false, bool toonSurface = false, float outlineScale = 1.0f, float metallic = 0.0f, bool outlineOnly = false);

        /// <summary>Binds capture-only shader parameters without allocating part identifiers or changing presentation/outline state.</summary>
        void BindCaptureGeometry(ID3D11DeviceContext* context,const void* material);

        /// Removes a borrowed material identity before its lifetime ends without dereferencing it.
        void RemoveMaterialEffects(const void* material);
        /// <summary>Returns the registered conductor weight, with the ordinary unregistered-material value of zero.</summary>
        float GetMaterialMetallic(const void* material) const;
        /// Preserves independent proxy masks when a visible material explicitly opts out of auxiliary contours.
        bool WritesAuxiliaryContours(const void* material) const;

        /// <summary>Binds exact registered weights; selective outline mode gives unregistered materials zero contour width.</summary>
        void BindRegisteredGeometry(ID3D11DeviceContext* context, const void* material,bool selectiveOutlines=false, bool transparent=false, const void* owner=nullptr);

#ifdef HELENGINE_LIGHTMAP
        /// <summary>Selects the baked diffuse pixel variant while retaining already bound material effects and UV0 shading.</summary>
        void BindLightmapGeometry(ID3D11DeviceContext* context,const void* material);
#endif
        /// Recreates matching attachments on resize and binds cleared camera-local MRT geometry targets.
        void Begin(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport, UINT samples, float depthNear = 0, float depthFar = 1);

        /// Allocates one visible-part identity and selects ordinary shading or the surface-free contour-mask shader.
        void BindGeometry(ID3D11DeviceContext* context, float reflectivity = 0.0f, float bloomWeight = 0.0f, bool pearlSurface = false, bool toonSurface = false, float outlineScale = 1.0f, float metallic = 0.0f, bool outlineOnly = false, const void* owner=nullptr);

        /// Resolves coverage and composites geometry into the original viewport; raw color skips effect prepasses before UI rendering.
        void Composite(ID3D11DeviceContext* context, ID3D11RenderTargetView* destination, ID3D11DepthStencilView* destinationDepth, const D3D11_VIEWPORT& viewport, bool rawColor = false);

    private:
        /// Stores display-space accent RGB and the nonzero selection-only radius in physical pixels.
        float SelectionColor[4] = {};
        /// Stores the selected object's projected bounds, including the dilation margin.
        float SelectionBounds[4] = {};

        /// Throws with the precise Direct3D operation when native resource creation fails.
        static void Check(HRESULT result, const char* operation);
        /// Creates immutable device resources on the renderer thread from a complete successfully compiled shader set.
        void InitializePipeline(const DirectX11ShaderCompilation::Bytecodes& bytecodes);
        /// Allocates viewport-sized color/part/coverage/depth attachments with the requested sample count.
        void Resize(UINT width, UINT height, UINT samples);

        /// Borrows compatible idle attachments while retaining this camera's own shaders and material configuration.
        void UseTargets(const std::shared_ptr<Win32CameraToonTargets>& targets);

        /// Returns the borrowed scratch set only after composition or cancellation of this camera's lifetime.
        void ReleaseTargets();

        /// Retains scratch attachment ownership without duplicating viewport resources for sequential cameras.
        std::shared_ptr<Win32CameraToonTargets> SharedTargets;
        /// Records whether this pass currently reserves the shared attachment set.
        bool TargetsAcquired = false;

        /// Uploads the complete camera parameter ABI for geometry, blur or final composition.
        void UploadParameters(ID3D11DeviceContext* context, float reflectivity = 0.0f, float bloomWeight = 0.0f, float blurX = 0.0f, float blurY = 0.0f, bool rawColor = false);

        /// Owns cancellation of CPU-only background compilation until device resources are prepared.
        std::unique_ptr<DirectX11ShaderCompilation> ShaderCompilation;
        /// Retains any compilation or device-creation failure so later polls cannot silently retry broken state.
        std::exception_ptr PipelineFailure;
        /// Allows render state changes only after every immutable GPU resource has been created successfully.
        bool PipelinePrepared = false;
        /// Stores independently switchable optional effect settings, initially preserving legacy rendering.
        CameraToonEffectsConfiguration Effects;
        /// Selects linear scRGB only for the actual FP16 presentation destination of this composite.
        bool HdrOutput = false;
        /// Borrows exact material identities for the lifetime of this camera registration.
        std::unordered_map<const void*,std::array<float,7>> MaterialEffects;
        /// Owns a mipmapped filtered approximation of the camera's environment.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> EnvironmentView;
        /// Records the maximum explicit environment LOD for standard roughness filtering.
        float EnvironmentMaximumLod = 0.0f;
        /// Owns the reference ACES 2.0 SDR transform independently from viewport size.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> AcesSdrView;
        /// Samples environment mips and intermediate glow with linear filtering.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> EffectSampler;
        /// <summary>Integrates three slice pairs with four radial steps per side on SM5 hardware.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> GtaoPixelShader;
        /// <summary>Denoises current-frame visibility while rejecting depth, normal and coverage discontinuities.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> GtaoFilterPixelShader;
        /// <summary>Preserves only the two new slots; the intervening shared GI and probe resources are untouched.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PreviousAoViews[2];
        /// Runs a bounded nine-tap separable filter on the selected HDR emission attachment.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> BlurPixelShader;
        /// Stores selected emitted radiance independently from linear HDR scene color.
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
        /// Marks thirty-two-pixel tiles containing any outlined geometry before full-resolution composition.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> OutlineTilePixelShader;
        /// Reads exact four-sample contour ownership while constructing the coarse occupancy mask.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> OutlineTileMultisamplePixelShader;
        /// Receives one occupancy value per thirty-two-by-thirty-two geometry tile.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> OutlineTileTarget;
        /// Rejects distant contour searches with nine bounded coarse texture reads per pixel.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> OutlineTileView;
        /// <summary>Retains alpha-weighted diffuse indirect radiance independently of direct light and emission.</summary>
        Microsoft::WRL::ComPtr<ID3D11Texture2D> DiffuseIndirect;
        /// <summary>Provides single-sample diffuse indirect radiance for final composition.</summary>
        Microsoft::WRL::ComPtr<ID3D11Texture2D> ResolvedDiffuseIndirect;
        /// <summary>Binds the seventh opaque geometry attachment, also blended with transparent material transmittance.</summary>
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> DiffuseIndirectTarget;
        /// <summary>Samples resolved diffuse indirect radiance without modifying direct or emissive contributions.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> DiffuseIndirectView;
        /// <summary>Receives full-resolution, bounded analytic horizon visibility before denoising.</summary>
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> AmbientVisibilityTarget;
        /// <summary>Samples current-frame horizon integration during the spatial filter.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> AmbientVisibilityView;
        /// <summary>Receives edge-aware visibility without temporal history.</summary>
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> FilteredVisibilityTarget;
        /// <summary>Samples current-frame spatially filtered visibility during radiance composition.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> FilteredVisibilityView;
        /// Stores resolved world positions with geometric coverage for bounded GTAO horizon integration.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> GeometryPosition;
        /// Stores resolved normal directions and per-material contour widths independently of alpha.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> GeometryNormal;
        /// Receives multisampled world positions without clamping scene coordinates.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> MultisampleGeometryPosition;
        /// Receives multisampled normals and contour eligibility.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> MultisampleGeometryNormal;
        /// Binds world positions as the fifth geometry attachment.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> GeometryPositionTarget;
        /// Binds normals and contour widths as the sixth geometry attachment.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> GeometryNormalTarget;
        /// Samples resolved positions during current-frame GTAO.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GeometryPositionView;
        /// Samples resolved normals and per-material contour eligibility.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GeometryNormalView;
        /// Selects explicitly registered toon shading for the current draw independently of the camera default.
        bool MaterialToonEnabled = false;
        /// Emits only the auxiliary contour mask, without color, emission or geometry auxiliaries.
        bool MaterialOutlineOnly = false;
        /// Writes only the independent auxiliary contour mask, without evaluating lighting or textures.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> OutlineOnlyPixelShader;
        /// Scales current material contour widths while preserving a sixteen-pixel absolute bound.
        float MaterialOutlineScale = 1.0f;
        /// Stores the current material conductor weight independently of global pearl and reflection settings.
        float MaterialMetallic = 0.0f;
        /// Skips tile generation and contour texture reads when this frame submitted no outlined material.
        bool OutlinedGeometrySubmitted = false;
        /// Skips the separate auxiliary contour pass unless explicit outline-only geometry was submitted.
        bool AuxiliaryOutlinesSubmitted = false;
        /// Retains the caller's transparent-mask resource binding across composition.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PreviousAuxiliaryPartView;
        /// Restores the caller-owned depth sampling slot after composition.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PreviousContourDepthView;
        /// Composites only transparent ink, leaving the previously shaded scene intact.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> AuxiliaryOutlinePixelShader;
        /// Composites transparent ink from exact four-sample identities.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> AuxiliaryOutlineMultisamplePixelShader;
        /// Builds sparse contour occupancy for single-sample transparent geometry.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> AuxiliaryTilePixelShader;
        /// Builds sparse contour occupancy for four-sample transparent geometry.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> AuxiliaryTileMultisamplePixelShader;
        /// Bounds contour searches by the largest registered material width multiplier.
        float MaximumOutlineScale = 1.0f;
        /// Retains every additional shader resource slot touched by environment, glow and geometry.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> PreviousEffectViews[6];
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
#ifdef HELENGINE_LIGHTMAP
        /// <summary>Samples saved irradiance instead of evaluating dynamic diffuse transport.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> LightmapPixelShader;
        /// <summary>Reads UV1 only for seam-expanded baked draws, keeping ordinary vertex input unchanged.</summary>
        Microsoft::WRL::ComPtr<ID3D11VertexShader> LightmapVertexShader;
        /// <summary>Maps the independent eight-byte UV1 vertex stream beside the original 32-byte vertex stream.</summary>
        Microsoft::WRL::ComPtr<ID3D11InputLayout> LightmapInputLayout;
        /// <summary>Retains authored pearl effects with baked diffuse irradiance.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> LightmapPearlPixelShader;
#endif
        /// Carries physical-pixel widths, lighting bands and viewport dimensions to shaders.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Parameters;
        /// Stores multisampled geometry color when coverage is enabled.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Color;
        /// Stores camera-local unsigned part identities without resolving or averaging samples.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Parts;
        /// Stores transparent part identity and quantized outline width independently from opaque ownership.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> AuxiliaryParts;
        /// Receives the eighth geometry output without blending integer identities.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> AuxiliaryPartTarget;
        /// Reads exact transparent coverage samples during the separate contour pass.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> AuxiliaryPartView;
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
        /// Samples the same opaque depth per MSAA sample to reject expanded contours behind foreground objects.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> DepthView;
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
        /// Allocates a distinct silhouette identity for each mesh submitted in this camera frame.
        uint32_t NextMeshIdentifier = 0;
        /// Reuses one mesh identity across its material slots; material boundaries are not contours.
        std::unordered_map<const void*,uint32_t> MeshIdentifiers;

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
        /// <summary>Stores the active perspective clip planes; zero near selects identity clip-space depth.</summary>
        float DepthNear = 0, DepthFar = 1;
        /// Stores the total centered internal stroke width in physical viewport pixels.
        float InsidePixels = 0.0f;
        /// Stores the number of diffuse light levels used by this camera only.
        int Bands = 3;
    };
}
