#pragma once

#include <chrono>

#include "platform/windows/win32/win32_idle_frame_decision.hpp"
#include "platform/windows/win32/win32_idle_throttle_settings.hpp"

namespace helengine::windows {
    /// Decides, before each loop iteration, whether the player is active or idle and how long an idle player should
    /// wait before its next frame. It is pure: every time value is passed in, and it never calls Win32.
    class Win32IdleFramePacer {
    public:
        /// Creates a pacer from the effective idle-throttle settings.
        /// <param name="settings">Validated settings that supply the idle delay and the idle frame rate.</param>
        explicit Win32IdleFramePacer(const Win32IdleThrottleSettings& settings);

        /// Decides the mode of the next frame. The player is active when an engine keep-awake condition holds or
        /// when less than the idle delay has passed since the last activity; an active decision never waits.
        /// Otherwise the player is idle and waits until one idle frame interval after the previous frame started.
        /// <param name="nowMs">Current steady-clock time in milliseconds.</param>
        /// <param name="lastActivityMs">Steady-clock time of the last recorded activity in milliseconds.</param>
        /// <param name="lastFrameStartMs">Steady-clock time at which the previous frame started, in milliseconds.</param>
        /// <param name="keepAwake">Whether the engine requires full-rate frames right now.</param>
        /// <returns>Whether the frame is active and how long to wait before it.</returns>
        Win32IdleFrameDecision Decide(long long nowMs, long long lastActivityMs, long long lastFrameStartMs, bool keepAwake) const;

        /// Gets the time between two idle frames in milliseconds.
        int GetIdleFrameIntervalMilliseconds() const;

        /// Converts a steady-clock time point to whole milliseconds since the clock's epoch, the unit Decide uses.
        /// <param name="timePoint">Steady-clock time point to convert.</param>
        /// <returns>Milliseconds since the steady-clock epoch.</returns>
        static long long ToMilliseconds(std::chrono::steady_clock::time_point timePoint);

    private:
        /// Stores how many milliseconds without activity must pass before the player counts as idle.
        int IdleAfterMilliseconds;

        /// Stores the frame rate the player targets while idle.
        int IdleFramesPerSecond;
    };
}
