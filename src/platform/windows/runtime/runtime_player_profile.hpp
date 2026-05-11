#pragma once

namespace helengine::windows {
    /// Stores one resolved runtime player profile used by the native host at startup.
    struct RuntimePlayerProfile {
        /// Stores the initial startup window width in pixels.
        int ResolutionWidth;

        /// Stores the initial startup window height in pixels.
        int ResolutionHeight;

        /// Validates that the resolved profile contains usable startup values.
        void Validate() const;
    };
}
