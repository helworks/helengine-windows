namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="ProbeFinder"/> locates the first uniformly transparent and uniformly opaque
/// probe pixels in row-major scan order, respecting the edge margin.
/// </summary>
public sealed class ProbeFinderTests {
    /// <summary>
    /// Verifies a fully transparent image reports its first valid probe at the top-left margin
    /// position, (2,2).
    /// </summary>
    [Fact]
    public void FindTransparent_uniform_image_returns_first_margin_pixel() {
        RegressionImage image = CreateUniformAlphaImage(9, 9, 0);

        ProbeCandidate candidate = ProbeFinder.FindTransparent(image);

        Assert.NotNull(candidate);
        Assert.Equal(2, candidate.X);
        Assert.Equal(2, candidate.Y);
    }

    /// <summary>
    /// Verifies a fully opaque image reports its first valid probe at the top-left margin position,
    /// (2,2).
    /// </summary>
    [Fact]
    public void FindOpaque_uniform_image_returns_first_margin_pixel() {
        RegressionImage image = CreateUniformAlphaImage(9, 9, 255);

        ProbeCandidate candidate = ProbeFinder.FindOpaque(image);

        Assert.NotNull(candidate);
        Assert.Equal(2, candidate.X);
        Assert.Equal(2, candidate.Y);
    }

    /// <summary>
    /// Verifies FindTransparent returns null when no pixel's 5x5 neighbourhood is entirely alpha 0.
    /// </summary>
    [Fact]
    public void FindTransparent_no_uniform_transparent_region_returns_null() {
        RegressionImage image = CreateUniformAlphaImage(9, 9, 255);

        ProbeCandidate candidate = ProbeFinder.FindTransparent(image);

        Assert.Null(candidate);
    }

    /// <summary>
    /// Verifies a single non-matching pixel inside an otherwise uniform neighbourhood disqualifies
    /// that candidate, and scanning continues in row-major order to the next valid position.
    /// </summary>
    [Fact]
    public void FindTransparent_skips_a_neighbourhood_with_one_non_transparent_pixel() {
        RegressionImage image = CreateUniformAlphaImage(10, 9, 0);
        int alphaOffset = (0 * image.Width + 0) * 4 + 3;
        image.Bgra[alphaOffset] = 255;

        ProbeCandidate candidate = ProbeFinder.FindTransparent(image);

        Assert.NotNull(candidate);
        Assert.Equal(3, candidate.X);
        Assert.Equal(2, candidate.Y);
    }

    /// <summary>
    /// Verifies a probe pixel closer than the 2-pixel margin to any edge is never returned, even when
    /// its neighbourhood, truncated to the image bounds, would otherwise look uniform.
    /// </summary>
    [Fact]
    public void FindTransparent_never_returns_a_pixel_closer_than_the_margin_to_an_edge() {
        RegressionImage image = CreateUniformAlphaImage(4, 4, 0);

        ProbeCandidate candidate = ProbeFinder.FindTransparent(image);

        Assert.Null(candidate);
    }

    /// <summary>
    /// Builds a uniform-alpha image of the given size, with every pixel's alpha set to the given
    /// value and an arbitrary but valid color.
    /// </summary>
    static RegressionImage CreateUniformAlphaImage(int width, int height, byte alpha) {
        byte[] bgra = new byte[width * height * 4];
        for (int offset = 0; offset < bgra.Length; offset += 4) {
            bgra[offset] = 1;
            bgra[offset + 1] = 2;
            bgra[offset + 2] = 3;
            bgra[offset + 3] = alpha;
        }

        return new RegressionImage(width, height, bgra);
    }
}
