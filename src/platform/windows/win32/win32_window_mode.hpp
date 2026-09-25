#pragma once

namespace helengine::windows {
    /// Selects how the main native window presents itself: today's ordinary bordered, taskbar-visible window, or a
    /// borderless, always-on-top overlay composed with per-pixel alpha through DirectComposition.
    enum class Win32WindowMode {
        /// The default: today's ordinary window, unchanged from before this mode existed.
        Normal,

        /// A borderless, topmost, click-through-capable overlay window.
        Overlay
    };
}
