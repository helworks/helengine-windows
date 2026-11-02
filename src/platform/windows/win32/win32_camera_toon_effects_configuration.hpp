#pragma once

namespace helengine::windows {
    /// Defines bounded camera-local effects independently of outline widths and lighting bands.
    struct CameraToonEffectsConfiguration {
        /// Enables lighting quantization and geometric contours while retaining independent effects.
        bool ToonEnabled = true;
        /// Permits explicitly registered materials to sample the camera's uploaded environment.
        bool ReflectionsEnabled = false;
        /// Scales reflected environment radiance from zero to one.
        float ReflectionStrength = 0.0f;
        /// Permits explicitly selected emissive materials to contribute glow.
        bool BloomEnabled = false;
        /// Scales selected glow from zero to two.
        float BloomStrength = 0.0f;
        /// Rejects emitted radiance below this zero-to-four threshold before blurring.
        float BloomThreshold = 1.0f;
        /// Sets the glow spread in zero-to-twenty-four physical viewport pixels.
        float BloomRadius = 0.0f;
        /// Selects the separate pearl shader only for explicitly eligible material instances.
        bool PearlEnabled = false;
        /// Blends the angular pink-to-cyan coating from zero to one.
        float PearlStrength = 0.0f;
        /// Scales the broad near-white direct-light highlight from zero to one.
        float PearlHighlight = 0.0f;
        /// Controls the direct highlight width from tight zero to broad one.
        float PearlWidth = 0.45f;
        /// Applies exposure and an SDR filmic curve once after linear scene lighting and bloom.
        bool ToneMappingEnabled = false;
        /// Multiplies linear radiance before tone mapping, from zero to sixteen.
        float Exposure = 1.0f;
        /// Scales scene exposure only for SDR presentation, preserving HDR radiance and UI white.
        float SdrExposure = 1.0f;
        /// Enables bounded geometry-aware contact occlusion independently from toon shading.
        bool AmbientOcclusionEnabled = false;
        /// Blends contact occlusion from zero to one without changing authored alpha.
        float AmbientOcclusionStrength = 0.0f;
        /// Bounds contact visibility to this zero-to-sixteen world-space distance.
        float AmbientOcclusionRadius = 0.6f;
        /// Rejects coplanar self-occlusion within this zero-to-one world-space offset.
        float AmbientOcclusionBias = 0.02f;
        /// Caps the three-slice GTAO horizon search to zero-to-sixty-four physical viewport pixels.
        float AmbientOcclusionPixelRadius = 24.0f;
        /// Sets the minimum sharp face-normal angle in degrees from zero to 180; 180 disables crease contours.
        float CreaseAngleDegrees = 45.0f;
        /// Enables the existing directional shadow atlas for receiving materials.
        bool ShadowsEnabled = false;
        /// Scales directional shadow occlusion from zero to one.
        float ShadowStrength = 0.0f;
        /// Sets the bounded PCF sampling radius from zero to three atlas texels.
        float ShadowSoftness = 0.0f;

        /// Selects the display-dependent scene exposure without changing geometry lighting or material emission.
        float OutputExposure(bool hdr) const { return Exposure*(hdr ? 1.0f : SdrExposure); }
    };
}
