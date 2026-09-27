#pragma once

#include <string>

namespace helengine::windows {
    /// Stores one resolved runtime player profile used by the native host at startup.
    struct RuntimePlayerProfile {
        /// Stores the initial startup window width in pixels.
        int ResolutionWidth;

        /// Stores the initial startup window height in pixels.
        int ResolutionHeight;

        /// Stores whether the player should throttle its render frame rate while idle (no player input for at
        /// least IdleAfterMilliseconds). Defaults to disabled so profiles that never mention the idle fields
        /// keep the player's original, unthrottled behavior.
        bool IdleThrottleEnabled = false;

        /// Stores how many milliseconds of continuous input inactivity must elapse before the idle throttle
        /// engages. Only meaningful when IdleThrottleEnabled is true.
        int IdleAfterMilliseconds = 500;

        /// Stores the render frame rate the player targets once idle-throttled. Only meaningful when
        /// IdleThrottleEnabled is true.
        int IdleFramesPerSecond = 10;

        /// Stores whether any idle-throttle field was present in the persisted profile.json. The loader uses
        /// this to omit the idle section when rewriting a profile that never mentioned it, keeping seeded and
        /// repaired files byte-identical to the pre-idle-throttle format.
        bool IdleFieldsPresent = false;

        /// Stores the requested main-window presentation mode: "normal" (default, today's ordinary window) or
        /// "overlay" (a borderless, always-on-top window composed with per-pixel alpha). The loader validates this
        /// against the exact accepted values whenever it is present in profile.json.
        std::string WindowMode = "normal";

        /// Stores how the overlay window's bounds are resolved: "monitor" (default; the primary monitor's full
        /// bounds) or "profile" (the profile resolution at the primary monitor's top-left). Only meaningful when
        /// WindowMode is "overlay".
        std::string OverlayBounds = "monitor";

        /// Stores how the overlay window clears its back buffer: "camera" (default; each camera's clear color,
        /// premultiplied) or "transparent" (always clears to fully transparent). Only meaningful when WindowMode is
        /// "overlay".
        std::string OverlayBackground = "camera";

        /// Stores whether any window-mode field (windowMode, overlayBounds or overlayBackground) was present in the
        /// persisted profile.json. The loader uses this to omit the window-mode section when rewriting a profile
        /// that never mentioned it, keeping seeded and repaired files byte-identical to the pre-window-mode format.
        bool WindowModeFieldsPresent = false;

        /// Stores the requested opt-in DPI-awareness value: "unaware" (default; today's DPI-unaware behavior) or
        /// "permonitorv2" (opts into Per-Monitor v2 DPI awareness). The loader validates this against the exact
        /// accepted values whenever it is present in profile.json.
        std::string DpiAwareness = "unaware";

        /// Stores whether the dpiAwareness field was present in the persisted profile.json. The loader uses this to
        /// omit the field when rewriting a profile that never mentioned it, keeping seeded and repaired files
        /// byte-identical to the pre-DPI-awareness format.
        bool DpiAwarenessFieldPresent = false;

        /// Validates that the resolved profile contains usable startup values.
        void Validate() const;
    };
}
