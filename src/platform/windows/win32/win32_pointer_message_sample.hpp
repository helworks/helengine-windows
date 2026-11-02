#pragma once
#include <Windows.h>
#include <cstdint>

namespace helengine::windows {
    /// Retains one window-delivered button transition and the client position where it occurred.
    struct Win32PointerMessageSample {
        /// Stores only buttons whose down message this window received.
        std::uint32_t Buttons;
        /// Preserves event coordinates when multiple transitions arrive before an engine frame.
        POINT Position;
    };
}
