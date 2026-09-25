#include "platform/windows/win32/win32_idle_frame_pacer.hpp"

#include <algorithm>

namespace helengine::windows {
    /// Creates a pacer from the effective idle-throttle settings.
    /// <param name="settings">Validated settings that supply the idle delay and the idle frame rate.</param>
    Win32IdleFramePacer::Win32IdleFramePacer(const Win32IdleThrottleSettings& settings)
        : IdleAfterMilliseconds(settings.GetIdleAfterMilliseconds())
        , IdleFramesPerSecond(settings.GetIdleFramesPerSecond()) {
    }

    /// Decides the mode of the next frame. The player is active when an engine keep-awake condition holds or
    /// when less than the idle delay has passed since the last activity; an active decision never waits.
    /// Otherwise the player is idle and waits until one idle frame interval after the previous frame started.
    /// <param name="nowMs">Current steady-clock time in milliseconds.</param>
    /// <param name="lastActivityMs">Steady-clock time of the last recorded activity in milliseconds.</param>
    /// <param name="lastFrameStartMs">Steady-clock time at which the previous frame started, in milliseconds.</param>
    /// <param name="keepAwake">Whether the engine requires full-rate frames right now.</param>
    /// <returns>Whether the frame is active and how long to wait before it.</returns>
    Win32IdleFrameDecision Win32IdleFramePacer::Decide(long long nowMs, long long lastActivityMs, long long lastFrameStartMs, bool keepAwake) const {
        Win32IdleFrameDecision decision {};
        if (keepAwake || nowMs - lastActivityMs < IdleAfterMilliseconds) {
            decision.Active = true;
            decision.WaitMilliseconds = 0;
            return decision;
        }

        long long remainingMilliseconds = lastFrameStartMs + GetIdleFrameIntervalMilliseconds() - nowMs;
        decision.Active = false;
        decision.WaitMilliseconds = static_cast<int>(std::max<long long>(0, remainingMilliseconds));
        return decision;
    }

    /// Gets the time between two idle frames in milliseconds.
    int Win32IdleFramePacer::GetIdleFrameIntervalMilliseconds() const {
        return 1000 / IdleFramesPerSecond;
    }

    /// Converts a steady-clock time point to whole milliseconds since the clock's epoch, the unit Decide uses.
    /// <param name="timePoint">Steady-clock time point to convert.</param>
    /// <returns>Milliseconds since the steady-clock epoch.</returns>
    long long Win32IdleFramePacer::ToMilliseconds(std::chrono::steady_clock::time_point timePoint) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count();
    }
}
