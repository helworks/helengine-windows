#pragma once

#include <Windows.h>

#include <string>

namespace helengine::windows {
    /// The single place that turns an HRESULT into text for DirectX error messages and log lines, so every failure the
    /// player reports shows the value the same way and the formatting is never copied into each class again.
    class DirectX11HResultFormatter {
    public:
        /// Formats an HRESULT as "0x" followed by exactly eight upper-case hexadecimal digits (for example
        /// 0x887A000A), which is how the Windows SDK headers and documentation list the values.
        /// <param name="result">The HRESULT to format; any value, successful or failing.</param>
        /// <returns>The formatted value, including the "0x" prefix.</returns>
        static std::string ToHex(HRESULT result);
    };
}
