#pragma once

#include <filesystem>
#include <string>

#include "platform/windows/runtime/runtime_player_profile.hpp"

namespace helengine::windows {
    /// Loads and repairs the persisted runtime player profile beside the player executable.
    class RuntimePlayerProfileLoader {
    public:
        /// Resolves one runtime player profile from disk or seeds it from deployment defaults.
        RuntimePlayerProfile LoadOrCreateProfile(
            const std::filesystem::path& applicationDirectoryPath,
            int defaultResolutionWidth,
            int defaultResolutionHeight,
            std::string& lifecycleMessage) const;

    private:
        /// Resolves the absolute profile path beside the executable.
        std::filesystem::path ResolveProfilePath(const std::filesystem::path& applicationDirectoryPath) const;

        /// Creates one validated runtime profile from the generated deployment defaults.
        RuntimePlayerProfile CreateDefaultProfile(int defaultResolutionWidth, int defaultResolutionHeight) const;

        /// Reads one persisted runtime profile from the supplied profile path.
        RuntimePlayerProfile ReadProfile(const std::filesystem::path& profilePath) const;

        /// Writes one runtime profile to the supplied profile path.
        void WriteProfile(const std::filesystem::path& profilePath, const RuntimePlayerProfile& profile) const;

        /// Parses one runtime profile from its JSON text payload.
        RuntimePlayerProfile ParseProfileJson(const std::string& json) const;

        /// Parses one required integer property value from the JSON profile payload.
        int ParseRequiredInteger(const std::string& json, const char* propertyName) const;

        /// Builds the persisted JSON payload for one runtime player profile.
        std::string BuildProfileJson(const RuntimePlayerProfile& profile) const;
    };
}
