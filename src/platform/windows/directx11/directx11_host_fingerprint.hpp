#pragma once

#include <Windows.h>

#include <chrono>
#include <string>
#include <vector>

namespace helengine::windows {
    class DirectX11Bootstrap;

    /// Collects the host-layer facts of a --frames run (swap-chain description, window styles, client size, Present
    /// count and failures, wall-clock time) and formats them as the one HOST_FINGERPRINT startup-log line that the
    /// regression net records and compares. Only constructed when the player was started with --frames, so a
    /// no-argument run never creates it.
    class DirectX11HostFingerprint {
    public:
        /// Creates a fingerprint bound to the bootstrap that owns the swap chain and to the player's main window.
        DirectX11HostFingerprint(DirectX11Bootstrap& bootstrap, HWND windowHandle);

        /// Records one presented frame: stamps the wall clock (the first call starts it, every call moves its end) and
        /// counts a failed Present. Returns true only for a failing HRESULT that has not been seen before in this run,
        /// so the caller logs each distinct failure once.
        bool RecordPresent(HRESULT presentResult);

        /// Builds the HOST_FINGERPRINT line from the current swap-chain description, window styles, client rectangle
        /// and Present count, plus the recorded failures, the given frame count, whether the idle throttle is enabled
        /// (written as idleThrottle=on or off), how many of the frames ran in idle and in active mode, and the
        /// milliseconds between the first and the last recorded Present. Throws std::runtime_error when a DXGI or Win32
        /// query fails.
        std::string Describe(int frameCount, bool idleThrottleEnabled, int idleFrames, int activeFrames) const;

        /// Formats a failing Present HRESULT as a readable startup-log line with the value in hexadecimal.
        static std::string DescribePresentFailure(HRESULT presentResult);

    private:
        /// Throws std::runtime_error carrying the operation name and the failing HRESULT in hexadecimal.
        static void ThrowIfFailed(HRESULT result, const char* operation);

        /// Stores the DirectX11 bootstrap whose swap chain is described; not owned.
        DirectX11Bootstrap& Bootstrap;

        /// Stores the main window whose styles and client rectangle are described; not owned.
        HWND WindowHandle;

        /// Stores whether RecordPresent has been called at least once, which means FirstPresentTime is valid.
        bool PresentRecorded;

        /// Stores the time of the first recorded Present.
        std::chrono::steady_clock::time_point FirstPresentTime;

        /// Stores the time of the latest recorded Present.
        std::chrono::steady_clock::time_point LastPresentTime;

        /// Counts recorded Present calls that returned a failing HRESULT.
        int PresentFailureCount;

        /// Stores each distinct failing Present HRESULT already reported, so every one is logged only once.
        std::vector<HRESULT> ReportedPresentFailures;
    };
}
