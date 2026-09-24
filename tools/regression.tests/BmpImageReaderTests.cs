namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="BmpImageReader"/> decodes 32-bit BI_RGB BMP files in both bottom-up and
/// top-down row order, and rejects unsupported bit depths, unsupported compression and truncated
/// pixel data.
/// </summary>
public sealed class BmpImageReaderTests {
    /// <summary>
    /// Verifies a hand-written 32-bit top-down BMP (negative height) decodes to the exact source
    /// pixels, in the same order they were written.
    /// </summary>
    [Fact]
    public void Read_top_down_bmp_returns_exact_pixels() {
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "top-down.bmp");
        byte[] pixel00 = { 10, 20, 30, 255 };
        byte[] pixel10 = { 40, 50, 60, 255 };
        byte[] pixel01 = { 70, 80, 90, 255 };
        byte[] pixel11 = { 100, 110, 120, 255 };
        byte[] storageOrder = RegressionTestFixtures.Concat(pixel00, pixel10, pixel01, pixel11);
        File.WriteAllBytes(path, RegressionTestFixtures.BuildBmp(2, -2, storageOrder));

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
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "bottom-up.bmp");
        byte[] pixel00 = { 10, 20, 30, 255 };
        byte[] pixel10 = { 40, 50, 60, 255 };
        byte[] pixel01 = { 70, 80, 90, 255 };
        byte[] pixel11 = { 100, 110, 120, 255 };
        byte[] storageOrder = RegressionTestFixtures.Concat(pixel01, pixel11, pixel00, pixel10);
        File.WriteAllBytes(path, RegressionTestFixtures.BuildBmp(2, 2, storageOrder));

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
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "24-bit.bmp");
        byte[] pixels = new byte[2 * 2 * 3];
        byte[] header = RegressionTestFixtures.BuildBmpHeader(2, 2, 24, 0, pixels.Length);
        File.WriteAllBytes(path, RegressionTestFixtures.Concat(header, pixels));

        Assert.Throws<InvalidDataException>(() => BmpImageReader.Read(path));
    }

    /// <summary>
    /// Verifies a BI_BITFIELDS (compression = 3) BMP is rejected, since the player's capture
    /// writer only ever emits BI_RGB and accepting BI_BITFIELDS would trust unvalidated channel
    /// masks.
    /// </summary>
    [Fact]
    public void Read_bi_bitfields_bmp_throws_invalid_data_exception() {
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "bitfields.bmp");
        byte[] pixels = new byte[2 * 2 * 4];
        byte[] header = RegressionTestFixtures.BuildBmpHeader(2, 2, 32, 3, pixels.Length);
        File.WriteAllBytes(path, RegressionTestFixtures.Concat(header, pixels));

        Assert.Throws<InvalidDataException>(() => BmpImageReader.Read(path));
    }

    /// <summary>
    /// Verifies a file whose declared pixel data extends past the end of the file (a truncated
    /// capture) is rejected instead of reading out of bounds.
    /// </summary>
    [Fact]
    public void Read_truncated_bmp_throws_invalid_data_exception() {
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "truncated.bmp");
        byte[] fullPixels = new byte[2 * 2 * 4];
        byte[] header = RegressionTestFixtures.BuildBmpHeader(2, 2, 32, 0, fullPixels.Length);
        byte[] truncatedPixels = fullPixels[..(fullPixels.Length - 4)];
        File.WriteAllBytes(path, RegressionTestFixtures.Concat(header, truncatedPixels));

        Assert.Throws<InvalidDataException>(() => BmpImageReader.Read(path));
    }
}
