#pragma once
#include "platform/windows/win32/win32_scene_gi_mesh.hpp"
#include <memory>

namespace helengine::windows {
    /// <summary>Retains the last submitted immutable geometry and compact dynamic state for one drawable.</summary>
    struct Win32SceneGiContributor {
        /// <summary>Solver identity unaffected by engine pointer reuse after removal.</summary>
        std::uint64_t Identity;
        /// <summary>Monotonically increases when world transform, draw ranges or material colors change.</summary>
        std::uint64_t Revision;
        /// <summary>Shares source geometry so cached identities cannot accidentally refer to reused storage.</summary>
        std::shared_ptr<Win32SceneGiMesh> Mesh;
        /// <summary>Stores IEEE32 matrix and color values plus exact integer draw ranges in binary64 comparison storage.</summary>
        std::vector<double> Signature;
        /// <summary>Marks membership in the current authoritative registry scan.</summary>
        bool Seen;
    };
}
