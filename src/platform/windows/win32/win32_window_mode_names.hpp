#pragma once

#include <string>

#include "platform/windows/win32/win32_overlay_background.hpp"
#include "platform/windows/win32/win32_overlay_bounds.hpp"
#include "platform/windows/win32/win32_window_mode.hpp"

namespace helengine::windows {
    /// Single source of truth for the exact, case-sensitive text accepted for windowMode, overlayBounds and
    /// overlayBackground, and for how each enum value renders back to text. RuntimePlayerProfileLoader (validating
    /// profile.json), Win32CommandLineOptions (validating command-line flag values) and Win32WindowModeSettings
    /// (validating the profile default when no command-line override was supplied, and formatting Describe()) all
    /// parse and format through this class instead of each keeping its own literal table, so the accepted values
    /// and their spellings can never drift apart.
    class Win32WindowModeNames {
    public:
        /// Parses a windowMode string into its enum value. Returns false, leaving windowMode unchanged, when text
        /// is not exactly "normal" or "overlay".
        static bool TryParseWindowMode(const std::string& text, Win32WindowMode& windowMode);

        /// Parses an overlayBounds string into its enum value. Returns false, leaving overlayBounds unchanged, when
        /// text is not exactly "monitor" or "profile".
        static bool TryParseOverlayBounds(const std::string& text, Win32OverlayBounds& overlayBounds);

        /// Parses an overlayBackground string into its enum value. Returns false, leaving overlayBackground
        /// unchanged, when text is not exactly "camera" or "transparent".
        static bool TryParseOverlayBackground(const std::string& text, Win32OverlayBackground& overlayBackground);

        /// Returns the exact accepted text for a window mode: "normal" or "overlay".
        static const char* ToText(Win32WindowMode windowMode);

        /// Returns the exact accepted text for an overlay bounds source: "monitor" or "profile".
        static const char* ToText(Win32OverlayBounds overlayBounds);

        /// Returns the exact accepted text for an overlay background: "camera" or "transparent".
        static const char* ToText(Win32OverlayBackground overlayBackground);
    };
}
