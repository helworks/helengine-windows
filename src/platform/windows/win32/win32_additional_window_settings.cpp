#include "platform/windows/win32/win32_additional_window_settings.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <utility>
#include "platform/windows/win32/win32_window_mode_names.hpp"

namespace helengine::windows {
    /// Stores one validated secondary-view configuration.
    Win32AdditionalWindowSettings::Win32AdditionalWindowSettings(std::string tag, Win32WindowMode mode, int left, int top, int width, int height, int sampleCount)
        : Tag(std::move(tag)), Mode(mode), Left(left), Top(top), Width(width), Height(height), SampleCount(sampleCount) { }

    /// Parses six required fields and optional MSAA without repairing invalid input.
    Win32AdditionalWindowSettings Win32AdditionalWindowSettings::Parse(const std::string& value) {
        std::array<std::string, 7> fields;
        std::size_t start = 0;
        std::size_t fieldCount = 0;
        while (true) {
            if (fieldCount == fields.size()) { throw std::invalid_argument("--window accepts six fields and an optional sample count of 1 or 4."); }
            std::size_t end = value.find(',', start);
            fields[fieldCount++] = value.substr(start, end == std::string::npos ? end : end - start);
            if (end == std::string::npos) { break; }
            start = end + 1;
        }
        if (fieldCount != 6 && fieldCount != 7) { throw std::invalid_argument("--window requires tag,mode,left,top,width,height[,samples]."); }
        int sampleCount = fieldCount == 7 ? ParseInteger(fields[6], false) : 1;
        if (sampleCount != 1 && sampleCount != 4) { throw std::invalid_argument("--window sample count must be 1 or 4."); }
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
            throw std::invalid_argument("--window mode must be normal, overlay or desktop.");
        }
        int left = ParseInteger(fields[2], false);
        int top = ParseInteger(fields[3], false);
        int width = ParseInteger(fields[4], true);
        int height = ParseInteger(fields[5], true);
        if (mode == Win32WindowMode::Desktop && (static_cast<long long>(left) + width > std::numeric_limits<int>::max()
            || static_cast<long long>(top) + height > std::numeric_limits<int>::max())) {
            throw std::invalid_argument("--window desktop bounds exceed the signed screen-coordinate range.");
        }
        return Win32AdditionalWindowSettings(fields[0], mode, left, top, width, height, sampleCount);
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
    /// Gets the validated sample count for this output only.
    int Win32AdditionalWindowSettings::GetSampleCount() const { return SampleCount; }
    /// Gets the positive client height.
    int Win32AdditionalWindowSettings::GetHeight() const { return Height; }
}
