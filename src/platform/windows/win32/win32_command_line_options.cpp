#include "platform/windows/win32/win32_command_line_options.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <stdexcept>

namespace helengine::windows {
    /// Creates an options set with no flags supplied, which leaves every player behavior at its default.
    Win32CommandLineOptions::Win32CommandLineOptions()
        : SceneSupplied(false),
          SceneId(),
          FrameLimitSupplied(false),
          FrameLimit(0),
          FixedDeltaSupplied(false),
          FixedDeltaSeconds(0.0),
          CapturePathSupplied(false),
          CapturePath(),
          ArgumentsIgnored(false),
          IgnoredArguments() {
    }

    /// Parses the given argument vector, skipping arguments[0] (the executable path). When no argument is a known
    /// flag (--scene, --frames, --fixed-delta, --capture), nothing is validated: the arguments are only kept as
    /// ignored text (see GetIgnoredArguments) so launches that pass unrelated arguments, such as a file path split by
    /// CommandLineToArgvW, keep working exactly as before. Once any known flag is present, validation is strict:
    /// each flag takes exactly one value, and unknown, repeated or value-less flags and out-of-range values throw
    /// std::invalid_argument with a readable message.
    Win32CommandLineOptions Win32CommandLineOptions::Parse(int argumentCount, wchar_t** arguments) {
        if (argumentCount < 1 || arguments == nullptr) {
            throw std::invalid_argument("Command line must contain at least the executable path.");
        }

        Win32CommandLineOptions options;
        bool knownFlagPresent = false;
        for (int scanIndex = 1; scanIndex < argumentCount; scanIndex++) {
            if (IsKnownFlag(arguments[scanIndex])) {
                knownFlagPresent = true;
                break;
            }
        }

        if (!knownFlagPresent) {
            for (int ignoredIndex = 1; ignoredIndex < argumentCount; ignoredIndex++) {
                if (ignoredIndex > 1) {
                    options.IgnoredArguments += ' ';
                }
                options.IgnoredArguments += ConvertToUtf8(arguments[ignoredIndex]);
            }
            options.ArgumentsIgnored = argumentCount > 1;
            return options;
        }

        int index = 1;
        while (index < argumentCount) {
            std::wstring flag = arguments[index];
            std::string flagText = ConvertToUtf8(flag);
            if (!IsKnownFlag(flag)) {
                throw std::invalid_argument("Unknown command-line argument: " + flagText);
            }

            if (index + 1 >= argumentCount) {
                throw std::invalid_argument("Command-line flag " + flagText + " requires a value.");
            }

            std::wstring value = arguments[index + 1];
            if (flag == L"--scene") {
                if (options.SceneSupplied) {
                    throw std::invalid_argument("Command-line flag --scene was given more than once.");
                }

                if (value.empty()) {
                    throw std::invalid_argument("Command-line flag --scene requires a non-empty scene id.");
                }

                options.SceneId = ConvertToUtf8(value);
                options.SceneSupplied = true;
            } else if (flag == L"--frames") {
                if (options.FrameLimitSupplied) {
                    throw std::invalid_argument("Command-line flag --frames was given more than once.");
                }

                options.FrameLimit = ParseFrameLimit(value);
                options.FrameLimitSupplied = true;
            } else if (flag == L"--fixed-delta") {
                if (options.FixedDeltaSupplied) {
                    throw std::invalid_argument("Command-line flag --fixed-delta was given more than once.");
                }

                options.FixedDeltaSeconds = ParseFixedDeltaSeconds(value);
                options.FixedDeltaSupplied = true;
            } else {
                if (options.CapturePathSupplied) {
                    throw std::invalid_argument("Command-line flag --capture was given more than once.");
                }

                if (value.empty()) {
                    throw std::invalid_argument("Command-line flag --capture requires a non-empty file path.");
                }

                options.CapturePath = value;
                options.CapturePathSupplied = true;
            }

            index += 2;
        }

        if (options.CapturePathSupplied && !options.FrameLimitSupplied) {
            throw std::invalid_argument("Command-line flag --capture requires --frames to choose the captured frame.");
        }

        return options;
    }

    /// Reads the real process command line through CommandLineToArgvW and parses it with Parse.
    Win32CommandLineOptions Win32CommandLineOptions::ParseProcessCommandLine() {
        int argumentCount = 0;
        wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr) {
            throw std::runtime_error("CommandLineToArgvW failed to split the process command line.");
        }

