#include "platform/windows/runtime/runtime_player_profile.hpp"

#include <stdexcept>

namespace helengine::windows {
    /// Validates that the resolved profile contains usable startup values.
    void RuntimePlayerProfile::Validate() const {
        if (ResolutionWidth <= 0) {
            throw std::runtime_error("Runtime player profile width must be positive.");
        } else if (ResolutionHeight <= 0) {
            throw std::runtime_error("Runtime player profile height must be positive.");
        }
    }
}
