namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="PngImageStore"/> round-trips pixel data through a PNG file without altering
/// the BGRA byte order or values used by <see cref="BmpImageReader"/>. This class is not in the
/// task brief's file list; it exists to satisfy the explicit round-trip ruling for PNG golden
/// loads (see task-1-report.md deviations).
/// </summary>
public sealed class PngImageStoreTests {
    /// <summary>
    /// Verifies saving a regression image to PNG and loading it back produces identical BGRA
    /// bytes, with full opacity preserved.
    /// </summary>
    [Fact]
    public void Save_then_load_round_trips_identical_bgra_bytes() {
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "round-trip.png");
        byte[] bgra = {
            10, 20, 30, 255,
            200, 150, 100, 255,
            0, 0, 0, 255,
            255, 255, 255, 255
        };
        RegressionImage original = new(2, 2, bgra);

        PngImageStore.Save(original, path);
        RegressionImage loaded = PngImageStore.Load(path);

        Assert.Equal(original.Width, loaded.Width);
        Assert.Equal(original.Height, loaded.Height);
        Assert.Equal(original.Bgra, loaded.Bgra);
    }

    /// <summary>
    /// Verifies SaveWithAlpha then LoadWithAlpha round-trips every byte exactly, including pixels
    /// whose alpha is 0 or near 0 with a non-zero color: Load's Graphics.DrawImageUnscaled would
    /// composite those pixels against black and lose their color, so the alpha-preserving pair must
    /// copy raw ARGB bytes instead.
    /// </summary>
    [Fact]
    public void SaveWithAlpha_then_LoadWithAlpha_round_trips_alpha_zero_pixels_with_nonzero_color() {
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "alpha-round-trip.png");
        byte[] bgra = {
            200, 180, 160, 0,
            10, 20, 30, 255,
            90, 60, 30, 128,
            1, 2, 3, 1
        };
        RegressionImage original = new(2, 2, bgra);

        PngImageStore.SaveWithAlpha(original, path);
        RegressionImage loaded = PngImageStore.LoadWithAlpha(path);

        Assert.Equal(original.Width, loaded.Width);
        Assert.Equal(original.Height, loaded.Height);
        Assert.Equal(original.Bgra, loaded.Bgra);
    }
}
