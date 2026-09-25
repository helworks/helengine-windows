#pragma once

#include <string>

#include "platform/windows/runtime/runtime_player_profile.hpp"
#include "platform/windows/win32/win32_command_line_options.hpp"

namespace helengine::windows {
    /// Stores the effective idle-throttle configuration of the native player after the command line has been applied
    /// over the runtime profile. The values are validated on construction, so an instance always holds a usable
    /// configuration.
    class Win32IdleThrottleSettings {
    public:
        /// Creates validated idle-throttle settings.
        /// <param name="enabled">Whether the player throttles its frame rate while idle.</param>
        /// <param name="idleAfterMilliseconds">Milliseconds without activity before the player counts as idle; must be positive.</param>
        /// <param name="idleFramesPerSecond">Frame rate used while idle; must be between 1 and 30.</param>
        /// <param name="source">Where the configuration came from: "profile", "commandLine" or "mixed".</param>
        /// <exception cref="std::invalid_argument">Thrown when any value is outside its supported range.</exception>
        Win32IdleThrottleSettings(bool enabled, int idleAfterMilliseconds, int idleFramesPerSecond, const std::string& source);

        /// Gets whether the player throttles its frame rate while idle.
        bool IsEnabled() const;

        /// Gets how many milliseconds without activity must pass before the player counts as idle.
        int GetIdleAfterMilliseconds() const;

        /// Gets the frame rate the player targets while idle.
        int GetIdleFramesPerSecond() const;

        /// Gets where the configuration came from: "profile", "commandLine" or "mixed".
        const std::string& GetSource() const;

        /// Resolves the effective settings: every idle command-line flag that was supplied overrides the matching
        /// profile value, and every flag that was not supplied keeps the profile value.
        /// <param name="profile">Runtime player profile resolved from profile.json.</param>
        /// <param name="options">Command-line options parsed at startup.</param>
        /// <returns>The validated effective settings.</returns>
        static Win32IdleThrottleSettings Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& options);

        /// Describes the effective configuration as one log-friendly line:
        /// `idleThrottle=<on|off> afterMs=<n> fps=<n> source=<profile|commandLine|mixed>`.
        std::string Describe() const;

    private:
        /// Stores whether the player throttles its frame rate while idle.
        bool Enabled;

        /// Stores how many milliseconds without activity must pass before the player counts as idle.
        int IdleAfterMilliseconds;

        /// Stores the frame rate the player targets while idle.
        int IdleFramesPerSecond;

        /// Stores where the configuration came from: "profile", "commandLine" or "mixed".
        std::string Source;
    };
}
