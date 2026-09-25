#include "platform/windows/win32/win32_window_mode_settings.hpp"

#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    /// Creates validated window-mode settings.
    /// <param name="windowMode">How the main window presents itself.</param>
    /// <param name="overlayBounds">How the overlay window's bounds are resolved; only meaningful in overlay mode.</param>
    /// <param name="overlayBackground">How the overlay window clears its back buffer; only meaningful in overlay mode.</param>
    /// <param name="source">Where the configuration came from: "profile", "commandLine" or "mixed".</param>
    /// <exception cref="std::invalid_argument">Thrown when source is not one of the three accepted values.</exception>
    Win32WindowModeSettings::Win32WindowModeSettings(
        Win32WindowMode windowMode,
        Win32OverlayBounds overlayBounds,
        Win32OverlayBackground overlayBackground,
        const std::string& source)
        : WindowMode(windowMode)
        , OverlayBounds(overlayBounds)
        , OverlayBackground(overlayBackground)
        , Source(source) {
        if (source != "profile" && source != "commandLine" && source != "mixed") {
            throw std::invalid_argument("Window mode source must be profile, commandLine or mixed, got: " + source);
        }
    }

    /// Gets how the main window presents itself.
    Win32WindowMode Win32WindowModeSettings::GetWindowMode() const {
        return WindowMode;
    }

    /// Gets how the overlay window's bounds are resolved; only meaningful when GetWindowMode() is Overlay.
    Win32OverlayBounds Win32WindowModeSettings::GetOverlayBounds() const {
        return OverlayBounds;
    }

    /// Gets how the overlay window clears its back buffer; only meaningful when GetWindowMode() is Overlay.
    Win32OverlayBackground Win32WindowModeSettings::GetOverlayBackground() const {
        return OverlayBackground;
    }

    /// Gets where the configuration came from: "profile", "commandLine" or "mixed".
    const std::string& Win32WindowModeSettings::GetSource() const {
        return Source;
    }

    /// Resolves the effective settings: every window-mode command-line flag that was supplied overrides the
    /// matching profile value, and every flag that was not supplied keeps the profile value. The source is
    /// "commandLine" when only command-line flags configured the window mode, "profile" when no window-mode flag
    /// was supplied, and "mixed" when the profile contained window-mode fields and window-mode flags were supplied
    /// as well.
    /// <param name="profile">Runtime player profile resolved from profile.json.</param>
    /// <param name="options">Command-line options parsed at startup.</param>
    /// <returns>The validated effective settings.</returns>
    Win32WindowModeSettings Win32WindowModeSettings::Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& options) {
        Win32WindowMode windowMode = options.HasWindowMode() ? options.GetWindowMode() : ParseProfileWindowMode(profile.WindowMode);
        Win32OverlayBounds overlayBounds = options.HasOverlayBounds() ? options.GetOverlayBounds() : ParseProfileOverlayBounds(profile.OverlayBounds);
        Win32OverlayBackground overlayBackground = options.HasOverlayBackground() ? options.GetOverlayBackground() : ParseProfileOverlayBackground(profile.OverlayBackground);

        bool commandLineSupplied = options.HasWindowMode() || options.HasOverlayBounds() || options.HasOverlayBackground();
        std::string source = "profile";
        if (commandLineSupplied && profile.WindowModeFieldsPresent) {
            source = "mixed";
        } else if (commandLineSupplied) {
            source = "commandLine";
        }

        if (options.HasHitTestProbe() && (!options.HasFrameLimit() || windowMode != Win32WindowMode::Overlay)) {
            throw std::invalid_argument(
                "Command-line flag --hit-test-probe requires --frames and an effective overlay window mode.");
        }

        return Win32WindowModeSettings(windowMode, overlayBounds, overlayBackground, source);
    }

    /// Describes the effective configuration as one log-friendly line:
    /// `windowMode=<normal|overlay> bounds=<monitor|profile> background=<camera|transparent> source=<profile|commandLine|mixed>`.
    std::string Win32WindowModeSettings::Describe() const {
        std::ostringstream builder;
        builder << "windowMode=" << (WindowMode == Win32WindowMode::Overlay ? "overlay" : "normal")
            << " bounds=" << (OverlayBounds == Win32OverlayBounds::Profile ? "profile" : "monitor")
            << " background=" << (OverlayBackground == Win32OverlayBackground::Transparent ? "transparent" : "camera")
            << " source=" << Source;
        return builder.str();
    }

    /// Parses the profile's windowMode string into its enum value, throwing std::invalid_argument when it is not
    /// exactly "normal" or "overlay". This is a defense-in-depth check: RuntimePlayerProfileLoader already rejects
    /// any other value while loading profile.json.
    Win32WindowMode Win32WindowModeSettings::ParseProfileWindowMode(const std::string& windowMode) {
        if (windowMode == "normal") {
            return Win32WindowMode::Normal;
        }

        if (windowMode == "overlay") {
            return Win32WindowMode::Overlay;
        }

        throw std::invalid_argument("Runtime player profile windowMode must be \"normal\" or \"overlay\", got: " + windowMode);
    }

    /// Parses the profile's overlayBounds string into its enum value, throwing std::invalid_argument when it is not
    /// exactly "monitor" or "profile". This is a defense-in-depth check: RuntimePlayerProfileLoader already rejects
    /// any other value while loading profile.json.
    Win32OverlayBounds Win32WindowModeSettings::ParseProfileOverlayBounds(const std::string& overlayBounds) {
        if (overlayBounds == "monitor") {
            return Win32OverlayBounds::Monitor;
        }

        if (overlayBounds == "profile") {
            return Win32OverlayBounds::Profile;
        }

        throw std::invalid_argument("Runtime player profile overlayBounds must be \"monitor\" or \"profile\", got: " + overlayBounds);
    }

    /// Parses the profile's overlayBackground string into its enum value, throwing std::invalid_argument when it is
    /// not exactly "camera" or "transparent". This is a defense-in-depth check: RuntimePlayerProfileLoader already
    /// rejects any other value while loading profile.json.
    Win32OverlayBackground Win32WindowModeSettings::ParseProfileOverlayBackground(const std::string& overlayBackground) {
        if (overlayBackground == "camera") {
            return Win32OverlayBackground::Camera;
        }

        if (overlayBackground == "transparent") {
            return Win32OverlayBackground::Transparent;
        }

        throw std::invalid_argument("Runtime player profile overlayBackground must be \"camera\" or \"transparent\", got: " + overlayBackground);
    }
}
