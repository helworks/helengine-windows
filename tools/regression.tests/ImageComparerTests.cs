namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies pixel-level comparison thresholds, diff image generation and size-mismatch handling
/// for <see cref="ImageComparer"/>.
/// </summary>
public sealed class ImageComparerTests {
    /// <summary>
    /// Verifies two identical images produce zero differing pixels and pass.
    /// </summary>
    [Fact]
    public void Compare_identical_images_pass_with_zero_differing_pixels() {
        RegressionImage expected = RegressionTestFixtures.CreateUniformImage(100, 100, 10, 20, 30);
        RegressionImage actual = RegressionTestFixtures.CreateUniformImage(100, 100, 10, 20, 30);

        ImageComparison comparison = ImageComparer.Compare(expected, actual);

        Assert.True(comparison.SizesMatch);
        Assert.Equal(0, comparison.DifferingPixels);
        Assert.True(comparison.Passed);
    }

    /// <summary>
    /// Verifies a single pixel whose green channel differs by 9 in a 100x100 image stays under the
    /// 0.001 differing-fraction threshold and passes.
    /// </summary>
    [Fact]
    public void Compare_single_pixel_over_tolerance_still_passes_under_fraction_threshold() {
        RegressionImage expected = RegressionTestFixtures.CreateUniformImage(100, 100, 10, 20, 30);
        RegressionImage actual = RegressionTestFixtures.CreateUniformImage(100, 100, 10, 20, 30);
        actual.Bgra[1] = (byte)(actual.Bgra[1] + 9);

        ImageComparison comparison = ImageComparer.Compare(expected, actual);

        Assert.Equal(1, comparison.DifferingPixels);
        Assert.Equal(1.0 / 10000.0, comparison.DifferingFraction);
        Assert.True(comparison.Passed);
    }

    /// <summary>
    /// Verifies 11 differing pixels in a 100x100 image exceed the 0.001 threshold and fail.
    /// </summary>
    [Fact]
    public void Compare_eleven_differing_pixels_fails_over_fraction_threshold() {
        RegressionImage expected = RegressionTestFixtures.CreateUniformImage(100, 100, 10, 20, 30);
        RegressionImage actual = RegressionTestFixtures.CreateUniformImage(100, 100, 10, 20, 30);
        for (int i = 0; i < 11; i++) {
            actual.Bgra[i * 4] = (byte)(actual.Bgra[i * 4] + 9);
        }

        ImageComparison comparison = ImageComparer.Compare(expected, actual);

        Assert.Equal(11, comparison.DifferingPixels);
        Assert.Equal(11.0 / 10000.0, comparison.DifferingFraction);
        Assert.False(comparison.Passed);
    }

    /// <summary>
    /// Verifies a channel delta exactly at the tolerance boundary does not count as differing.
    /// </summary>
    [Fact]
    public void Compare_delta_at_tolerance_boundary_does_not_count_as_differing() {
        RegressionImage expected = RegressionTestFixtures.CreateUniformImage(10, 10, 10, 20, 30);
        RegressionImage actual = RegressionTestFixtures.CreateUniformImage(10, 10, 10, 20, 30);
        actual.Bgra[1] = (byte)(actual.Bgra[1] + ImageComparer.ChannelTolerance);

        ImageComparison comparison = ImageComparer.Compare(expected, actual);

        Assert.Equal(0, comparison.DifferingPixels);
    }

    /// <summary>
    /// Verifies alpha-only differences never count toward the differing pixel count.
    /// </summary>
    [Fact]
    public void Compare_alpha_only_difference_is_ignored() {
        RegressionImage expected = RegressionTestFixtures.CreateUniformImage(10, 10, 10, 20, 30);
        RegressionImage actual = RegressionTestFixtures.CreateUniformImage(10, 10, 10, 20, 30);
        actual.Bgra[3] = 0;

        ImageComparison comparison = ImageComparer.Compare(expected, actual);

        Assert.Equal(0, comparison.DifferingPixels);
    }

    /// <summary>
    /// Verifies a size mismatch reports mismatched sizes, a failed comparison and no diff image.
    /// </summary>
    [Fact]
    public void Compare_size_mismatch_fails_without_diff_image() {
        RegressionImage expected = RegressionTestFixtures.CreateUniformImage(10, 10, 10, 20, 30);
        RegressionImage actual = RegressionTestFixtures.CreateUniformImage(20, 10, 10, 20, 30);

        ImageComparison comparison = ImageComparer.Compare(expected, actual);

        Assert.False(comparison.SizesMatch);
        Assert.False(comparison.Passed);
        Assert.Null(comparison.DiffImage);
    }

