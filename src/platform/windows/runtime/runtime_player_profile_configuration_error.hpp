#pragma once

#include <stdexcept>
#include <string>

namespace helengine::windows {
    /// Signals that the persisted runtime player profile carries an idle-throttle configuration value the
    /// player refuses to silently repair (for example a malformed boolean flag, a non-integer duration, or a
    /// frame rate outside the supported range). RuntimePlayerProfileLoader throws this outside its
    /// resolution-repair path so it always propagates instead of being swallowed and rewritten with defaults;
    /// Win32Application::ResolveRuntimePlayerProfile converts it into a Win32ExitRequest with exit code 2.
    class RuntimePlayerProfileConfigurationError : public std::runtime_error {
    public:
        /// Creates a configuration error carrying a readable reason for the startup log.
        explicit RuntimePlayerProfileConfigurationError(const std::string& message);
    };
}
