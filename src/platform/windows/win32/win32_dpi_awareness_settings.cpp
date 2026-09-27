#include "platform/windows/win32/win32_dpi_awareness_settings.hpp"

#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    /// Creates validated DPI-awareness settings.
    /// <param name="dpiAwareness">Whether the process opts into Per-Monitor v2 DPI awareness.</param>
    /// <param name="source">Where the configuration came from: "profile", "commandLine" or "default".</param>
    /// <exception cref="std::invalid_argument">Thrown when source is not one of the three accepted values.</exception>
    Win32DpiAwarenessSettings::Win32DpiAwarenessSettings(Win32DpiAwareness dpiAwareness, const std::string& source)
        : DpiAwareness(dpiAwareness)
        , Source(source) {
        if (source != "profile" && source != "commandLine" && source != "default") {
            throw std::invalid_argument("DPI awareness source must be profile, commandLine or default, got: " + source);
        }
    }

    /// Gets whether the process opts into Per-Monitor v2 DPI awareness.
    Win32DpiAwareness Win32DpiAwarenessSettings::GetDpiAwareness() const {
        return DpiAwareness;
    }

    /// Gets whether GetDpiAwareness() is Win32DpiAwareness::PerMonitorV2.
    bool Win32DpiAwarenessSettings::IsPerMonitorV2() const {
        return DpiAwareness == Win32DpiAwareness::PerMonitorV2;
    }

    /// Gets where the configuration came from: "profile", "commandLine" or "default".
    const std::string& Win32DpiAwarenessSettings::GetSource() const {
        return Source;
    }

    /// Resolves the effective settings: --dpi-awareness, when supplied, overrides the profile's dpiAwareness field;
    /// otherwise the profile's dpiAwareness field is used if present; otherwise the default is
    /// Win32DpiAwareness::Unaware. The source is "commandLine" when --dpi-awareness was supplied, "profile" when it
    /// was not but the profile contained a dpiAwareness field, and "default" when neither was supplied.
    /// <param name="profile">Runtime player profile resolved from profile.json.</param>
    /// <param name="commandLineOptions">Command-line options parsed at startup.</param>
    /// <returns>The validated effective settings.</returns>
    Win32DpiAwarenessSettings Win32DpiAwarenessSettings::Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& commandLineOptions) {
        Win32DpiAwareness dpiAwareness = Win32DpiAwareness::Unaware;
        if (commandLineOptions.HasDpiAwareness()) {
            dpiAwareness = commandLineOptions.GetDpiAwareness();
        } else if (!Win32DpiAwarenessNames::TryParse(profile.DpiAwareness, dpiAwareness)) {
            throw std::invalid_argument(
                "Runtime player profile dpiAwareness must be \"unaware\" or \"permonitorv2\", got: " + profile.DpiAwareness);
        }

        std::string source = "default";
        if (commandLineOptions.HasDpiAwareness()) {
            source = "commandLine";
        } else if (profile.DpiAwarenessFieldPresent) {
            source = "profile";
        }

        return Win32DpiAwarenessSettings(dpiAwareness, source);
    }

    /// Describes the effective configuration as one log-friendly line:
    /// `dpiAwareness=<unaware|permonitorv2> source=<profile|commandLine|default>`.
    std::string Win32DpiAwarenessSettings::Describe() const {
        std::ostringstream builder;
        builder << "dpiAwareness=" << Win32DpiAwarenessNames::ToText(DpiAwareness) << " source=" << Source;
        return builder.str();
    }
}
