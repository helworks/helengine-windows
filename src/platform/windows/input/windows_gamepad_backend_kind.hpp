#pragma once

namespace helengine::windows {
    /// Identifies the Windows native backend that produced one gamepad snapshot.
    enum class WindowsGamepadBackendKind {
        /// Snapshot produced by the DirectInput backend.
        DirectInput = 0,

        /// Snapshot produced by the XInput backend.
        XInput = 1,

        /// Snapshot produced by the SteamInput backend.
        SteamInput = 2
    };
}
