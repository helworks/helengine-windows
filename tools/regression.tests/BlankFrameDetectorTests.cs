namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="BlankFrameDetector"/> classifies frames as blank based on the fraction of
/// pixels matching the top-left reference pixel's B, G and R channels.
/// </summary>
public sealed class BlankFrameDetectorTests {
    /// <summary>
    /// Verifies a uniform single-color image is blank.
    /// </summary>
    [Fact]
    public void IsBlank_uniform_image_is_blank() {
        RegressionImage image = CreateImage(10, 10, (x, y) => new byte[] { 5, 5, 5, 255 });

        Assert.True(BlankFrameDetector.IsBlank(image));
    }

    /// <summary>
    /// Verifies one differing pixel in a 10x10 image (99% matching) is not blank.
    /// </summary>
    [Fact]
    public void IsBlank_one_differing_pixel_in_10x10_is_not_blank() {
        RegressionImage image = CreateImage(10, 10, (x, y) =>
            x == 9 && y == 9 ? new byte[] { 200, 200, 200, 255 } : new byte[] { 5, 5, 5, 255 });

        Assert.False(BlankFrameDetector.IsBlank(image));
    }

    /// <summary>
    /// Verifies one differing pixel in a 1000x1 image (99.9% matching) is blank.
    /// </summary>
    [Fact]
    public void IsBlank_one_differing_pixel_in_1000x1_is_blank() {
        RegressionImage image = CreateImage(1000, 1, (x, y) =>
            x == 999 ? new byte[] { 200, 200, 200, 255 } : new byte[] { 5, 5, 5, 255 });

        Assert.True(BlankFrameDetector.IsBlank(image));
    }

    /// <summary>
    /// Builds a test image whose pixel colors are produced by the given per-pixel factory.
    /// </summary>
    static RegressionImage CreateImage(int width, int height, Func<int, int, byte[]> pixelAt) {
        byte[] bgra = new byte[width * height * 4];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                byte[] pixel = pixelAt(x, y);
                int offset = (y * width + x) * 4;
                pixel.CopyTo(bgra, offset);
            }
        }

        return new RegressionImage(width, height, bgra);
    }
}
