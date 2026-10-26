#pragma once
#include "directx11_shader_macro.hpp"
#include <d3dcompiler.h>
#include <vector>
namespace helengine::windows {
    /// Describes an immutable, independently compilable backend shader without borrowing renderer data.
    struct DirectX11ShaderRequest {
        /// Owns the complete HLSL translation unit; runtime includes must already be resolved.
        std::string Source;
        /// Identifies the shader in compiler diagnostics.
        std::string SourceName;
        /// Selects the requested HLSL entry function.
        std::string Entry;
        /// Selects the exact shader model and stage supported by the consuming pipeline.
        std::string Profile;
        /// Preserves the caller's compiler optimization and validation flags.
        UINT Flags;
        /// Owns every preprocessor name and value, preserving independent shader variants.
        std::vector<DirectX11ShaderMacro> Macros;
    };
}
