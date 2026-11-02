#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <dxgiformat.h>
#include <DirectXPackedVector.h>

namespace helengine::windows {
    /// Converts the two supported presentation encodings into stable alpha bytes or explicitly bounded SDR BMP pixels.
    class DirectX11OutputPixelConversion {
    public:
        /// Returns the physical pixel size and rejects formats whose channel layout or transfer function is unknown.
        static std::size_t GetPixelByteCount(DXGI_FORMAT format) {
            if (format == DXGI_FORMAT_B8G8R8A8_UNORM) { return 4; }
            if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) { return 8; }
            throw std::invalid_argument("Output readback requires BGRA8 SDR or RGBA16F scRGB pixels.");
        }

        /// Reads alpha independently of HDR brightness, retaining the overlay's existing rounded 0-to-255 thresholds.
        static int ReadAlpha(const void* pixel, DXGI_FORMAT format) {
            GetPixelByteCount(format);
            if (!pixel) { throw std::invalid_argument("Output alpha readback requires a pixel."); }
            if (format == DXGI_FORMAT_B8G8R8A8_UNORM) { return static_cast<const std::uint8_t*>(pixel)[3]; }
            std::uint16_t halfAlpha;
            std::memcpy(&halfAlpha, static_cast<const std::uint8_t*>(pixel) + 6, sizeof(halfAlpha));
            return Quantize(DirectX::PackedVector::XMConvertHalfToFloat(halfAlpha));
        }

        /// Copies SDR bytes unchanged; exports premultiplied linear scRGB by unpremultiplying, dividing by the current
        /// SDR-white scale (80 nits per scRGB unit), clamping SDR gamut/range, sRGB encoding, and premultiplying again.
        /// Clamping deliberately maps the OS reference white to SDR white and bounds brighter HDR highlights for BMP.
        static void ConvertRowToSdrBgra(const void* source, std::uint8_t* destination, std::size_t width, DXGI_FORMAT format, float sdrWhiteScale) {
            GetPixelByteCount(format);
            if (!source || !destination) { throw std::invalid_argument("Output conversion requires source and destination rows."); }
            if (format == DXGI_FORMAT_B8G8R8A8_UNORM) {
                std::memcpy(destination, source, width * 4);
                return;
            }
            if (!std::isfinite(sdrWhiteScale) || sdrWhiteScale <= 0) {
                throw std::invalid_argument("scRGB export requires a finite positive SDR-white scale.");
            }
            const auto* bytes = static_cast<const std::uint8_t*>(source);
            for (std::size_t index = 0; index < width; index++) {
                std::array<std::uint16_t, 4> half {};
                std::memcpy(half.data(), bytes + index * 8, 8);
                double alpha = ClampUnit(DirectX::PackedVector::XMConvertHalfToFloat(half[3]));
                destination[index * 4 + 3] = Quantize(alpha);
                for (std::size_t channel = 0; channel < 3; channel++) {
                    double linear = DirectX::PackedVector::XMConvertHalfToFloat(half[channel]);
                    double normalized = alpha > 0 ? linear / alpha / sdrWhiteScale : 0;
                    double bounded = ClampUnit(normalized);
                    double encoded = bounded >= 1 ? 1 : bounded <= 0.0031308 ? bounded * 12.92 : 1.055 * std::pow(bounded, 1.0 / 2.4) - 0.055;
                    destination[index * 4 + 2 - channel] = Quantize(encoded * alpha);
                }
            }
        }

    private:
        /// Rejects nonfinite pixel values instead of producing platform-dependent integer conversions.
        static double ClampUnit(double value) {
            if (!std::isfinite(value)) { throw std::invalid_argument("Output pixels must contain finite color and alpha values."); }
            return std::clamp(value, 0.0, 1.0);
        }

        /// Rounds normalized alpha or encoded color to the nearest byte after bounding its representable range.
        static std::uint8_t Quantize(double value) { return static_cast<std::uint8_t>(std::lround(ClampUnit(value) * 255)); }
    };
}
