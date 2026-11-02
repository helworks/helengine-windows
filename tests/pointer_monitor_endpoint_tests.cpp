#include <iostream>
#include "platform/windows/win32/win32_pointer_window_routing.hpp"

namespace helengine::windows {
    /// Checks endpoint conversion without querying or controlling the user's desktop.
    class PointerMonitorEndpointTests {
    public:
        /// Covers exposed edges, neighboring monitors, windowed views and negative desktop coordinates.
        static void Run() {
            const RECT monitor {0, 0, 1920, 1200};
            Check({500, 1200}, monitor, monitor, false, {500, 1199});
            Check({1920, 1200}, monitor, monitor, false, {1919, 1199});
            Check({1920, 400}, monitor, monitor, false, {1919, 400});
            Check({500, 1199}, monitor, monitor, true, {500, 1199});
            Check({500, 0}, monitor, monitor, true, {500, 0});
            Check({500, 1200}, monitor, monitor, true, {500, 1200});
            Check({1920, 400}, monitor, monitor, true, {1920, 400});
            Check({500, 1201}, monitor, monitor, false, {500, 1201});
            Check({-1, 1200}, monitor, monitor, false, {-1, 1200});
            Check({500, 600}, {0, 0, 800, 600}, monitor, false, {500, 600});
            const RECT negative {-1920, -1200, 0, 0};
            Check({-500, 0}, negative, negative, false, {-500, -1});
            Check({0, 0}, negative, negative, false, {-1, -1});
        }
    private:
        /// Compares both coordinates so a boundary correction cannot move along the other axis.
        static void Check(POINT point, RECT client, RECT monitor, bool onDisplay, POINT expected) {
            const POINT actual = Win32PointerWindowRouting::NormalizeMonitorEndpoint(point, client, monitor, onDisplay);
            if (actual.x != expected.x || actual.y != expected.y) {
                throw std::runtime_error("Pointer monitor endpoint mapping changed the wrong coordinate.");
            }
        }
    };
}

/// Runs pure geometry cases without creating windows or injecting input.
int main() {
    try {
        helengine::windows::PointerMonitorEndpointTests::Run();
        std::cout << "PASS monitor endpoint geometry (12 cases, no desktop input)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
