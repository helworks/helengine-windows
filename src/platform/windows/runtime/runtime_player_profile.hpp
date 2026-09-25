#pragma once

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

        /// Validates that the resolved profile contains usable startup values.
        void Validate() const;
    };
}
