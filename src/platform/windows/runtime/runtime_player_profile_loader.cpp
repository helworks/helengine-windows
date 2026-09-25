#include "platform/windows/runtime/runtime_player_profile_loader.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "platform/windows/runtime/runtime_player_profile_configuration_error.hpp"

namespace helengine::windows {
    /// Resolves one runtime player profile from disk or seeds it from deployment defaults.
    RuntimePlayerProfile RuntimePlayerProfileLoader::LoadOrCreateProfile(
        const std::filesystem::path& applicationDirectoryPath,
        int defaultResolutionWidth,
        int defaultResolutionHeight,
        std::string& lifecycleMessage) const {
        RuntimePlayerProfile defaultProfile = CreateDefaultProfile(defaultResolutionWidth, defaultResolutionHeight);
        std::filesystem::path profilePath = ResolveProfilePath(applicationDirectoryPath);
        if (!std::filesystem::exists(profilePath)) {
            WriteProfile(profilePath, defaultProfile);
            lifecycleMessage = "profile.json was seeded from deployment defaults.";
            return defaultProfile;
        }

        RuntimePlayerProfile idleFieldsProfile = defaultProfile;
        try {
            std::string fileContents = ReadProfileFileContents(profilePath);

            bool idleThrottleEnabledPresent = TryParseOptionalBoolean(fileContents, "idleThrottleEnabled", idleFieldsProfile.IdleThrottleEnabled);
            bool idleAfterMillisecondsPresent = TryParseOptionalInteger(fileContents, "idleAfterMilliseconds", idleFieldsProfile.IdleAfterMilliseconds);
            bool idleFramesPerSecondPresent = TryParseOptionalInteger(fileContents, "idleFramesPerSecond", idleFieldsProfile.IdleFramesPerSecond);
            idleFieldsProfile.IdleFieldsPresent = idleThrottleEnabledPresent || idleAfterMillisecondsPresent || idleFramesPerSecondPresent;
            ValidateIdleFields(idleFieldsProfile);

            RuntimePlayerProfile profile = ParseProfileJson(fileContents);
            profile.IdleThrottleEnabled = idleFieldsProfile.IdleThrottleEnabled;
            profile.IdleAfterMilliseconds = idleFieldsProfile.IdleAfterMilliseconds;
            profile.IdleFramesPerSecond = idleFieldsProfile.IdleFramesPerSecond;
            profile.IdleFieldsPresent = idleFieldsProfile.IdleFieldsPresent;
            profile.Validate();
            lifecycleMessage = "profile.json loaded successfully.";
            return profile;
        } catch (const RuntimePlayerProfileConfigurationError&) {
            throw;
        } catch (const std::exception&) {
            RuntimePlayerProfile repairedProfile = defaultProfile;
            if (idleFieldsProfile.IdleFieldsPresent) {
                repairedProfile.IdleThrottleEnabled = idleFieldsProfile.IdleThrottleEnabled;
                repairedProfile.IdleAfterMilliseconds = idleFieldsProfile.IdleAfterMilliseconds;
                repairedProfile.IdleFramesPerSecond = idleFieldsProfile.IdleFramesPerSecond;
                repairedProfile.IdleFieldsPresent = true;
            }

            WriteProfile(profilePath, repairedProfile);
            lifecycleMessage = "profile.json was invalid and was recreated from deployment defaults.";
            return repairedProfile;
        }
    }

    /// Resolves the absolute profile path beside the executable.
    std::filesystem::path RuntimePlayerProfileLoader::ResolveProfilePath(const std::filesystem::path& applicationDirectoryPath) const {
        if (applicationDirectoryPath.empty()) {
            throw std::runtime_error("Application directory path is required to resolve profile.json.");
        }

        return applicationDirectoryPath / "profile.json";
    }

    /// Creates one validated runtime profile from the generated deployment defaults.
    RuntimePlayerProfile RuntimePlayerProfileLoader::CreateDefaultProfile(int defaultResolutionWidth, int defaultResolutionHeight) const {
        RuntimePlayerProfile profile {
            defaultResolutionWidth,
            defaultResolutionHeight
        };
        profile.Validate();
        return profile;
    }

    /// Reads the raw JSON text content of the profile file at the supplied path.
    std::string RuntimePlayerProfileLoader::ReadProfileFileContents(const std::filesystem::path& profilePath) const {
        std::ifstream stream(profilePath, std::ios::in | std::ios::binary);
        if (!stream.is_open()) {
            throw std::runtime_error("profile.json could not be opened for reading.");
        }

        std::ostringstream builder;
        builder << stream.rdbuf();
        if (stream.bad()) {
            throw std::runtime_error("profile.json could not be read.");
        }

        return builder.str();
    }

