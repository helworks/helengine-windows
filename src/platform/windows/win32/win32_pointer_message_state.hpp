#pragma once
#include <deque>
#include <stdexcept>
#include "platform/windows/win32/win32_pointer_message_sample.hpp"

namespace helengine::windows {
    /// Converts window-local mouse messages into ordered engine samples without querying a foreign foreground thread.
    class Win32PointerMessageState {
    public:
        /// Records a genuine transition; an up without a matching owned down never creates a gesture.
        void Observe(std::uint32_t button, bool down, POINT position) {
            if (button == 0 || (button & (button - 1)) != 0 || button > 16) {
                throw std::invalid_argument("Pointer messages require one of the five supported button bits.");
            }
            const std::uint32_t next = down ? Buttons | button : Buttons & ~button;
            if (next == Buttons) { return; }
            if (Pending.size() >= 256) { throw std::runtime_error("Window pointer transition queue exceeded its bound."); }
            Buttons = next;
            Pending.push_back({Buttons, position});
        }

        /// Emits at most one transition per frame, preserving even a complete click received between two frames.
        std::uint32_t Consume(POINT& position) {
            if (!Pending.empty()) {
                const Win32PointerMessageSample sample = Pending.front(); Pending.pop_front();
                position = sample.Position; DeliveredButtons = sample.Buttons;
            } else { DeliveredButtons = Buttons; }
            return DeliveredButtons;
        }

        /// Cancels pending gestures and releases already-delivered buttons outside all client controls, preventing activation.
        void Cancel() {
            Buttons = 0; Pending.clear();
            if (DeliveredButtons != 0) { Pending.push_back({0, {-1, -1}}); }
        }

        /// Keeps the originating pointer view selected until every transition and release has reached the engine.
        bool HasWork() const { return Buttons != 0 || DeliveredButtons != 0 || !Pending.empty(); }

        /// Reports current window-message button ownership for capture acquisition and release.
        std::uint32_t GetButtons() const { return Buttons; }

    private:
        /// Tracks buttons whose down/up messages have reached this window.
        std::uint32_t Buttons = 0;
        /// Tracks the last sample delivered to engine UI so cancellation can safely release it.
        std::uint32_t DeliveredButtons = 0;
        /// Retains bounded ordered transitions rather than merging a fast press and release into idle.
        std::deque<Win32PointerMessageSample> Pending;
    };
}
