#include <iostream>
#include <stdexcept>
#include "platform/windows/win32/win32_pointer_message_state.hpp"

namespace helengine::windows {
    /// Tests ordered overlay input without native windows, screenshots or injected desktop events.
    class PointerMessageStateTests {
    public:
        /// Requires fast clicks, chords, outside releases and capture cancellation to retain exact gesture authority.
        static void Run() {
            Win32PointerMessageState state; POINT point {900, 900};
            state.Observe(1, false, {2, 3});
            Require(!state.HasWork(), "An outside-origin release cannot create input.");
            state.Observe(1, true, {10, 20}); state.Observe(1, false, {11, 21});
            Require(state.HasWork() && state.Consume(point) == 1 && point.x == 10 && point.y == 20, "Fast down must preserve its event position.");
            Require(state.HasWork() && state.Consume(point) == 0 && point.x == 11 && !state.HasWork(), "Fast up must follow on the next sample.");
            state.Observe(1, true, {5, 6}); state.Observe(1, true, {8, 9});
            Require(state.Consume(point) == 1 && point.x == 5, "Repeated down must not duplicate a press.");
            state.Observe(4, true, {7, 8}); Require(state.Consume(point) == 5, "Independent buttons must form a chord.");
            state.Observe(1, false, {-10, 50}); Require(state.Consume(point) == 4 && point.x == -10, "Outside release must retain its outside position and remaining button.");
            state.Cancel(); Require(state.HasWork() && state.Consume(point) == 0 && point.x == -1 && point.y == -1, "Capture loss must cancel outside all controls.");
            Require(!state.HasWork(), "Cancellation must not retain a stuck button.");
            state.Observe(1, true, {1, 2}); state.Observe(1, false, {1, 2}); state.Cancel();
            Require(!state.HasWork(), "Cancellation must discard a click not yet delivered.");
            state.Observe(1, true, {3, 4}); Require(state.Consume(point) == 1, "Fresh click after cancellation must remain usable.");
            state.Observe(1, false, {3, 4}); Require(state.Consume(point) == 0, "Fresh gesture must release normally.");
        }
    private:
        /// Reports the violated input invariant through the test exit code.
        static void Require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
    };
}

/// Runs only deterministic message-state tests and never touches the desktop.
int main() {
    try { helengine::windows::PointerMessageStateTests::Run(); std::cout << "PASS ordered overlay pointer messages, fast clicks, chords and cancellation (no desktop input)\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