    /// Writes one runtime profile to the supplied profile path.
    void RuntimePlayerProfileLoader::WriteProfile(const std::filesystem::path& profilePath, const RuntimePlayerProfile& profile) const {
        profile.Validate();
        std::filesystem::create_directories(profilePath.parent_path());

        std::ofstream stream(profilePath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!stream.is_open()) {
            throw std::runtime_error("profile.json could not be opened for writing.");
        }

        std::string json = BuildProfileJson(profile);
        stream.write(json.c_str(), static_cast<std::streamsize>(json.size()));
        if (!stream.good()) {
            throw std::runtime_error("profile.json could not be written.");
        }
    }

    /// Parses one runtime profile from its JSON text payload.
    RuntimePlayerProfile RuntimePlayerProfileLoader::ParseProfileJson(const std::string& json) const {
        RuntimePlayerProfile profile {
            ParseRequiredInteger(json, "resolutionWidth"),
            ParseRequiredInteger(json, "resolutionHeight")
        };
        profile.Validate();
        return profile;
    }

    /// Parses one required integer property value from the JSON profile payload.
    int RuntimePlayerProfileLoader::ParseRequiredInteger(const std::string& json, const char* propertyName) const {
        if (propertyName == nullptr || propertyName[0] == '\0') {
            throw std::runtime_error("A profile property name is required.");
        }

        std::string propertyToken = "\"";
        propertyToken += propertyName;
        propertyToken += "\"";
        std::size_t propertyIndex = json.find(propertyToken);
        if (propertyIndex == std::string::npos) {
            throw std::runtime_error("profile.json is missing a required property.");
        }

        std::size_t colonIndex = json.find(':', propertyIndex + propertyToken.length());
        if (colonIndex == std::string::npos) {
            throw std::runtime_error("profile.json contains an invalid property assignment.");
        }

        std::size_t valueStartIndex = colonIndex + 1;
        while (valueStartIndex < json.length() && std::isspace(static_cast<unsigned char>(json[valueStartIndex])) != 0) {
            valueStartIndex++;
        }

        if (valueStartIndex >= json.length()) {
            throw std::runtime_error("profile.json ended before a property value was found.");
        }

        std::size_t valueEndIndex = valueStartIndex;
        if (json[valueEndIndex] == '-') {
            valueEndIndex++;
        }

        while (valueEndIndex < json.length() && std::isdigit(static_cast<unsigned char>(json[valueEndIndex])) != 0) {
            valueEndIndex++;
        }

        if (valueEndIndex == valueStartIndex || (json[valueStartIndex] == '-' && valueEndIndex == valueStartIndex + 1)) {
            throw std::runtime_error("profile.json contains a non-integer property value.");
        }

        std::string valueText = json.substr(valueStartIndex, valueEndIndex - valueStartIndex);
        try {
            return std::stoi(valueText);
        } catch (const std::exception&) {
            throw std::runtime_error("profile.json contains an integer that could not be parsed.");
        }
    }

    /// Parses one optional integer property from the JSON profile payload, if present. Returns whether the
    /// property was found; throws RuntimePlayerProfileConfigurationError when it is present but its value
    /// cannot be parsed as an integer.
    bool RuntimePlayerProfileLoader::TryParseOptionalInteger(const std::string& json, const char* propertyName, int& value) const {
        if (propertyName == nullptr || propertyName[0] == '\0') {
            throw std::runtime_error("A profile property name is required.");
        }

        std::string propertyToken = "\"";
        propertyToken += propertyName;
        propertyToken += "\"";
        std::size_t propertyIndex = json.find(propertyToken);
        if (propertyIndex == std::string::npos) {
            return false;
        }

        std::size_t colonIndex = json.find(':', propertyIndex + propertyToken.length());
        if (colonIndex == std::string::npos) {
            throw RuntimePlayerProfileConfigurationError(
                std::string("profile.json contains an invalid assignment for '") + propertyName + "'.");
        }

        std::size_t valueStartIndex = colonIndex + 1;
        while (valueStartIndex < json.length() && std::isspace(static_cast<unsigned char>(json[valueStartIndex])) != 0) {
            valueStartIndex++;
        }

        if (valueStartIndex >= json.length()) {
            throw RuntimePlayerProfileConfigurationError(
                std::string("profile.json ended before a value was found for '") + propertyName + "'.");
        }

        std::size_t valueEndIndex = valueStartIndex;
        if (json[valueEndIndex] == '-') {
            valueEndIndex++;
        }

        while (valueEndIndex < json.length() && std::isdigit(static_cast<unsigned char>(json[valueEndIndex])) != 0) {
            valueEndIndex++;
        }

        if (valueEndIndex == valueStartIndex || (json[valueStartIndex] == '-' && valueEndIndex == valueStartIndex + 1)) {
            throw RuntimePlayerProfileConfigurationError(
                std::string("profile.json contains a non-integer value for '") + propertyName + "'.");
        }

        std::string valueText = json.substr(valueStartIndex, valueEndIndex - valueStartIndex);
        try {
            value = std::stoi(valueText);
        } catch (const std::exception&) {
            throw RuntimePlayerProfileConfigurationError(
                std::string("profile.json contains an integer that could not be parsed for '") + propertyName + "'.");
        }

        return true;
    }

