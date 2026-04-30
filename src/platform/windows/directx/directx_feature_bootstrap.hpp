#pragma once

namespace helengine::windows {
    /// Registers the DirectX-side slices that correspond to the enabled generated-core features.
    class DirectXFeatureBootstrap {
    public:
        /// Registers every DirectX subsystem that is still enabled after C++ feature pruning.
        static void RegisterEnabledFeatures();

    private:
        /// Registers the shared 2D rendering path.
        static void RegisterRender2D();

        /// Registers the sprite rendering path.
        static void RegisterSprites();

        /// Registers the text rendering path.
        static void RegisterText2D();

        /// Registers the shader compilation and binding path.
        static void RegisterShaders();

        /// Registers the debug overlay path.
        static void RegisterDebugOverlay();
    };
}
