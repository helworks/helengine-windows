#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
namespace helengine::windows {
    /// <summary>Defines the explicit room reflection volume and artistic palette for the realtime ink renderer.</summary>
    struct Win32SceneInkConfiguration {
        /// <summary>Places the raster cubemap capture strictly inside its authored room bounds.</summary>
        std::array<float,3> Position{};
        /// <summary>Defines the inclusive lower receiver bounds for parallax correction.</summary>
        std::array<float,3> BoundsMinimum{};
        /// <summary>Defines the inclusive upper receiver bounds for parallax correction.</summary>
        std::array<float,3> BoundsMaximum{};
        /// <summary>Sets linear artistic fill independently of baked or traced irradiance.</summary>
        std::array<float,3> Ambient{};
        /// <summary>Tints surfaces below the first lit band and direct shadow receivers.</summary>
        std::array<float,3> ShadowColor{};
        /// <summary>Clears uncovered cubemap pixels to the current artistic sky color.</summary>
        std::array<float,3> Sky{};
        /// <summary>Allows at most one face per interval, keeping a six-face cycle below one second at normal frame cadence.</summary>
        double FaceIntervalSeconds=.15;
        /// <summary>Rejects incomplete boxes, nonfinite palette channels and capture budgets outside the approved range.</summary>
        void Validate() const {
            if(!std::isfinite(FaceIntervalSeconds) || FaceIntervalSeconds<.1 || FaceIntervalSeconds>.16) throw std::invalid_argument("Ink face interval must be between 0.1 and 0.16 seconds.");
            for(unsigned axis=0;axis<3;++axis) {
                if(!std::isfinite(Position[axis]) || !std::isfinite(BoundsMinimum[axis]) || !std::isfinite(BoundsMaximum[axis]) || !(BoundsMinimum[axis]<Position[axis] && Position[axis]<BoundsMaximum[axis])) throw std::invalid_argument("Ink capture must lie strictly inside finite authored bounds.");
                if(!std::isfinite(Ambient[axis]) || !std::isfinite(ShadowColor[axis]) || !std::isfinite(Sky[axis]) || Ambient[axis]<0 || ShadowColor[axis]<0 || Sky[axis]<0) throw std::invalid_argument("Ink palette channels must be finite and nonnegative.");
            }
        }
    };
}