    /// Parses one optional boolean property from the JSON profile payload, if present. Returns whether the
    /// property was found; throws RuntimePlayerProfileConfigurationError when it is present with any value
    /// other than exactly `true` or `false`.
    bool RuntimePlayerProfileLoader::TryParseOptionalBoolean(const std::string& json, const char* propertyName, bool& value) const {
        if (propertyName == nullptr || propertyName[0] == '\0') {
            throw std::runtime_error("A profile property name is required.");
        }

        std::string propertyToken = "\"";
        propertyToken += propertyName;
        propertyToken += "\"";
        std::size_t propertyIndex = json.find(propertyToken);
        if (propertyIndex == std::string::npos) {
            return false;
        }

        std::size_t colonIndex = json.find(':', propertyIndex + propertyToken.length());
        if (colonIndex == std::string::npos) {
            throw RuntimePlayerProfileConfigurationError(
                std::string("profile.json contains an invalid assignment for '") + propertyName + "'.");
        }

        std::size_t valueStartIndex = colonIndex + 1;
        while (valueStartIndex < json.length() && std::isspace(static_cast<unsigned char>(json[valueStartIndex])) != 0) {
            valueStartIndex++;
        }

        const std::string trueLiteral = "true";
        const std::string falseLiteral = "false";
        std::size_t trueLiteralEndIndex = valueStartIndex + trueLiteral.length();
        bool matchesTrueLiteral = json.compare(valueStartIndex, trueLiteral.length(), trueLiteral) == 0
            && (trueLiteralEndIndex >= json.length() || std::isalnum(static_cast<unsigned char>(json[trueLiteralEndIndex])) == 0);
        if (matchesTrueLiteral) {
            value = true;
            return true;
        }

        std::size_t falseLiteralEndIndex = valueStartIndex + falseLiteral.length();
        bool matchesFalseLiteral = json.compare(valueStartIndex, falseLiteral.length(), falseLiteral) == 0
            && (falseLiteralEndIndex >= json.length() || std::isalnum(static_cast<unsigned char>(json[falseLiteralEndIndex])) == 0);
        if (matchesFalseLiteral) {
            value = false;
            return true;
        }

        throw RuntimePlayerProfileConfigurationError(
            std::string("profile.json contains a non-boolean value for '") + propertyName + "'.");
    }

    /// Validates the idle-throttle fields resolved onto the supplied profile, throwing
    /// RuntimePlayerProfileConfigurationError when IdleAfterMilliseconds is not positive or
    /// IdleFramesPerSecond falls outside the supported 1..30 range.
    void RuntimePlayerProfileLoader::ValidateIdleFields(const RuntimePlayerProfile& profile) const {
        if (profile.IdleAfterMilliseconds <= 0) {
            throw RuntimePlayerProfileConfigurationError("Runtime player profile idleAfterMilliseconds must be positive.");
        }

        if (profile.IdleFramesPerSecond < 1 || profile.IdleFramesPerSecond > 30) {
            throw RuntimePlayerProfileConfigurationError("Runtime player profile idleFramesPerSecond must be between 1 and 30.");
        }
    }

    /// Builds the persisted JSON payload for one runtime player profile.
    std::string RuntimePlayerProfileLoader::BuildProfileJson(const RuntimePlayerProfile& profile) const {
        profile.Validate();

        std::ostringstream builder;
        builder << "{\n";
        builder << "  \"resolutionWidth\": " << profile.ResolutionWidth << ",\n";
        if (profile.IdleFieldsPresent) {
            builder << "  \"resolutionHeight\": " << profile.ResolutionHeight << ",\n";
            builder << "  \"idleThrottleEnabled\": " << (profile.IdleThrottleEnabled ? "true" : "false") << ",\n";
            builder << "  \"idleAfterMilliseconds\": " << profile.IdleAfterMilliseconds << ",\n";
            builder << "  \"idleFramesPerSecond\": " << profile.IdleFramesPerSecond << "\n";
        } else {
            builder << "  \"resolutionHeight\": " << profile.ResolutionHeight << "\n";
        }
        builder << "}\n";
        return builder.str();
    }
}
