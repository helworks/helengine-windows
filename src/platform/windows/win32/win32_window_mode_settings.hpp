#pragma once

#include <string>

#include "platform/windows/runtime/runtime_player_profile.hpp"
#include "platform/windows/win32/win32_command_line_options.hpp"
#include "platform/windows/win32/win32_overlay_background.hpp"
#include "platform/windows/win32/win32_overlay_bounds.hpp"
#include "platform/windows/win32/win32_window_mode.hpp"
#include "platform/windows/win32/win32_window_mode_names.hpp"

namespace helengine::windows {
    /// Stores the effective window-mode configuration of the native player after the command line has been applied
    /// over the runtime profile: whether the main window presents itself normally or as a borderless, always-on-top
    /// overlay, and, in overlay mode, how its bounds and back-buffer clear behavior are resolved. The values are
    /// validated on construction, so an instance always holds a usable configuration, and an invalid combination
    /// (for example a stray --hit-test-probe) already fails startup before the window is created.
    class Win32WindowModeSettings {
    public:
        /// Creates validated window-mode settings.
        /// <param name="windowMode">How the main window presents itself.</param>
        /// <param name="overlayBounds">How the overlay window's bounds are resolved; only meaningful in overlay mode.</param>
        /// <param name="overlayBackground">How the overlay window clears its back buffer; only meaningful in overlay mode.</param>
        /// <param name="source">Where the configuration came from: "profile", "commandLine" or "mixed".</param>
        /// <exception cref="std::invalid_argument">Thrown when source is not one of the three accepted values.</exception>
        Win32WindowModeSettings(
            Win32WindowMode windowMode,
            Win32OverlayBounds overlayBounds,
            Win32OverlayBackground overlayBackground,
            const std::string& source);

        /// Gets how the main window presents itself.
        Win32WindowMode GetWindowMode() const;

        /// Gets how the overlay window's bounds are resolved; only meaningful when GetWindowMode() is Overlay.
        Win32OverlayBounds GetOverlayBounds() const;

        /// Gets how the overlay window clears its back buffer; only meaningful when GetWindowMode() is Overlay.
        Win32OverlayBackground GetOverlayBackground() const;

        /// Gets where the configuration came from: "profile", "commandLine" or "mixed".
        const std::string& GetSource() const;

        /// Resolves the effective settings: every window-mode command-line flag that was supplied overrides the
        /// matching profile value, and every flag that was not supplied keeps the profile value (validated through
        /// Win32WindowModeNames as a defense-in-depth check; RuntimePlayerProfileLoader already rejects any other
        /// profile value while loading profile.json). Also throws std::invalid_argument when --hit-test-probe was
        /// supplied without both --frames and an effective overlay window mode, since the probe has nothing to
        /// sample and no frame to sample it on otherwise.
        /// <param name="profile">Runtime player profile resolved from profile.json.</param>
        /// <param name="options">Command-line options parsed at startup.</param>
        /// <returns>The validated effective settings.</returns>
        static Win32WindowModeSettings Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& options);

        /// Describes the effective configuration as one log-friendly line:
        /// `windowMode=<normal|overlay> bounds=<monitor|profile> background=<camera|transparent> source=<profile|commandLine|mixed>`.
        std::string Describe() const;

    private:
        /// Stores how the main window presents itself.
        Win32WindowMode WindowMode;

        /// Stores how the overlay window's bounds are resolved.
        Win32OverlayBounds OverlayBounds;

        /// Stores how the overlay window clears its back buffer.
        Win32OverlayBackground OverlayBackground;

        /// Stores where the configuration came from: "profile", "commandLine" or "mixed".
        std::string Source;
    };
}
