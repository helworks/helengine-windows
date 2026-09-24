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
        Directory.CreateDirectory(TestOutputDirectory);
        string path = Path.Combine(TestOutputDirectory, "round-trip.png");
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
    /// Gets the directory used for temporary PNG fixtures written by these tests.
    /// </summary>
    static string TestOutputDirectory => Path.Combine(AppContext.BaseDirectory, "test-output");
}