        try {
            Win32CommandLineOptions options = Parse(argumentCount, arguments);
            LocalFree(arguments);
            return options;
        } catch (...) {
            LocalFree(arguments);
            throw;
        }
    }

    /// Gets whether --scene was supplied to override the packaged startup scene.
    bool Win32CommandLineOptions::HasScene() const {
        return SceneSupplied;
    }

    /// Gets the UTF-8 scene id requested through --scene; only meaningful when HasScene() is true.
    const std::string& Win32CommandLineOptions::GetSceneId() const {
        return SceneId;
    }

    /// Gets whether --frames was supplied to quit after a fixed number of presented frames.
    bool Win32CommandLineOptions::HasFrameLimit() const {
        return FrameLimitSupplied;
    }

    /// Gets the number of presented frames after which the player quits; only meaningful when HasFrameLimit() is true.
    int Win32CommandLineOptions::GetFrameLimit() const {
        return FrameLimit;
    }

    /// Gets whether --fixed-delta was supplied to advance the engine by a constant step each frame.
    bool Win32CommandLineOptions::HasFixedDelta() const {
        return FixedDeltaSupplied;
    }

    /// Gets the constant per-frame update step in seconds; only meaningful when HasFixedDelta() is true.
    double Win32CommandLineOptions::GetFixedDeltaSeconds() const {
        return FixedDeltaSeconds;
    }

    /// Gets whether --capture was supplied to request a frame capture file.
    bool Win32CommandLineOptions::HasCapturePath() const {
        return CapturePathSupplied;
    }

    /// Gets the capture file path requested through --capture; only meaningful when HasCapturePath() is true.
    const std::wstring& Win32CommandLineOptions::GetCapturePath() const {
        return CapturePath;
    }

    /// Gets whether arguments were supplied without any known flag, so they were ignored instead of validated.
    bool Win32CommandLineOptions::HasIgnoredArguments() const {
        return ArgumentsIgnored;
    }

    /// Gets the ignored arguments as UTF-8, joined by single spaces; only meaningful when HasIgnoredArguments() is true.
    const std::string& Win32CommandLineOptions::GetIgnoredArguments() const {
        return IgnoredArguments;
    }

    /// Returns whether the argument is one of the regression flags (--scene, --frames, --fixed-delta, --capture).
    bool Win32CommandLineOptions::IsKnownFlag(const std::wstring& argument) {
        return argument == L"--scene" || argument == L"--frames" || argument == L"--fixed-delta" || argument == L"--capture";
    }

    /// Converts a UTF-16 command-line value to UTF-8 so it can be compared with engine scene ids and logged.
    std::string Win32CommandLineOptions::ConvertToUtf8(const std::wstring& value) {
        if (value.empty()) {
            return std::string();
        }

        int byteCount = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (byteCount <= 0) {
            throw std::invalid_argument("Command-line value could not be converted to UTF-8.");
        }

        std::string converted(static_cast<size_t>(byteCount), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), converted.data(), byteCount, nullptr, nullptr);
        return converted;
    }

    /// Parses a --frames value that must be a whole int of at least one, throwing std::invalid_argument otherwise.
    int Win32CommandLineOptions::ParseFrameLimit(const std::wstring& value) {
        std::string invalidMessage = "Command-line flag --frames requires a whole number of at least 1, got: " + ConvertToUtf8(value);
        if (value.empty() || std::iswspace(value[0])) {
            throw std::invalid_argument(invalidMessage);
        }

        wchar_t* end = nullptr;
        errno = 0;
        long parsed = std::wcstol(value.c_str(), &end, 10);
        if (end != value.c_str() + value.size() || errno == ERANGE || parsed < 1 || parsed > INT_MAX) {
            throw std::invalid_argument(invalidMessage);
        }

        return static_cast<int>(parsed);
    }

    /// Parses a --fixed-delta value that must be a finite double greater than zero, throwing std::invalid_argument otherwise.
    double Win32CommandLineOptions::ParseFixedDeltaSeconds(const std::wstring& value) {
        std::string invalidMessage = "Command-line flag --fixed-delta requires a number of seconds greater than 0, got: " + ConvertToUtf8(value);
        if (value.empty() || std::iswspace(value[0])) {
            throw std::invalid_argument(invalidMessage);
        }

        wchar_t* end = nullptr;
        errno = 0;
        double parsed = std::wcstod(value.c_str(), &end);
        if (end != value.c_str() + value.size() || errno == ERANGE || !std::isfinite(parsed) || parsed <= 0.0) {
            throw std::invalid_argument(invalidMessage);
        }

        return parsed;
    }
}
