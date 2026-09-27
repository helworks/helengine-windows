#include "platform/windows/win32/win32_dpi_awareness_names.hpp"

#include <stdexcept>

namespace helengine::windows {
    /// Parses a dpiAwareness string into its enum value. Returns false, leaving dpiAwareness unchanged, when text
    /// is not exactly "unaware" or "permonitorv2".
    bool Win32DpiAwarenessNames::TryParse(const std::string& text, Win32DpiAwareness& dpiAwareness) {
        if (text == "unaware") {
            dpiAwareness = Win32DpiAwareness::Unaware;
            return true;
        }

        if (text == "permonitorv2") {
            dpiAwareness = Win32DpiAwareness::PerMonitorV2;
            return true;
        }

        return false;
    }

    /// Returns the exact accepted text for a DPI awareness value: "unaware" or "permonitorv2".
    const char* Win32DpiAwarenessNames::ToText(Win32DpiAwareness dpiAwareness) {
        switch (dpiAwareness) {
            case Win32DpiAwareness::Unaware:
                return "unaware";
            case Win32DpiAwareness::PerMonitorV2:
                return "permonitorv2";
        }

        throw std::invalid_argument("Unrecognized Win32DpiAwareness value.");
    }
}
