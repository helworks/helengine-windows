namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="RegressionCommandRunner"/> dispatches CLI commands, prints the documented
/// result lines and returns the documented exit codes.
/// </summary>
public sealed class RegressionCommandRunnerTests {
    /// <summary>
    /// Verifies an unrecognized command prints USAGE and returns exit code 2.
    /// </summary>
    [Fact]
    public void Run_unknown_command_prints_usage_and_returns_2() {
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "bogus" }, output);

        Assert.Equal(2, exitCode);
        Assert.StartsWith("USAGE", output.ToString());
    }

    /// <summary>
    /// Verifies compare-failing writes one NEW line per newly failing test and returns exit code 1.
    /// </summary>
    [Fact]
    public void Run_compare_failing_writes_new_lines_and_returns_1() {
        Directory.CreateDirectory(TestOutputDirectory);
        string baselinePath = Path.Combine(TestOutputDirectory, "baseline.txt");
        string currentPath = Path.Combine(TestOutputDirectory, "current.txt");
        File.WriteAllText(baselinePath, "A\n");
        File.WriteAllText(currentPath, "A\nB\n");
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare-failing", baselinePath, currentPath }, output);

        Assert.Equal(1, exitCode);
        Assert.Contains("NEW B", output.ToString());
    }

    /// <summary>
    /// Verifies comparing two identical BMP/PNG captures via the compare command passes with exit
    /// code 0.
    /// </summary>
    [Fact]
    public void Run_compare_identical_captures_passes_and_returns_0() {
        Directory.CreateDirectory(TestOutputDirectory);
        string capturePath = Path.Combine(TestOutputDirectory, "capture.bmp");
        string goldenPath = Path.Combine(TestOutputDirectory, "golden.png");
        string diffPath = Path.Combine(TestOutputDirectory, "diff.png");
        RegressionImage image = CreateUniformImage(4, 4, 10, 20, 30);
        WriteBmp(capturePath, image);
        PngImageStore.Save(image, goldenPath);
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare", capturePath, goldenPath, diffPath }, output);

        Assert.Equal(0, exitCode);
        Assert.StartsWith("PASS", output.ToString());
    }

    /// <summary>
    /// Gets the directory used for temporary fixtures written by these tests.
    /// </summary>
    static string TestOutputDirectory => Path.Combine(AppContext.BaseDirectory, "test-output");

    /// <summary>
    /// Builds a uniform BGRA test image of the given size and color.
    /// </summary>
    static RegressionImage CreateUniformImage(int width, int height, byte b, byte g, byte r) {
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
    /// Writes a top-down 32-bit BI_RGB BMP file for the given image.
    /// </summary>
    static void WriteBmp(string path, RegressionImage image) {
        byte[] header = new byte[54];
        header[0] = (byte)'B';
        header[1] = (byte)'M';
        BitConverter.GetBytes((uint)(54 + image.Bgra.Length)).CopyTo(header, 2);
        BitConverter.GetBytes((uint)54).CopyTo(header, 10);
        BitConverter.GetBytes((uint)40).CopyTo(header, 14);
        BitConverter.GetBytes(image.Width).CopyTo(header, 18);
        BitConverter.GetBytes(-image.Height).CopyTo(header, 22);
        BitConverter.GetBytes((ushort)1).CopyTo(header, 26);
        BitConverter.GetBytes((ushort)32).CopyTo(header, 28);
        BitConverter.GetBytes((uint)0).CopyTo(header, 30);
        BitConverter.GetBytes((uint)image.Bgra.Length).CopyTo(header, 34);

        byte[] file = new byte[header.Length + image.Bgra.Length];
        header.CopyTo(file, 0);
        image.Bgra.CopyTo(file, header.Length);
        File.WriteAllBytes(path, file);
    }
}
