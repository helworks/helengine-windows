#include "platform/windows/win32/win32_window_mode_names.hpp"

#include <stdexcept>

namespace helengine::windows {
    /// Parses a windowMode string into its enum value. Returns false, leaving windowMode unchanged, when text is
    /// not exactly "normal" or "overlay".
    bool Win32WindowModeNames::TryParseWindowMode(const std::string& text, Win32WindowMode& windowMode) {
        if (text == "normal") {
            windowMode = Win32WindowMode::Normal;
            return true;
        }

        if (text == "overlay") {
            windowMode = Win32WindowMode::Overlay;
            return true;
        }

        return false;
    }

    /// Parses an overlayBounds string into its enum value. Returns false, leaving overlayBounds unchanged, when
    /// text is not exactly "monitor" or "profile".
    bool Win32WindowModeNames::TryParseOverlayBounds(const std::string& text, Win32OverlayBounds& overlayBounds) {
        if (text == "monitor") {
            overlayBounds = Win32OverlayBounds::Monitor;
            return true;
        }

        if (text == "profile") {
            overlayBounds = Win32OverlayBounds::Profile;
            return true;
        }

        return false;
    }

    /// Parses an overlayBackground string into its enum value. Returns false, leaving overlayBackground unchanged,
    /// when text is not exactly "camera" or "transparent".
    bool Win32WindowModeNames::TryParseOverlayBackground(const std::string& text, Win32OverlayBackground& overlayBackground) {
        if (text == "camera") {
            overlayBackground = Win32OverlayBackground::Camera;
            return true;
        }

        if (text == "transparent") {
            overlayBackground = Win32OverlayBackground::Transparent;
            return true;
        }

        return false;
    }

    /// Returns the exact accepted text for a window mode: "normal" or "overlay".
    const char* Win32WindowModeNames::ToText(Win32WindowMode windowMode) {
        switch (windowMode) {
            case Win32WindowMode::Normal:
                return "normal";
            case Win32WindowMode::Overlay:
                return "overlay";
        }

        throw std::invalid_argument("Unrecognized Win32WindowMode value.");
    }

    /// Returns the exact accepted text for an overlay bounds source: "monitor" or "profile".
    const char* Win32WindowModeNames::ToText(Win32OverlayBounds overlayBounds) {
        switch (overlayBounds) {
            case Win32OverlayBounds::Monitor:
                return "monitor";
            case Win32OverlayBounds::Profile:
                return "profile";
        }

        throw std::invalid_argument("Unrecognized Win32OverlayBounds value.");
    }

    /// Returns the exact accepted text for an overlay background: "camera" or "transparent".
    const char* Win32WindowModeNames::ToText(Win32OverlayBackground overlayBackground) {
        switch (overlayBackground) {
            case Win32OverlayBackground::Camera:
                return "camera";
            case Win32OverlayBackground::Transparent:
                return "transparent";
        }

        throw std::invalid_argument("Unrecognized Win32OverlayBackground value.");
    }
}
