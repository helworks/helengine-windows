#pragma once

#include <string>

namespace helengine::windows {
    /// Holds the opt-in player command-line options used by regression runs (scene override, frame limit,
    /// fixed update delta and capture path). A default-constructed instance means "no flags supplied" and keeps
    /// the player on its normal startup path.
    class Win32CommandLineOptions {
    public:
        /// Creates an options set with no flags supplied, which leaves every player behavior at its default.
        Win32CommandLineOptions();

        /// Parses the given argument vector, skipping arguments[0] (the executable path). When no argument is a known
        /// flag (--scene, --frames, --fixed-delta, --capture), nothing is validated: the arguments are only kept as
        /// ignored text (see GetIgnoredArguments) so launches that pass unrelated arguments, such as a file path split by
        /// CommandLineToArgvW, keep working exactly as before. Once any known flag is present, validation is strict:
        /// each flag takes exactly one value, and unknown, repeated or value-less flags and out-of-range values throw
        /// std::invalid_argument with a readable message.
        static Win32CommandLineOptions Parse(int argumentCount, wchar_t** arguments);

        /// Reads the real process command line through CommandLineToArgvW and parses it with Parse.
        static Win32CommandLineOptions ParseProcessCommandLine();

        /// Gets whether --scene was supplied to override the packaged startup scene.
        bool HasScene() const;

        /// Gets the UTF-8 scene id requested through --scene; only meaningful when HasScene() is true.
        const std::string& GetSceneId() const;

        /// Gets whether --frames was supplied to quit after a fixed number of presented frames.
        bool HasFrameLimit() const;

        /// Gets the number of presented frames after which the player quits; only meaningful when HasFrameLimit() is true.
        int GetFrameLimit() const;

        /// Gets whether --fixed-delta was supplied to advance the engine by a constant step each frame.
        bool HasFixedDelta() const;

        /// Gets the constant per-frame update step in seconds; only meaningful when HasFixedDelta() is true.
        double GetFixedDeltaSeconds() const;

        /// Gets whether --capture was supplied to request a frame capture file.
        bool HasCapturePath() const;

        /// Gets the capture file path requested through --capture; only meaningful when HasCapturePath() is true.
        const std::wstring& GetCapturePath() const;

        /// Gets whether arguments were supplied without any known flag, so they were ignored instead of validated.
        bool HasIgnoredArguments() const;

        /// Gets the ignored arguments as UTF-8, joined by single spaces; only meaningful when HasIgnoredArguments() is true.
        const std::string& GetIgnoredArguments() const;

    private:
        /// Returns whether the argument is one of the regression flags (--scene, --frames, --fixed-delta, --capture).
        static bool IsKnownFlag(const std::wstring& argument);

        /// Converts a UTF-16 command-line value to UTF-8 so it can be compared with engine scene ids and logged.
        static std::string ConvertToUtf8(const std::wstring& value);

        /// Parses a --frames value that must be a whole int of at least one, throwing std::invalid_argument otherwise.
        static int ParseFrameLimit(const std::wstring& value);

        /// Parses a --fixed-delta value that must be a finite double greater than zero, throwing std::invalid_argument otherwise.
        static double ParseFixedDeltaSeconds(const std::wstring& value);

        /// Stores whether --scene was supplied.
        bool SceneSupplied;

        /// Stores the UTF-8 scene id supplied through --scene.
        std::string SceneId;

        /// Stores whether --frames was supplied.
        bool FrameLimitSupplied;

        /// Stores the presented-frame count after which the player quits.
        int FrameLimit;

        /// Stores whether --fixed-delta was supplied.
        bool FixedDeltaSupplied;

        /// Stores the constant per-frame update step in seconds.
        double FixedDeltaSeconds;

        /// Stores whether --capture was supplied.
        bool CapturePathSupplied;

        /// Stores the capture file path supplied through --capture.
        std::wstring CapturePath;

        /// Stores whether arguments were supplied without any known flag and were therefore ignored.
        bool ArgumentsIgnored;

        /// Stores the ignored arguments as UTF-8, joined by single spaces, for the one startup log line.
        std::string IgnoredArguments;
    };
}
