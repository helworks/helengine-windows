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

        /// Reads the raw JSON text content of the profile file at the supplied path.
        std::string ReadProfileFileContents(const std::filesystem::path& profilePath) const;

        /// Writes one runtime profile to the supplied profile path.
        void WriteProfile(const std::filesystem::path& profilePath, const RuntimePlayerProfile& profile) const;

        /// Parses one runtime profile's resolution fields from its JSON text payload.
        RuntimePlayerProfile ParseProfileJson(const std::string& json) const;

        /// Parses one required integer property value from the JSON profile payload.
        int ParseRequiredInteger(const std::string& json, const char* propertyName) const;

        /// Parses one optional integer property from the JSON profile payload, if present. Returns whether the
        /// property was found; throws RuntimePlayerProfileConfigurationError when it is present but its value
        /// cannot be parsed as an integer.
        bool TryParseOptionalInteger(const std::string& json, const char* propertyName, int& value) const;

        /// Parses one optional boolean property from the JSON profile payload, if present. Returns whether the
        /// property was found; throws RuntimePlayerProfileConfigurationError when it is present with any value
        /// other than exactly `true` or `false`.
        bool TryParseOptionalBoolean(const std::string& json, const char* propertyName, bool& value) const;

        /// Validates the idle-throttle fields resolved onto the supplied profile, throwing
        /// RuntimePlayerProfileConfigurationError when IdleAfterMilliseconds is not positive or
        /// IdleFramesPerSecond falls outside the supported 1..30 range.
        void ValidateIdleFields(const RuntimePlayerProfile& profile) const;

        /// Builds the persisted JSON payload for one runtime player profile.
        std::string BuildProfileJson(const RuntimePlayerProfile& profile) const;
    };
}
