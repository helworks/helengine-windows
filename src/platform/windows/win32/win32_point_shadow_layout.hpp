#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
namespace helengine::windows {
    /// <summary>Partitions four local-light tiles into six square faces without reducing the two directional cascades.</summary>
    struct Win32PointShadowLayout {
        /// <summary>Returns a deterministic dominant-axis face, including edge ties shared with the receiver shader.</summary>
        static unsigned Face(double x,double y,double z) {
            const double ax=std::abs(x),ay=std::abs(y),az=std::abs(z);
            if(ax>=ay && ax>=az) return x>=0?0:1;
            if(ay>=az) return y>=0?2:3;
            return z>=0?4:5;
        }
        /// <summary>Returns the outward axis of each positive/negative X, Y and Z face.</summary>
        static std::array<float,3> Direction(unsigned face) {
            if(face>=6) throw std::out_of_range("Point shadow face");
            std::array<float,3> result{};result[face/2]=(face%2)==0?1.0f:-1.0f;return result;
        }
        /// <summary>Uses Z as up on vertical faces to avoid a singular view transform.</summary>
        static std::array<float,3> Up(unsigned face) {
            if(face>=6) throw std::out_of_range("Point shadow face");
            return face/2==1?std::array<float,3>{0,0,1}:std::array<float,3>{0,1,0};
        }
        /// <summary>Returns the pixel origin of a 256-square face within a 512 by 768 local-light tile.</summary>
        static std::array<unsigned,2> Rectangle(unsigned tile,unsigned face) {
            if(tile>=4 || face>=6) throw std::out_of_range("Point shadow atlas tile");
            return {tile*512+(face%2)*256,1024+(face/2)*256};
        }
    };
}
