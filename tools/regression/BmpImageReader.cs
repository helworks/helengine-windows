namespace helengine.windows.regression;

/// <summary>
/// Reads uncompressed 32-bit BI_RGB BMP capture files produced by the Windows player's screenshot
/// pipeline into an in-memory, top-down <see cref="RegressionImage"/>. Only BI_RGB is supported:
/// the player's capture writer never emits BI_BITFIELDS, so accepting it would silently trust an
/// unvalidated channel layout.
/// </summary>
public static class BmpImageReader {
    /// <summary>
    /// Reads a 32-bit BI_RGB BMP file, stored in either bottom-up or top-down row order, into a
    /// top-down <see cref="RegressionImage"/>.
    /// </summary>
    /// <param name="path">Path to the BMP file to read.</param>
    /// <returns>The decoded image with top-down BGRA pixel data.</returns>
    /// <exception cref="InvalidDataException">
    /// Thrown when the file is not a well-formed 32-bit BI_RGB BMP, or when the file is too short
    /// to contain the pixel data its header describes.
    /// </exception>
    public static RegressionImage Read(string path) {
        byte[] file = File.ReadAllBytes(path);
        if (file.Length < 54 || file[0] != (byte)'B' || file[1] != (byte)'M') {
            throw new InvalidDataException($"'{path}' is not a BMP file.");
        }

        uint pixelDataOffset = BitConverter.ToUInt32(file, 10);
        uint headerSize = BitConverter.ToUInt32(file, 14);
        if (headerSize < 40) {
            throw new InvalidDataException($"'{path}' has an unsupported BMP header size {headerSize}.");
        }

        int width = BitConverter.ToInt32(file, 18);
        int height = BitConverter.ToInt32(file, 22);
        ushort planes = BitConverter.ToUInt16(file, 26);
        ushort bitCount = BitConverter.ToUInt16(file, 28);
        uint compression = BitConverter.ToUInt32(file, 30);

        if (planes != 1) {
            throw new InvalidDataException($"'{path}' has an unsupported BMP plane count {planes}.");
        }

        if (bitCount != 32) {
            throw new InvalidDataException($"'{path}' is not a 32-bit BMP (bit count {bitCount}).");
        }

        if (compression != 0) {
            throw new InvalidDataException($"'{path}' has an unsupported BMP compression {compression}; only BI_RGB (0) is supported.");
        }

        bool topDown = height < 0;
        int absoluteHeight = Math.Abs(height);
        int stride = width * 4;
        long requiredLength = pixelDataOffset + (long)stride * absoluteHeight;
        if (requiredLength > file.Length) {
            throw new InvalidDataException($"'{path}' is truncated: expected at least {requiredLength} bytes of pixel data but the file is {file.Length} bytes.");
        }

        byte[] bgra = new byte[stride * absoluteHeight];

        for (int y = 0; y < absoluteHeight; y++) {
            int sourceRow = topDown ? y : absoluteHeight - 1 - y;
            int sourceOffset = (int)pixelDataOffset + sourceRow * stride;
            int destinationOffset = y * stride;
            Array.Copy(file, sourceOffset, bgra, destinationOffset, stride);
        }

        return new RegressionImage(width, absoluteHeight, bgra);
    }
}
