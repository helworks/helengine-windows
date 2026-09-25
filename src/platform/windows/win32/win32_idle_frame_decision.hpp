#pragma once

namespace helengine::windows {
    /// Describes what the idle-throttled loop should do before its next frame.
    struct Win32IdleFrameDecision {
        /// Stores whether the player is active and should render immediately at the normal (vsync-paced) rate.
        bool Active;

        /// Stores how many milliseconds the loop should wait for messages before the next idle frame; always 0 when
        /// Active is true, and 0 when the next idle frame is already due.
        int WaitMilliseconds;
    };
}
