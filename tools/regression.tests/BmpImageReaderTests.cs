namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="BmpImageReader"/> decodes 32-bit BI_RGB BMP files in both bottom-up and
/// top-down row order, and rejects unsupported bit depths.
/// </summary>
public sealed class BmpImageReaderTests {
    /// <summary>
    /// Verifies a hand-written 32-bit top-down BMP (negative height) decodes to the exact source
    /// pixels, in the same order they were written.
    /// </summary>
    [Fact]
    public void Read_top_down_bmp_returns_exact_pixels() {
        Directory.CreateDirectory(TestOutputDirectory);
        string path = Path.Combine(TestOutputDirectory, "top-down.bmp");
        byte[] pixel00 = { 10, 20, 30, 255 };
        byte[] pixel10 = { 40, 50, 60, 255 };
        byte[] pixel01 = { 70, 80, 90, 255 };
        byte[] pixel11 = { 100, 110, 120, 255 };
        byte[] storageOrder = Concat(pixel00, pixel10, pixel01, pixel11);
        File.WriteAllBytes(path, BuildBmp(2, -2, storageOrder));

        RegressionImage image = BmpImageReader.Read(path);

        Assert.Equal(2, image.Width);
        Assert.Equal(2, image.Height);
        Assert.Equal(pixel00, image.Bgra[0..4]);
        Assert.Equal(pixel10, image.Bgra[4..8]);
        Assert.Equal(pixel01, image.Bgra[8..12]);
        Assert.Equal(pixel11, image.Bgra[12..16]);
    }

    /// <summary>
    /// Verifies a hand-written 32-bit bottom-up BMP (positive height) decodes into top-down pixel
    /// order, with the file's rows reversed.
    /// </summary>
    [Fact]
    public void Read_bottom_up_bmp_reverses_rows_into_top_down_order() {
        Directory.CreateDirectory(TestOutputDirectory);
        string path = Path.Combine(TestOutputDirectory, "bottom-up.bmp");
        byte[] pixel00 = { 10, 20, 30, 255 };
        byte[] pixel10 = { 40, 50, 60, 255 };
        byte[] pixel01 = { 70, 80, 90, 255 };
        byte[] pixel11 = { 100, 110, 120, 255 };
        byte[] storageOrder = Concat(pixel01, pixel11, pixel00, pixel10);
        File.WriteAllBytes(path, BuildBmp(2, 2, storageOrder));

        RegressionImage image = BmpImageReader.Read(path);

        Assert.Equal(2, image.Width);
        Assert.Equal(2, image.Height);
        Assert.Equal(pixel00, image.Bgra[0..4]);
        Assert.Equal(pixel10, image.Bgra[4..8]);
        Assert.Equal(pixel01, image.Bgra[8..12]);
        Assert.Equal(pixel11, image.Bgra[12..16]);
    }

    /// <summary>
    /// Verifies a 24-bit BMP is rejected as an unsupported bit depth.
    /// </summary>
    [Fact]
    public void Read_24_bit_bmp_throws_invalid_data_exception() {
        Directory.CreateDirectory(TestOutputDirectory);
        string path = Path.Combine(TestOutputDirectory, "24-bit.bmp");
        byte[] pixels = new byte[2 * 2 * 3];
        byte[] header = BuildBmpHeader(2, 2, 24, 0, pixels.Length);
        File.WriteAllBytes(path, Concat(header, pixels));

        Assert.Throws<InvalidDataException>(() => BmpImageReader.Read(path));
    }

    /// <summary>
    /// Gets the directory used for temporary BMP fixtures written by these tests.
    /// </summary>
    static string TestOutputDirectory => Path.Combine(AppContext.BaseDirectory, "test-output");

    /// <summary>
    /// Builds a full BMP file with a 54-byte header and 32-bit BI_RGB pixel data already laid out
    /// in file storage order.
    /// </summary>
    static byte[] BuildBmp(int width, int height, byte[] pixelDataInStorageOrder) {
        byte[] header = BuildBmpHeader(width, height, 32, 0, pixelDataInStorageOrder.Length);
        return Concat(header, pixelDataInStorageOrder);
    }

    /// <summary>
    /// Builds the 14-byte BITMAPFILEHEADER and 40-byte BITMAPINFOHEADER for a BMP fixture.
    /// </summary>
    static byte[] BuildBmpHeader(int width, int height, ushort bitCount, uint compression, int pixelDataLength) {
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
    /// Concatenates byte arrays in order into a single array.
    /// </summary>
    static byte[] Concat(params byte[][] arrays) {
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
