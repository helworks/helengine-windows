#pragma once

#include <string>

#include "platform/windows/runtime/runtime_player_profile.hpp"
#include "platform/windows/win32/win32_command_line_options.hpp"
#include "platform/windows/win32/win32_dpi_awareness.hpp"
#include "platform/windows/win32/win32_dpi_awareness_names.hpp"

namespace helengine::windows {
    /// Stores the effective, opt-in DPI-awareness configuration of the native player after the command line has
    /// been applied over the runtime profile: whether the main window stays DPI-unaware (today's default,
    /// unchanged behavior) or opts into Per-Monitor v2 awareness. The value is validated on construction, so an
    /// instance always holds a usable configuration, and an invalid profile value already fails startup before the
    /// window is created. Modeled on Win32WindowModeSettings.
    class Win32DpiAwarenessSettings {
    public:
        /// Creates validated DPI-awareness settings.
        /// <param name="dpiAwareness">Whether the process opts into Per-Monitor v2 DPI awareness.</param>
        /// <param name="source">Where the configuration came from: "profile", "commandLine" or "default".</param>
        /// <exception cref="std::invalid_argument">Thrown when source is not one of the three accepted values.</exception>
        Win32DpiAwarenessSettings(Win32DpiAwareness dpiAwareness, const std::string& source);

        /// Gets whether the process opts into Per-Monitor v2 DPI awareness.
        Win32DpiAwareness GetDpiAwareness() const;

        /// Gets whether GetDpiAwareness() is Win32DpiAwareness::PerMonitorV2.
        bool IsPerMonitorV2() const;

        /// Gets where the configuration came from: "profile", "commandLine" or "default".
        const std::string& GetSource() const;

        /// Resolves the effective settings: --dpi-awareness, when supplied, overrides the profile's dpiAwareness
        /// field; otherwise the profile's dpiAwareness field is used if present (validated through
        /// Win32DpiAwarenessNames as a defense-in-depth check; RuntimePlayerProfileLoader already rejects any other
        /// profile value while loading profile.json); otherwise the default is Win32DpiAwareness::Unaware.
        /// <param name="profile">Runtime player profile resolved from profile.json.</param>
        /// <param name="commandLineOptions">Command-line options parsed at startup.</param>
        /// <returns>The validated effective settings.</returns>
        static Win32DpiAwarenessSettings Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& commandLineOptions);

        /// Describes the effective configuration as one log-friendly line:
        /// `dpiAwareness=<unaware|permonitorv2> source=<profile|commandLine|default>`.
        std::string Describe() const;

    private:
        /// Stores whether the process opts into Per-Monitor v2 DPI awareness.
        Win32DpiAwareness DpiAwareness;

        /// Stores where the configuration came from: "profile", "commandLine" or "default".
        std::string Source;
    };
}
