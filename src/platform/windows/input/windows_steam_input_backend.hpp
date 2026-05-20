#pragma once

#include <string>

#include "platform/windows/input/i_windows_gamepad_backend.hpp"

namespace helengine::windows {
    /// Captures Steam-managed gamepads through the SteamInput controller roster and their emulated gamepad slots.
    class WindowsSteamInputBackend : public IWindowsGamepadBackend {
    public:
        /// Creates one SteamInput backend with no initialized Steam API state.
        WindowsSteamInputBackend();

        /// Shuts SteamInput and SteamAPI down when the backend owns them.
        ~WindowsSteamInputBackend() override;

        /// Returns which backend this adapter represents.
        WindowsGamepadBackendKind GetBackendKind() const override;

        /// Initializes SteamAPI and SteamInput.
        bool TryInitialize(std::string& failureMessage) override;

        /// Captures the current Steam-managed controllers.
        void CaptureFrame(List<WindowsGamepadDeviceState>& devices) override;

    private:
        /// Tracks whether SteamAPI was initialized successfully by this backend.
        bool SteamApiInitialized;

        /// Tracks whether SteamInput was initialized successfully by this backend.
        bool SteamInputInitialized;
    };
}
