#pragma once
#include <string>
namespace helengine::windows {
    /// Owns a preprocessor definition until its background HLSL invocation finishes.
    struct DirectX11ShaderMacro {
        /// Names the HLSL preprocessor symbol.
        std::string Name;
        /// Contains the exact substitution text, including an explicitly empty definition.
        std::string Value;
    };
}
