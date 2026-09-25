#include "platform/windows/win32/win32_idle_throttle_settings.hpp"

#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    /// Creates validated idle-throttle settings.
    /// <param name="enabled">Whether the player throttles its frame rate while idle.</param>
    /// <param name="idleAfterMilliseconds">Milliseconds without activity before the player counts as idle; must be positive.</param>
    /// <param name="idleFramesPerSecond">Frame rate used while idle; must be between 1 and 30.</param>
    /// <param name="source">Where the configuration came from: "profile", "commandLine" or "mixed".</param>
    /// <exception cref="std::invalid_argument">Thrown when any value is outside its supported range.</exception>
    Win32IdleThrottleSettings::Win32IdleThrottleSettings(bool enabled, int idleAfterMilliseconds, int idleFramesPerSecond, const std::string& source)
        : Enabled(enabled)
        , IdleAfterMilliseconds(idleAfterMilliseconds)
        , IdleFramesPerSecond(idleFramesPerSecond)
        , Source(source) {
        if (idleAfterMilliseconds <= 0) {
            throw std::invalid_argument("Idle throttle idleAfterMilliseconds must be positive.");
        }

        if (idleFramesPerSecond < 1 || idleFramesPerSecond > 30) {
            throw std::invalid_argument("Idle throttle idleFramesPerSecond must be between 1 and 30.");
        }

        if (source != "profile" && source != "commandLine" && source != "mixed") {
            throw std::invalid_argument("Idle throttle source must be profile, commandLine or mixed, got: " + source);
        }
    }

    /// Gets whether the player throttles its frame rate while idle.
    bool Win32IdleThrottleSettings::IsEnabled() const {
        return Enabled;
    }

    /// Gets how many milliseconds without activity must pass before the player counts as idle.
    int Win32IdleThrottleSettings::GetIdleAfterMilliseconds() const {
        return IdleAfterMilliseconds;
    }

    /// Gets the frame rate the player targets while idle.
    int Win32IdleThrottleSettings::GetIdleFramesPerSecond() const {
        return IdleFramesPerSecond;
    }

    /// Gets where the configuration came from: "profile", "commandLine" or "mixed".
    const std::string& Win32IdleThrottleSettings::GetSource() const {
        return Source;
    }

    /// Resolves the effective settings: every idle command-line flag that was supplied overrides the matching
    /// profile value, and every flag that was not supplied keeps the profile value. The source is "commandLine"
    /// when only idle flags configured the throttle, "profile" when no idle flag was supplied, and "mixed" when the
    /// profile contained idle fields and idle flags were supplied as well.
    /// <param name="profile">Runtime player profile resolved from profile.json.</param>
    /// <param name="options">Command-line options parsed at startup.</param>
    /// <returns>The validated effective settings.</returns>
    Win32IdleThrottleSettings Win32IdleThrottleSettings::Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& options) {
        bool enabled = options.HasIdleThrottle() ? options.GetIdleThrottleEnabled() : profile.IdleThrottleEnabled;
        int idleAfterMilliseconds = options.HasIdleAfterMilliseconds() ? options.GetIdleAfterMilliseconds() : profile.IdleAfterMilliseconds;
        int idleFramesPerSecond = options.HasIdleFramesPerSecond() ? options.GetIdleFramesPerSecond() : profile.IdleFramesPerSecond;

        bool commandLineSupplied = options.HasIdleThrottle() || options.HasIdleAfterMilliseconds() || options.HasIdleFramesPerSecond();
        std::string source = "profile";
        if (commandLineSupplied && profile.IdleFieldsPresent) {
            source = "mixed";
        } else if (commandLineSupplied) {
            source = "commandLine";
        }

        return Win32IdleThrottleSettings(enabled, idleAfterMilliseconds, idleFramesPerSecond, source);
    }

    /// Describes the effective configuration as one log-friendly line:
    /// `idleThrottle=<on|off> afterMs=<n> fps=<n> source=<profile|commandLine|mixed>`.
    std::string Win32IdleThrottleSettings::Describe() const {
        std::ostringstream builder;
        builder << "idleThrottle=" << (Enabled ? "on" : "off")
            << " afterMs=" << IdleAfterMilliseconds
            << " fps=" << IdleFramesPerSecond
            << " source=" << Source;
        return builder.str();
    }
}
