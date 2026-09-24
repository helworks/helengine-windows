namespace helengine.windows.regression.tests;

/// <summary>
/// Shared fixtures for the regression tool's test suite: a temporary output directory, uniform
/// test images, and hand-built BMP byte layouts. Centralized here so individual test classes do
/// not each duplicate this setup.
/// </summary>
static class RegressionTestFixtures {
    /// <summary>
    /// Gets the directory used for temporary fixture files written by the test suite, creating it
    /// if it does not already exist. Tests write here (under the build output) rather than %TEMP%.
    /// </summary>
    public static string TestOutputDirectory {
        get {
            string path = Path.Combine(AppContext.BaseDirectory, "test-output");
            Directory.CreateDirectory(path);
            return path;
        }
    }

    /// <summary>
    /// Builds a uniform BGRA test image of the given size and BGR color, with full opacity.
    /// </summary>
    public static RegressionImage CreateUniformImage(int width, int height, byte b, byte g, byte r) {
        byte[] bgra = new byte[width * height * 4];
        for (int offset = 0; offset < bgra.Length; offset += 4) {
            bgra[offset] = b;
            bgra[offset + 1] = g;
            bgra[offset + 2] = r;
            bgra[offset + 3] = 255;
        }

        return new RegressionImage(width, height, bgra);
    }

    /// <summary>
    /// Builds the 14-byte BITMAPFILEHEADER and 40-byte BITMAPINFOHEADER for a BMP fixture.
    /// </summary>
    /// <param name="width">Image width to encode at header offset 18.</param>
    /// <param name="height">Signed height to encode at header offset 22 (negative = top-down).</param>
    /// <param name="bitCount">Bits per pixel to encode at header offset 28.</param>
    /// <param name="compression">Compression method to encode at header offset 30.</param>
    /// <param name="pixelDataLength">Length of the pixel data that follows the header.</param>
    public static byte[] BuildBmpHeader(int width, int height, ushort bitCount, uint compression, int pixelDataLength) {
        byte[] header = new byte[54];
        header[0] = (byte)'B';
        header[1] = (byte)'M';
        BitConverter.GetBytes((uint)(54 + pixelDataLength)).CopyTo(header, 2);
        BitConverter.GetBytes((uint)54).CopyTo(header, 10);
        BitConverter.GetBytes((uint)40).CopyTo(header, 14);
        BitConverter.GetBytes(width).CopyTo(header, 18);
        BitConverter.GetBytes(height).CopyTo(header, 22);
        BitConverter.GetBytes((ushort)1).CopyTo(header, 26);
        BitConverter.GetBytes(bitCount).CopyTo(header, 28);
        BitConverter.GetBytes(compression).CopyTo(header, 30);
        BitConverter.GetBytes((uint)pixelDataLength).CopyTo(header, 34);
        return header;
    }

    /// <summary>
    /// Builds a full 32-bit BI_RGB BMP file (54-byte header plus pixel data) with the pixel data
    /// already laid out in file storage order (negative height for top-down, positive for
    /// bottom-up).
    /// </summary>
    public static byte[] BuildBmp(int width, int height, byte[] pixelDataInStorageOrder) {
        byte[] header = BuildBmpHeader(width, height, 32, 0, pixelDataInStorageOrder.Length);
        return Concat(header, pixelDataInStorageOrder);
    }

    /// <summary>
    /// Writes a top-down 32-bit BI_RGB BMP file to disk for the given image, whose pixel data is
    /// already stored top-down.
    /// </summary>
    public static void WriteBmp(string path, RegressionImage image) {
        File.WriteAllBytes(path, BuildBmp(image.Width, -image.Height, image.Bgra));
    }

    /// <summary>
    /// Concatenates byte arrays in order into a single array.
    /// </summary>
    public static byte[] Concat(params byte[][] arrays) {
        int length = 0;
        foreach (byte[] array in arrays) {
            length += array.Length;
        }

        byte[] result = new byte[length];
        int offset = 0;
        foreach (byte[] array in arrays) {
            array.CopyTo(result, offset);
            offset += array.Length;
        }

        return result;
    }
}
