#include "platform/windows/win32/win32_command_line_options.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <cstring>
#include <stdexcept>

#include "platform/windows/win32/win32_dpi_awareness_names.hpp"
#include "platform/windows/win32/win32_window_mode_names.hpp"

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
          IdleThrottleSupplied(false),
          IdleThrottleEnabled(false),
          IdleAfterMillisecondsSupplied(false),
          IdleAfterMilliseconds(0),
          IdleFramesPerSecondSupplied(false),
          IdleFramesPerSecond(0),
          WindowModeSupplied(false),
          WindowMode(Win32WindowMode::Normal),
          OverlayBoundsSupplied(false),
          OverlayBounds(Win32OverlayBounds::Monitor),
          OverlayBackgroundSupplied(false),
          OverlayBackground(Win32OverlayBackground::Camera),
          HitTestProbeSupplied(false),
          HitTestProbeX(0),
          HitTestProbeY(0),
          ArgumentsIgnored(false),
          IgnoredArguments(),
          DpiAwarenessSupplied(false),
          DpiAwareness(Win32DpiAwareness::Unaware) {
    }

    /// Parses the given argument vector, skipping arguments[0] (the executable path). When no argument is a known
    /// flag (--scene, --frames, --fixed-delta, --capture, --idle-throttle, --idle-after-ms, --idle-fps,
    /// --window-mode, --overlay-bounds, --overlay-background, --hit-test-probe, --dpi-awareness), nothing is
    /// validated: the arguments are only kept as ignored text (see GetIgnoredArguments) so launches that pass
    /// unrelated arguments, such as a file path split by CommandLineToArgvW, keep working exactly as before. Once
    /// any known flag is present, validation is strict: each flag takes exactly one value, and unknown, repeated or
    /// value-less flags and out-of-range values throw std::invalid_argument with a readable message.
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
            } else if (flag == L"--idle-throttle") {
                if (options.IdleThrottleSupplied) {
                    throw std::invalid_argument("Command-line flag --idle-throttle was given more than once.");
                }

                options.IdleThrottleEnabled = ParseIdleThrottleEnabled(value);
                options.IdleThrottleSupplied = true;
            } else if (flag == L"--idle-after-ms") {
                if (options.IdleAfterMillisecondsSupplied) {
                    throw std::invalid_argument("Command-line flag --idle-after-ms was given more than once.");
                }

                options.IdleAfterMilliseconds = ParseIdleAfterMilliseconds(value);
                options.IdleAfterMillisecondsSupplied = true;
            } else if (flag == L"--idle-fps") {
                if (options.IdleFramesPerSecondSupplied) {
                    throw std::invalid_argument("Command-line flag --idle-fps was given more than once.");
                }

                options.IdleFramesPerSecond = ParseIdleFramesPerSecond(value);
                options.IdleFramesPerSecondSupplied = true;
            } else if (flag == L"--window-mode") {
                if (options.WindowModeSupplied) {
                    throw std::invalid_argument("Command-line flag --window-mode was given more than once.");
                }

                options.WindowMode = ParseWindowMode(value);
                options.WindowModeSupplied = true;
            } else if (flag == L"--overlay-bounds") {
                if (options.OverlayBoundsSupplied) {
                    throw std::invalid_argument("Command-line flag --overlay-bounds was given more than once.");
                }

                options.OverlayBounds = ParseOverlayBounds(value);
                options.OverlayBoundsSupplied = true;
            } else if (flag == L"--overlay-background") {
                if (options.OverlayBackgroundSupplied) {
                    throw std::invalid_argument("Command-line flag --overlay-background was given more than once.");
                }

                options.OverlayBackground = ParseOverlayBackground(value);
                options.OverlayBackgroundSupplied = true;
            } else if (flag == L"--hit-test-probe") {
                if (options.HitTestProbeSupplied) {
                    throw std::invalid_argument("Command-line flag --hit-test-probe was given more than once.");
                }

                ParseHitTestProbe(value, options.HitTestProbeX, options.HitTestProbeY);
                options.HitTestProbeSupplied = true;
            } else if (flag == L"--window") {
                if (options.AdditionalWindows.size() >= 15) {
                    throw std::invalid_argument("The player supports at most 15 additional windows.");
                }
                Win32AdditionalWindowSettings settings = Win32AdditionalWindowSettings::Parse(ConvertToUtf8(value));
                for (const auto& existing : options.AdditionalWindows) {
                    if (::_stricmp(existing.GetTag().c_str(), settings.GetTag().c_str()) == 0) {
                        throw std::invalid_argument("Duplicate --window tag: " + settings.GetTag());
                    }
                }
                options.AdditionalWindows.push_back(settings);
            } else if (flag == L"--dpi-awareness") {
                if (options.DpiAwarenessSupplied) {
                    throw std::invalid_argument("Command-line flag --dpi-awareness was given more than once.");
                }

                options.DpiAwareness = ParseDpiAwareness(value);
                options.DpiAwarenessSupplied = true;
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

    /// Gets whether --idle-throttle was supplied to opt into throttling the frame rate while the player is idle.
    bool Win32CommandLineOptions::HasIdleThrottle() const {
        return IdleThrottleSupplied;
    }

    /// Gets whether the idle throttle was requested "on" or "off" through --idle-throttle; only meaningful when
    /// HasIdleThrottle() is true.
    bool Win32CommandLineOptions::GetIdleThrottleEnabled() const {
        return IdleThrottleEnabled;
    }

    /// Gets whether --idle-after-ms was supplied to override the idle-detection delay.
    bool Win32CommandLineOptions::HasIdleAfterMilliseconds() const {
        return IdleAfterMillisecondsSupplied;
    }

    /// Gets the number of milliseconds of inactivity after which the player is considered idle; only meaningful
    /// when HasIdleAfterMilliseconds() is true.
    int Win32CommandLineOptions::GetIdleAfterMilliseconds() const {
        return IdleAfterMilliseconds;
    }

    /// Gets whether --idle-fps was supplied to override the throttled frame rate used while idle.
    bool Win32CommandLineOptions::HasIdleFramesPerSecond() const {
        return IdleFramesPerSecondSupplied;
    }

    /// Gets the frame rate, in frames per second, applied while the player is idle; only meaningful when
    /// HasIdleFramesPerSecond() is true.
    int Win32CommandLineOptions::GetIdleFramesPerSecond() const {
        return IdleFramesPerSecond;
    }

    /// Gets whether --window-mode was supplied to override the profile's window presentation mode.
    bool Win32CommandLineOptions::HasWindowMode() const {
        return WindowModeSupplied;
    }

    /// Gets the window mode requested through --window-mode; only meaningful when HasWindowMode() is true.
    Win32WindowMode Win32CommandLineOptions::GetWindowMode() const {
        return WindowMode;
    }

    /// Gets whether --overlay-bounds was supplied to override the profile's overlay bounds source.
    bool Win32CommandLineOptions::HasOverlayBounds() const {
        return OverlayBoundsSupplied;
    }

    /// Gets the overlay bounds source requested through --overlay-bounds; only meaningful when
    /// HasOverlayBounds() is true.
    Win32OverlayBounds Win32CommandLineOptions::GetOverlayBounds() const {
        return OverlayBounds;
    }

    /// Gets whether --overlay-background was supplied to override the profile's overlay clear behavior.
    bool Win32CommandLineOptions::HasOverlayBackground() const {
        return OverlayBackgroundSupplied;
    }

    /// Gets the overlay background requested through --overlay-background; only meaningful when
    /// HasOverlayBackground() is true.
    Win32OverlayBackground Win32CommandLineOptions::GetOverlayBackground() const {
        return OverlayBackground;
    }

    /// Gets whether --hit-test-probe was supplied to request a one-pixel alpha sample instead of a normal run.
    bool Win32CommandLineOptions::HasHitTestProbe() const {
        return HitTestProbeSupplied;
    }

    /// Gets the probed pixel's client-area X coordinate; only meaningful when HasHitTestProbe() is true.
    int Win32CommandLineOptions::GetHitTestProbeX() const {
        return HitTestProbeX;
    }

    /// Gets the probed pixel's client-area Y coordinate; only meaningful when HasHitTestProbe() is true.
    int Win32CommandLineOptions::GetHitTestProbeY() const {
        return HitTestProbeY;
    }

    /// Gets whether --dpi-awareness was supplied to override the profile's DPI-awareness opt-in.
    bool Win32CommandLineOptions::HasDpiAwareness() const {
        return DpiAwarenessSupplied;
    }

    /// Gets the DPI-awareness value requested through --dpi-awareness; only meaningful when HasDpiAwareness() is true.
    Win32DpiAwareness Win32CommandLineOptions::GetDpiAwareness() const {
        return DpiAwareness;
    }

    /// Gets whether arguments were supplied without any known flag, so they were ignored instead of validated.
    bool Win32CommandLineOptions::HasIgnoredArguments() const {
        return ArgumentsIgnored;
    }

    /// Gets the ignored arguments as UTF-8, joined by single spaces; only meaningful when HasIgnoredArguments() is true.
    const std::string& Win32CommandLineOptions::GetIgnoredArguments() const {
        return IgnoredArguments;
    }

    /// Returns whether the argument is one of the regression flags (--scene, --frames, --fixed-delta, --capture,
    /// --idle-throttle, --idle-after-ms, --idle-fps, --window-mode, --overlay-bounds, --overlay-background,
    /// --hit-test-probe, --dpi-awareness).
    bool Win32CommandLineOptions::IsKnownFlag(const std::wstring& argument) {
        return argument == L"--scene" || argument == L"--frames" || argument == L"--fixed-delta" || argument == L"--capture"
            || argument == L"--idle-throttle" || argument == L"--idle-after-ms" || argument == L"--idle-fps"
            || argument == L"--window-mode" || argument == L"--overlay-bounds" || argument == L"--overlay-background"
            || argument == L"--hit-test-probe" || argument == L"--dpi-awareness" || argument == L"--window";
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

    /// Parses a whole-number command-line value, rejecting empty or leading-whitespace text, partial parses and
    /// overflow, and enforcing it falls within [minimumValue, maximumValue] inclusive. On any failure throws
    /// std::invalid_argument whose message is invalidValueMessagePrefix followed by the offending value converted
    /// to UTF-8, so every bounded-integer flag (--frames, --idle-after-ms, --idle-fps) reports failures the same
    /// way while keeping its own wording and bounds.
    int Win32CommandLineOptions::ParseBoundedInteger(const std::wstring& value, int minimumValue, int maximumValue, const std::string& invalidValueMessagePrefix) {
        std::string invalidMessage = invalidValueMessagePrefix + ConvertToUtf8(value);
        if (value.empty() || std::iswspace(value[0])) {
            throw std::invalid_argument(invalidMessage);
        }

        wchar_t* end = nullptr;
        errno = 0;
        long parsed = std::wcstol(value.c_str(), &end, 10);
        if (end != value.c_str() + value.size() || errno == ERANGE || parsed < minimumValue || parsed > maximumValue) {
            throw std::invalid_argument(invalidMessage);
        }

        return static_cast<int>(parsed);
    }

    /// Parses a --frames value that must be a whole int of at least one, throwing std::invalid_argument otherwise.
    int Win32CommandLineOptions::ParseFrameLimit(const std::wstring& value) {
        return ParseBoundedInteger(value, 1, INT_MAX, "Command-line flag --frames requires a whole number of at least 1, got: ");
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

    /// Parses an --idle-throttle value that must be exactly "on" or "off", throwing std::invalid_argument otherwise.
    bool Win32CommandLineOptions::ParseIdleThrottleEnabled(const std::wstring& value) {
        if (value == L"on") {
            return true;
        }

        if (value == L"off") {
            return false;
        }

        throw std::invalid_argument("Command-line flag --idle-throttle requires \"on\" or \"off\", got: " + ConvertToUtf8(value));
    }

    /// Parses an --idle-after-ms value that must be a whole number of at least one, throwing std::invalid_argument otherwise.
    int Win32CommandLineOptions::ParseIdleAfterMilliseconds(const std::wstring& value) {
        return ParseBoundedInteger(value, 1, INT_MAX, "Command-line flag --idle-after-ms requires a whole number of at least 1, got: ");
    }

    /// Parses an --idle-fps value that must be a whole number from 1 to 30, throwing std::invalid_argument otherwise.
    int Win32CommandLineOptions::ParseIdleFramesPerSecond(const std::wstring& value) {
        return ParseBoundedInteger(value, 1, 30, "Command-line flag --idle-fps requires a whole number from 1 to 30, got: ");
    }

    /// Parses a --window-mode value that must be exactly "normal" or "overlay", throwing std::invalid_argument otherwise.
    Win32WindowMode Win32CommandLineOptions::ParseWindowMode(const std::wstring& value) {
        std::string text = ConvertToUtf8(value);
        Win32WindowMode windowMode = Win32WindowMode::Normal;
        if (Win32WindowModeNames::TryParseWindowMode(text, windowMode)) {
            return windowMode;
        }

        throw std::invalid_argument("Command-line flag --window-mode requires \"normal\" or \"overlay\", got: " + text);
    }

    /// Parses an --overlay-bounds value that must be exactly "monitor" or "profile", throwing std::invalid_argument otherwise.
    Win32OverlayBounds Win32CommandLineOptions::ParseOverlayBounds(const std::wstring& value) {
        std::string text = ConvertToUtf8(value);
        Win32OverlayBounds overlayBounds = Win32OverlayBounds::Monitor;
        if (Win32WindowModeNames::TryParseOverlayBounds(text, overlayBounds)) {
            return overlayBounds;
        }

        throw std::invalid_argument("Command-line flag --overlay-bounds requires \"monitor\" or \"profile\", got: " + text);
    }

    /// Parses an --overlay-background value that must be exactly "camera" or "transparent", throwing std::invalid_argument otherwise.
    Win32OverlayBackground Win32CommandLineOptions::ParseOverlayBackground(const std::wstring& value) {
        std::string text = ConvertToUtf8(value);
        Win32OverlayBackground overlayBackground = Win32OverlayBackground::Camera;
        if (Win32WindowModeNames::TryParseOverlayBackground(text, overlayBackground)) {
            return overlayBackground;
        }

        throw std::invalid_argument("Command-line flag --overlay-background requires \"camera\" or \"transparent\", got: " + text);
    }

    /// Parses a --hit-test-probe value that must be two base-10 non-negative integers separated by exactly one comma,
    /// with both numbers fully consumed, throwing std::invalid_argument otherwise. Writes the parsed coordinates into
    /// probeX and probeY. Each coordinate must start with a digit: a leading '+' or '-' is rejected, so the value can
    /// only ever be non-negative.
    void Win32CommandLineOptions::ParseHitTestProbe(const std::wstring& value, int& probeX, int& probeY) {
        std::string invalidMessage = "Command-line flag --hit-test-probe requires two non-negative whole numbers separated by a comma, got: " + ConvertToUtf8(value);

        std::size_t commaIndex = value.find(L',');
        if (commaIndex == std::wstring::npos || value.find(L',', commaIndex + 1) != std::wstring::npos) {
            throw std::invalid_argument(invalidMessage);
        }

        std::wstring xText = value.substr(0, commaIndex);
        std::wstring yText = value.substr(commaIndex + 1);
        if (xText.empty() || yText.empty() || std::iswdigit(xText[0]) == 0 || std::iswdigit(yText[0]) == 0) {
            throw std::invalid_argument(invalidMessage);
        }

        wchar_t* xEnd = nullptr;
        errno = 0;
        long parsedX = std::wcstol(xText.c_str(), &xEnd, 10);
        if (xEnd != xText.c_str() + xText.size() || errno == ERANGE || parsedX < 0) {
            throw std::invalid_argument(invalidMessage);
        }

        wchar_t* yEnd = nullptr;
        errno = 0;
        long parsedY = std::wcstol(yText.c_str(), &yEnd, 10);
        if (yEnd != yText.c_str() + yText.size() || errno == ERANGE || parsedY < 0) {
            throw std::invalid_argument(invalidMessage);
        }

        probeX = static_cast<int>(parsedX);
        probeY = static_cast<int>(parsedY);
    }

    /// Parses a --dpi-awareness value that must be exactly "unaware" or "permonitorv2", throwing
    /// std::invalid_argument otherwise.
    Win32DpiAwareness Win32CommandLineOptions::ParseDpiAwareness(const std::wstring& value) {
        std::string text = ConvertToUtf8(value);
        Win32DpiAwareness dpiAwareness = Win32DpiAwareness::Unaware;
        if (Win32DpiAwarenessNames::TryParse(text, dpiAwareness)) {
            return dpiAwareness;
        }

        throw std::invalid_argument("Command-line flag --dpi-awareness requires \"unaware\" or \"permonitorv2\", got: " + text);
    }
    /// Returns the validated secondary-view configurations without copying them.
    const std::vector<Win32AdditionalWindowSettings>& Win32CommandLineOptions::GetAdditionalWindows() const {
        return AdditionalWindows;
    }

}
