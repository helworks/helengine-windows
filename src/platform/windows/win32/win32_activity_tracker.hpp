#pragma once

#include <Windows.h>

#include <chrono>

namespace helengine::windows {
    /// Records when the player last saw user or window activity, so the idle frame pacer can tell an idle player
    /// from an active one. The window feeds it every message it receives; only input and window-state messages
    /// count as activity.
    class Win32ActivityTracker {
    public:
        /// Creates a tracker whose last activity is the moment of creation, so the player starts in active mode.
        Win32ActivityTracker();

        /// Records activity now when the message is a keyboard, mouse or window-state message; ignores all others.
        /// <param name="message">Win32 message identifier received by the window procedure.</param>
        void ObserveMessage(UINT message);

        /// Records activity now regardless of any message.
        void MarkActivity();

        /// Gets the time of the most recent recorded activity.
        std::chrono::steady_clock::time_point GetLastActivity() const;

    private:
        /// Stores the time of the most recent recorded activity.
        std::chrono::steady_clock::time_point LastActivity;
    };
}
