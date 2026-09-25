#include "platform/windows/directx11/directx11_hresult_formatter.hpp"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace helengine::windows {
    /// Formats an HRESULT as "0x" followed by exactly eight upper-case hexadecimal digits (for example 0x887A000A),
    /// which is how the Windows SDK headers and documentation list the values.
    std::string DirectX11HResultFormatter::ToHex(HRESULT result) {
        std::ostringstream valueBuilder;
        valueBuilder << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << static_cast<std::uint32_t>(result);
        return valueBuilder.str();
    }
}
