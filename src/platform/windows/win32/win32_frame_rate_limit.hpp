#pragma once
#include <Windows.h>
#include <chrono>
#include <cwchar>
#include <stdexcept>

namespace helengine::windows {
    /// Caps shared host-frame starts without changing the swap chain's vertical synchronization.
    class Win32FrameRateLimit {
        /// Uses a monotonic clock so wall-clock/time-zone changes cannot disturb frame pacing.
        using Clock = std::chrono::steady_clock;
        /// Owns a high-resolution, nonperiodic timer; null when the cap is not configured.
        HANDLE Timer = nullptr;
        /// Holds the configured minimum interval between host frame starts.
        Clock::duration Interval{};
        /// Remembers the earliest permitted start of the next frame.
        Clock::time_point Next{};
    public:
        /// Reads an optional integer limit once; an absent setting preserves existing host behavior.
        Win32FrameRateLimit() {
            wchar_t value[32]{};
            const DWORD length = GetEnvironmentVariableW(L"HELENGINE_MAX_FPS", value, 32);
            if (length == 0) return;
            if (length >= 32) throw std::invalid_argument("HELENGINE_MAX_FPS is too long.");
            wchar_t* end = nullptr;
            const long limit = std::wcstol(value, &end, 10);
            if (end == value || *end != 0 || limit < 1 || limit > 1000)
                throw std::invalid_argument("HELENGINE_MAX_FPS must be an integer from 1 to 1000.");
            Interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / limit));
            Timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if (!Timer) throw std::runtime_error("Unable to create the host frame pacing timer.");
        }
        /// Releases the process-local timer without changing the Windows global timer resolution.
        ~Win32FrameRateLimit() { if (Timer) CloseHandle(Timer); }
        /// Disallows copying ownership of the native timer.
        Win32FrameRateLimit(const Win32FrameRateLimit&) = delete;
        /// Disallows assigning ownership of the native timer.
        Win32FrameRateLimit& operator=(const Win32FrameRateLimit&) = delete;
        /// Sleeps only the remaining budget; slow frames restart the schedule without catch-up bursts.
        void Wait() {
            if (!Timer) return;
            auto now = Clock::now();
            while (now < Next) {
                const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(Next - now).count();
                LARGE_INTEGER due{};
                due.QuadPart = -(nanoseconds + 99) / 100;
                if (!SetWaitableTimerEx(Timer, &due, 0, nullptr, nullptr, nullptr, 0))
                    throw std::runtime_error("Unable to arm the host frame pacing timer.");
                if (WaitForSingleObject(Timer, INFINITE) != WAIT_OBJECT_0)
                    throw std::runtime_error("Host frame pacing timer wait failed.");
                now = Clock::now();
            }
            if (Next == Clock::time_point{} || now - Next >= Interval) Next = now + Interval;
            else Next += Interval;
        }
    };
}
