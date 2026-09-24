#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace helengine::windows {
    /// Writes uncompressed 32-bit BMP files from tightly packed BGRA pixel rows; used by the opt-in --capture path to
    /// persist the final rendered frame without any external imaging library.
    class BmpImageWriter {
    public:
        /// Writes a top-down 32-bit BI_RGB bitmap (negative height) whose rows are stored first-to-last exactly as given.
        /// bgraRows must hold width * height * 4 bytes with a row stride of width * 4. Throws std::invalid_argument for
        /// invalid dimensions or buffer sizes and std::runtime_error when the file cannot be opened or written.
        static void WriteTopDownBgra32(const std::wstring& path, int width, int height, const std::vector<uint8_t>& bgraRows);
    };
}
