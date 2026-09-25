#pragma once

namespace helengine::windows {
    /// Selects how the overlay window's position and size are resolved. Only meaningful in the overlay window mode.
    enum class Win32OverlayBounds {
        /// The default: the primary monitor's full bounds.
        Monitor,

        /// The profile's resolution, positioned at the primary monitor's top-left. Exists for deterministic tests.
        Profile
    };
}
