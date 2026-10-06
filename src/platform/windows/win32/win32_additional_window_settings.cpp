#include "platform/windows/win32/win32_additional_window_settings.hpp"

#include <array>
#include <charconv>
#include <stdexcept>
#include <utility>
#include "platform/windows/win32/win32_window_mode_names.hpp"

namespace helengine::windows {
    /// Stores one validated secondary-view configuration.
    Win32AdditionalWindowSettings::Win32AdditionalWindowSettings(std::string tag, Win32WindowMode mode, int left, int top, int width, int height)
        : Tag(std::move(tag)), Mode(mode), Left(left), Top(top), Width(width), Height(height) { }

    /// Parses six exact comma-separated fields without repairing invalid input.
    Win32AdditionalWindowSettings Win32AdditionalWindowSettings::Parse(const std::string& value) {
        std::array<std::string, 6> fields;
        std::size_t start = 0;
        for (std::size_t index = 0; index < fields.size(); index++) {
            std::size_t end = value.find(',', start);
            if ((index + 1 < fields.size() && end == std::string::npos)
                || (index + 1 == fields.size() && end != std::string::npos)) {
                throw std::invalid_argument("--window requires tag,normal|overlay,left,top,width,height.");
            }
            fields[index] = value.substr(start, end == std::string::npos ? end : end - start);
            start = end == std::string::npos ? value.size() : end + 1;
        }
        if (fields[0].empty() || fields[0].size() > 64 || fields[0] == "main") {
            throw std::invalid_argument("--window requires a unique tag of 1..64 characters other than main.");
        }
        for (char character : fields[0]) {
            if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9') || character == '_' || character == '-')) {
                throw std::invalid_argument("--window tags may contain only ASCII letters, digits, underscores and hyphens.");
            }
        }
        Win32WindowMode mode;
        if (!Win32WindowModeNames::TryParseWindowMode(fields[1], mode)) {
            throw std::invalid_argument("--window mode must be normal or overlay.");
        }
        return Win32AdditionalWindowSettings(fields[0], mode, ParseInteger(fields[2], false), ParseInteger(fields[3], false),
            ParseInteger(fields[4], true), ParseInteger(fields[5], true));
    }

    /// Parses a complete decimal integer; dimensions are limited to the D3D11 texture limit.
    int Win32AdditionalWindowSettings::ParseInteger(const std::string& value, bool dimension) {
        int result = 0;
        auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
        if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size()
            || (dimension && (result <= 0 || result > 16384))) {
            throw std::invalid_argument("--window coordinates must be integers and dimensions must be between 1 and 16384.");
        }
        return result;
    }
    /// Gets the stable window tag.
    const std::string& Win32AdditionalWindowSettings::GetTag() const { return Tag; }
    /// Gets the native presentation mode.
    Win32WindowMode Win32AdditionalWindowSettings::GetMode() const { return Mode; }
    /// Gets the screen-space left coordinate.
    int Win32AdditionalWindowSettings::GetLeft() const { return Left; }
    /// Gets the screen-space top coordinate.
    int Win32AdditionalWindowSettings::GetTop() const { return Top; }
    /// Gets the positive client width.
    int Win32AdditionalWindowSettings::GetWidth() const { return Width; }
    /// Gets the positive client height.
    int Win32AdditionalWindowSettings::GetHeight() const { return Height; }
}
