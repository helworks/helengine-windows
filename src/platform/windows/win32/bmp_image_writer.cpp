#include "platform/windows/win32/bmp_image_writer.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace helengine::windows {
    /// Writes a top-down 32-bit BI_RGB bitmap (negative height) whose rows are stored first-to-last exactly as given.
    /// bgraRows must hold width * height * 4 bytes with a row stride of width * 4. Throws std::invalid_argument for
    /// invalid dimensions or buffer sizes and std::runtime_error when the file cannot be opened or written.
    void BmpImageWriter::WriteTopDownBgra32(const std::wstring& path, int width, int height, const std::vector<uint8_t>& bgraRows) {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("BMP capture dimensions must be positive.");
        }

        const uint64_t pixelByteCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
        if (pixelByteCount > 0xFFFFFFFFull - sizeof(BITMAPFILEHEADER) - sizeof(BITMAPINFOHEADER)) {
            throw std::invalid_argument("BMP capture dimensions exceed the 32-bit BMP size limit.");
        }
        if (bgraRows.size() != pixelByteCount) {
            throw std::invalid_argument("BMP capture pixel buffer size does not match width * height * 4.");
        }

        BITMAPINFOHEADER infoHeader = {};
        infoHeader.biSize = sizeof(BITMAPINFOHEADER);
        infoHeader.biWidth = width;
        infoHeader.biHeight = -height;
        infoHeader.biPlanes = 1;
        infoHeader.biBitCount = 32;
        infoHeader.biCompression = BI_RGB;
        infoHeader.biSizeImage = static_cast<DWORD>(pixelByteCount);

        BITMAPFILEHEADER fileHeader = {};
        fileHeader.bfType = 0x4D42;
        fileHeader.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));
        fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(pixelByteCount);

        const std::filesystem::path filePath(path);
        std::ofstream stream(filePath, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            throw std::runtime_error("Could not open the BMP capture file for writing: " + filePath.string());
        }

        stream.write(reinterpret_cast<const char*>(&fileHeader), sizeof(BITMAPFILEHEADER));
        stream.write(reinterpret_cast<const char*>(&infoHeader), sizeof(BITMAPINFOHEADER));
        stream.write(reinterpret_cast<const char*>(bgraRows.data()), static_cast<std::streamsize>(bgraRows.size()));
        stream.flush();
        if (!stream.good()) {
            throw std::runtime_error("Could not write the BMP capture file: " + filePath.string());
        }
    }
}
