#pragma once

#include <string>

#include "platform/windows/win32/win32_dpi_awareness.hpp"

namespace helengine::windows {
    /// Single source of truth for the exact, case-sensitive text accepted for dpiAwareness, and for how each enum
    /// value renders back to text. RuntimePlayerProfileLoader (validating profile.json), Win32CommandLineOptions
    /// (validating the command-line flag value) and Win32DpiAwarenessSettings (validating the profile default when
    /// no command-line override was supplied, and formatting Describe()) all parse and format through this class
    /// instead of each keeping its own literal table, so the accepted values and their spellings can never drift
    /// apart. Modeled on Win32WindowModeNames.
    class Win32DpiAwarenessNames {
    public:
        /// Parses a dpiAwareness string into its enum value. Returns false, leaving dpiAwareness unchanged, when
        /// text is not exactly "unaware" or "permonitorv2".
        static bool TryParse(const std::string& text, Win32DpiAwareness& dpiAwareness);

        /// Returns the exact accepted text for a DPI awareness value: "unaware" or "permonitorv2".
        static const char* ToText(Win32DpiAwareness dpiAwareness);
    };
}
