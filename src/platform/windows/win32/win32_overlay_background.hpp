#pragma once

namespace helengine::windows {
    /// Selects how the overlay window clears its back buffer between frames. Only meaningful in the overlay window
    /// mode.
    enum class Win32OverlayBackground {
        /// The default: each camera's clear color, premultiplied.
        Camera,

        /// Clears to fully transparent (0,0,0,0) regardless of any camera's clear color.
        Transparent
    };
}
