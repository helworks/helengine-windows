#pragma once

#include "helcpp_config.hpp"

namespace helengine::windows {
    /// Exposes the generated core feature decisions as compile-time booleans for the Windows host.
    class WindowsFeatureFlags {
    public:
        /// Reports whether the shared 2D renderer is compiled into the generated core.
        static constexpr bool Render2DEnabled = HE_CPP_FEATURE_RENDER2D != 0;

        /// Reports whether sprite systems are compiled into the generated core.
        static constexpr bool SpritesEnabled = HE_CPP_FEATURE_SPRITES != 0;

        /// Reports whether text systems are compiled into the generated core.
        static constexpr bool Text2DEnabled = HE_CPP_FEATURE_TEXT2D != 0;

        /// Reports whether shader systems are compiled into the generated core.
        static constexpr bool ShadersEnabled = HE_CPP_FEATURE_SHADERS != 0;

        /// Reports whether the debug overlay is compiled into the generated core.
        static constexpr bool DebugOverlayEnabled = HE_CPP_FEATURE_DEBUGOVERLAY != 0;
    };
}