    /// <summary>
    /// Verifies a differing pixel is rendered as opaque magenta in the diff image.
    /// </summary>
    [Fact]
    public void Compare_differing_pixel_renders_magenta_in_diff_image() {
        RegressionImage expected = RegressionTestFixtures.CreateUniformImage(1, 1, 10, 20, 30);
        RegressionImage actual = RegressionTestFixtures.CreateUniformImage(1, 1, 200, 200, 200);

        ImageComparison comparison = ImageComparer.Compare(expected, actual);

        Assert.Equal(255, comparison.DiffImage.Bgra[0]);
        Assert.Equal(0, comparison.DiffImage.Bgra[1]);
        Assert.Equal(255, comparison.DiffImage.Bgra[2]);
        Assert.Equal(255, comparison.DiffImage.Bgra[3]);
    }

    /// <summary>
    /// Verifies CompareRgba counts an alpha-only difference over tolerance as differing, unlike
    /// Compare which only looks at B, G and R.
    /// </summary>
    [Fact]
    public void CompareRgba_alpha_difference_over_tolerance_counts_as_differing() {
        RegressionImage expected = new(1, 1, new byte[] { 10, 20, 30, 255 });
        RegressionImage actual = new(1, 1, new byte[] { 10, 20, 30, 200 });

        ImageComparison comparison = ImageComparer.CompareRgba(expected, actual);

        Assert.Equal(1, comparison.DifferingPixels);
        Assert.False(comparison.Passed);
    }

    /// <summary>
    /// Verifies CompareRgba does not count an alpha difference within the channel tolerance.
    /// </summary>
    [Fact]
    public void CompareRgba_alpha_difference_within_tolerance_does_not_count() {
        RegressionImage expected = new(1, 1, new byte[] { 10, 20, 30, 255 });
        RegressionImage actual = new(1, 1, new byte[] { 10, 20, 30, (byte)(255 - ImageComparer.ChannelTolerance) });

        ImageComparison comparison = ImageComparer.CompareRgba(expected, actual);

        Assert.Equal(0, comparison.DifferingPixels);
    }

    /// <summary>
    /// Verifies CompareRgba still applies the same tolerance and fraction rules to color channels as
    /// Compare does.
    /// </summary>
    [Fact]
    public void CompareRgba_matches_rgb_only_behavior_for_color_differences() {
        RegressionImage expected = RegressionTestFixtures.CreateUniformImage(10, 10, 10, 20, 30);
        RegressionImage actual = RegressionTestFixtures.CreateUniformImage(10, 10, 10, 20, 30);
        actual.Bgra[1] = (byte)(actual.Bgra[1] + 9);

        ImageComparison comparison = ImageComparer.CompareRgba(expected, actual);

        Assert.Equal(1, comparison.DifferingPixels);
    }

    /// <summary>
    /// Verifies a size mismatch under CompareRgba fails without a diff image, exactly like Compare.
    /// </summary>
    [Fact]
    public void CompareRgba_size_mismatch_fails_without_diff_image() {
        RegressionImage expected = RegressionTestFixtures.CreateUniformImage(10, 10, 10, 20, 30);
        RegressionImage actual = RegressionTestFixtures.CreateUniformImage(20, 10, 10, 20, 30);

        ImageComparison comparison = ImageComparer.CompareRgba(expected, actual);

        Assert.False(comparison.SizesMatch);
        Assert.False(comparison.Passed);
        Assert.Null(comparison.DiffImage);
    }

    /// <summary>
    /// Verifies CompareRgba's diff image marks a differing pixel opaque magenta and a matching pixel
    /// opaque greyscale, even though the matching pixel's own alpha was fully transparent.
    /// </summary>
    [Fact]
    public void CompareRgba_diff_image_marks_differing_pixels_magenta_and_others_greyscale_opaque() {
        RegressionImage expected = new(2, 1, new byte[] { 10, 20, 30, 255, 10, 20, 30, 255 });
        RegressionImage actual = new(2, 1, new byte[] { 10, 20, 30, 0, 10, 20, 30, 255 });

        ImageComparison comparison = ImageComparer.CompareRgba(expected, actual);

        Assert.Equal(255, comparison.DiffImage.Bgra[0]);
        Assert.Equal(0, comparison.DiffImage.Bgra[1]);
        Assert.Equal(255, comparison.DiffImage.Bgra[2]);
        Assert.Equal(255, comparison.DiffImage.Bgra[3]);
        Assert.Equal(255, comparison.DiffImage.Bgra[7]);
    }
}
