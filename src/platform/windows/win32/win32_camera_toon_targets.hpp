#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace helengine::windows {
    /// Shares compatible viewport scratch attachments only between non-overlapping camera passes on one device.
    struct Win32CameraToonTargets {
        /// Prevents borrowing targets while another camera is between Begin and Composite.
        bool Busy = false;
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
        /// Receives the reduced vertical glow filter.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> BloomVerticalTarget;
        /// Samples horizontal glow while filtering vertically.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> BloomHorizontalView;
        /// Samples the complete reduced glow during final premultiplied composition.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> BloomVerticalView;
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
        /// Receives one occupancy value per thirty-two-by-thirty-two geometry tile.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> OutlineTileTarget;
        /// Rejects distant contour searches with nine bounded coarse texture reads per pixel.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> OutlineTileView;
        /// Tracks current attachment dimensions to replace resources only when required.
        UINT Width = 0;
        /// Tracks current attachment height independently from viewport screen position.
        UINT Height = 0;
        /// Tracks the attachment sample count for 1x/4x resource compatibility.
        UINT Samples = 0;
    };
}
