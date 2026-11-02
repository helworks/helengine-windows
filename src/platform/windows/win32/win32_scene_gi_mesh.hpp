#pragma once
#include "helgi/Vec3.h"
#include <cstdint>
#include <vector>

namespace helengine::windows {
    /// <summary>Retains platform-independent source geometry beside an uploaded model for optional scene transport.</summary>
    struct Win32SceneGiMesh {
        /// <summary>Model-space vertex positions copied before the upload caller releases its raw asset.</summary>
        std::vector<helgi::Vec3> Positions;
        /// <summary>Unified triangle indices, empty for nonindexed geometry.</summary>
        std::vector<std::uint32_t> Indices;
    };
}
