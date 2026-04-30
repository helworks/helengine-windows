#include "platform/windows/directx/directx_feature_bootstrap.hpp"

#include "platform/windows/windows_feature_flags.hpp"
#include "runtime/feature_manifest.hpp"

namespace helengine::windows {
    /// Registers the DirectX-side slices that correspond to the enabled generated-core features.
    void DirectXFeatureBootstrap::RegisterEnabledFeatures() {
        std::size_t featureCount = 0;
        const HEFeatureEntry* featureEntries = he_get_feature_entries(&featureCount);

        for (std::size_t index = 0; index < featureCount; index++) {
            const HEFeatureEntry& featureEntry = featureEntries[index];
            if (!featureEntry.Enabled) {
                continue;
            }

            switch (featureEntry.Feature) {
                case HEFeature::Render2D:
#if HE_CPP_FEATURE_RENDER2D
                    RegisterRender2D();
#endif
                    break;

                case HEFeature::Sprites:
#if HE_CPP_FEATURE_SPRITES
                    RegisterSprites();
#endif
                    break;

                case HEFeature::Text2D:
#if HE_CPP_FEATURE_TEXT2D
                    RegisterText2D();
#endif
                    break;

                case HEFeature::Shaders:
#if HE_CPP_FEATURE_SHADERS
                    RegisterShaders();
#endif
                    break;

                case HEFeature::DebugOverlay:
#if HE_CPP_FEATURE_DEBUGOVERLAY
                    RegisterDebugOverlay();
#endif
                    break;
            }
        }
    }

    /// Reserves the DirectX registration point for the shared 2D renderer.
    void DirectXFeatureBootstrap::RegisterRender2D() {
        static_assert(WindowsFeatureFlags::Render2DEnabled, "Render2D registration must stay gated by the generated feature define.");
    }

    /// Reserves the DirectX registration point for sprite rendering.
    void DirectXFeatureBootstrap::RegisterSprites() {
        static_assert(WindowsFeatureFlags::SpritesEnabled, "Sprite registration must stay gated by the generated feature define.");
    }

    /// Reserves the DirectX registration point for text rendering.
    void DirectXFeatureBootstrap::RegisterText2D() {
        static_assert(WindowsFeatureFlags::Text2DEnabled, "Text registration must stay gated by the generated feature define.");
    }

    /// Reserves the DirectX registration point for shader systems.
    void DirectXFeatureBootstrap::RegisterShaders() {
        static_assert(WindowsFeatureFlags::ShadersEnabled, "Shader registration must stay gated by the generated feature define.");
    }

    /// Reserves the DirectX registration point for the debug overlay.
    void DirectXFeatureBootstrap::RegisterDebugOverlay() {
        static_assert(WindowsFeatureFlags::DebugOverlayEnabled, "Debug overlay registration must stay gated by the generated feature define.");
    }
}
