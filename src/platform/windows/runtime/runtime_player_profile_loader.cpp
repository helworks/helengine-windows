#include "platform/windows/runtime/runtime_player_profile_loader.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

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

        try {
            RuntimePlayerProfile profile = ReadProfile(profilePath);
            profile.Validate();
            lifecycleMessage = "profile.json loaded successfully.";
            return profile;
        } catch (const std::exception&) {
            WriteProfile(profilePath, defaultProfile);
            lifecycleMessage = "profile.json was invalid and was recreated from deployment defaults.";
            return defaultProfile;
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

    /// Reads one persisted runtime profile from the supplied profile path.
    RuntimePlayerProfile RuntimePlayerProfileLoader::ReadProfile(const std::filesystem::path& profilePath) const {
        std::ifstream stream(profilePath, std::ios::in | std::ios::binary);
        if (!stream.is_open()) {
            throw std::runtime_error("profile.json could not be opened for reading.");
        }

        std::ostringstream builder;
        builder << stream.rdbuf();
        if (stream.bad()) {
            throw std::runtime_error("profile.json could not be read.");
        }

        return ParseProfileJson(builder.str());
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

    /// Builds the persisted JSON payload for one runtime player profile.
    std::string RuntimePlayerProfileLoader::BuildProfileJson(const RuntimePlayerProfile& profile) const {
        profile.Validate();

        std::ostringstream builder;
        builder << "{\n";
        builder << "  \"resolutionWidth\": " << profile.ResolutionWidth << ",\n";
        builder << "  \"resolutionHeight\": " << profile.ResolutionHeight << "\n";
        builder << "}\n";
        return builder.str();
    }
}
