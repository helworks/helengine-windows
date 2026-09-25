namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="PremultipliedAlphaChecker"/> counts premultiplied-alpha violations and
/// measures the transparent and opaque pixel fractions of a capture.
/// </summary>
public sealed class PremultipliedAlphaCheckerTests {
    /// <summary>
    /// Verifies a correctly premultiplied image, where every color channel is at or below its own
    /// alpha, reports zero violating pixels.
    /// </summary>
    [Fact]
    public void Measure_correctly_premultiplied_image_reports_no_violations() {
        RegressionImage image = new(2, 1, new byte[] {
            10, 20, 30, 200,
            0, 0, 0, 0
        });

        PremultipliedAlphaMeasurement measurement = PremultipliedAlphaChecker.Measure(image);

        Assert.Equal(0, measurement.ViolatingPixels);
    }

    /// <summary>
    /// Verifies a pixel with more than one color channel above its alpha still counts as a single
    /// violating pixel, not one violation per channel.
    /// </summary>
    [Fact]
    public void Measure_counts_one_violation_per_pixel_with_a_channel_above_alpha() {
        RegressionImage image = new(2, 1, new byte[] {
            200, 200, 10, 100,
            10, 20, 30, 255
        });

        PremultipliedAlphaMeasurement measurement = PremultipliedAlphaChecker.Measure(image);

        Assert.Equal(1, measurement.ViolatingPixels);
    }

    /// <summary>
    /// Verifies a color channel exactly equal to its alpha is a valid premultiplied value, not a
    /// violation.
    /// </summary>
    [Fact]
    public void Measure_channel_equal_to_alpha_is_not_a_violation() {
        RegressionImage image = new(1, 1, new byte[] { 100, 100, 100, 100 });

        PremultipliedAlphaMeasurement measurement = PremultipliedAlphaChecker.Measure(image);

        Assert.Equal(0, measurement.ViolatingPixels);
    }

    /// <summary>
    /// Verifies the transparent and opaque fractions only count alpha exactly 0 or exactly 255,
    /// leaving a partially transparent pixel out of both.
    /// </summary>
    [Fact]
    public void Measure_computes_transparent_and_opaque_fractions() {
        RegressionImage image = new(4, 1, new byte[] {
            0, 0, 0, 0,
            0, 0, 0, 0,
            10, 10, 10, 255,
            5, 5, 5, 128
        });

        PremultipliedAlphaMeasurement measurement = PremultipliedAlphaChecker.Measure(image);

        Assert.Equal(0.5, measurement.TransparentFraction);
        Assert.Equal(0.25, measurement.OpaqueFraction);
    }
}
