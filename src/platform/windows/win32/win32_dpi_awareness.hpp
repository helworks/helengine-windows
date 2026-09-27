#pragma once

namespace helengine::windows {
    /// Selects whether the native player opts into Per-Monitor v2 DPI awareness or keeps today's default, DPI-unaware
    /// behavior, where Windows itself scales the whole window for the process instead of the process handling
    /// scaling on its own.
    enum class Win32DpiAwareness {
        /// The default: today's DPI-unaware behavior, unchanged from before this option existed.
        Unaware,

        /// Opts into Per-Monitor v2 DPI awareness through SetProcessDpiAwarenessContext, so the process receives
        /// real physical-pixel coordinates and DPI-change notifications instead of being scaled by Windows.
        PerMonitorV2
    };
}
