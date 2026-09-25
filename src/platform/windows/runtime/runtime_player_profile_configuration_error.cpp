#include "platform/windows/runtime/runtime_player_profile_configuration_error.hpp"

namespace helengine::windows {
    /// Creates a configuration error carrying a readable reason for the startup log.
    RuntimePlayerProfileConfigurationError::RuntimePlayerProfileConfigurationError(const std::string& message)
        : std::runtime_error(message) {
    }
}
