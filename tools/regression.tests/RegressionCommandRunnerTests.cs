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
        string baselinePath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "baseline.txt");
        string currentPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "current.txt");
        File.WriteAllText(baselinePath, "A\n");
        File.WriteAllText(currentPath, "A\nB\n");
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare-failing", baselinePath, currentPath }, output);

        Assert.Equal(1, exitCode);
        Assert.Contains("NEW B", output.ToString());
    }

    /// <summary>
    /// Verifies compare-failing with a flaky list turns a listed new failure into a "WARN flaky" line and returns
    /// exit code 0.
    /// </summary>
    [Fact]
    public void Run_compare_failing_with_flaky_new_failure_warns_and_returns_0() {
        string baselinePath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "flaky-baseline.txt");
        string currentPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "flaky-current.txt");
        string flakyPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "flaky-list.txt");
        File.WriteAllText(baselinePath, "A\n");
        File.WriteAllText(currentPath, "A\nF\n");
        File.WriteAllText(flakyPath, "F\n");
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare-failing", baselinePath, currentPath, flakyPath }, output);

        Assert.Equal(0, exitCode);
        Assert.StartsWith("PASS", output.ToString());
        Assert.Contains("WARN flaky F", output.ToString());
        Assert.DoesNotContain("NEW F", output.ToString());
    }

    /// <summary>
    /// Verifies compare-failing with a flaky list still fails, with a NEW line, on a new failure that is not listed.
    /// </summary>
    [Fact]
    public void Run_compare_failing_with_non_flaky_new_failure_returns_1() {
        string baselinePath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "nonflaky-baseline.txt");
        string currentPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "nonflaky-current.txt");
        string flakyPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "nonflaky-list.txt");
        File.WriteAllText(baselinePath, "A\n");
        File.WriteAllText(currentPath, "A\nN\n");
        File.WriteAllText(flakyPath, "F\n");
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare-failing", baselinePath, currentPath, flakyPath }, output);

        Assert.Equal(1, exitCode);
        Assert.StartsWith("FAIL", output.ToString());
        Assert.Contains("NEW N", output.ToString());
        Assert.DoesNotContain("WARN", output.ToString());
    }

    /// <summary>
    /// Verifies compare-failing without the optional flaky list keeps its original behavior: every new failure is
    /// a NEW line and the exit code is 1.
    /// </summary>
    [Fact]
    public void Run_compare_failing_without_flaky_list_keeps_original_behavior() {
        string baselinePath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "noflaky-baseline.txt");
        string currentPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "noflaky-current.txt");
        File.WriteAllText(baselinePath, "A\n");
        File.WriteAllText(currentPath, "A\nF\n");
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare-failing", baselinePath, currentPath }, output);

        Assert.Equal(1, exitCode);
        Assert.Contains("NEW F", output.ToString());
        Assert.DoesNotContain("WARN", output.ToString());
    }

    /// <summary>
    /// Verifies comparing two identical BMP/PNG captures via the compare command passes with exit
    /// code 0.
    /// </summary>
    [Fact]
    public void Run_compare_identical_captures_passes_and_returns_0() {
        string capturePath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "capture.bmp");
        string goldenPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "golden.png");
        string diffPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "diff.png");
        RegressionImage image = RegressionTestFixtures.CreateUniformImage(4, 4, 10, 20, 30);
        RegressionTestFixtures.WriteBmp(capturePath, image);
        PngImageStore.Save(image, goldenPath);
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare", capturePath, goldenPath, diffPath }, output);

        Assert.Equal(0, exitCode);
        Assert.StartsWith("PASS", output.ToString());
    }

    /// <summary>
    /// Verifies a golden recorded from a capture whose alpha bytes are not opaque still matches that
    /// same capture. The player's back buffer alpha is undefined (the swap chain ignores it), and
    /// real captures carry alpha 0 where UI text was blended; a golden round trip must not let that
    /// alpha change the compared colors.
    /// </summary>
    [Fact]
    public void Run_compare_passes_against_a_golden_recorded_from_a_capture_with_transparent_alpha() {
        string capturePath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "transparent-capture.bmp");
        string goldenPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "transparent-golden.png");
        string diffPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "transparent-diff.png");
        byte[] bgra = {
            200, 180, 160, 0,
            200, 180, 160, 0,
            90, 60, 30, 224,
            10, 20, 30, 255
        };
        RegressionTestFixtures.WriteBmp(capturePath, new RegressionImage(2, 2, bgra));
        RegressionCommandRunner runner = new();
        Assert.Equal(0, runner.Run(new[] { "record-golden", capturePath, goldenPath }, new StringWriter()));
        StringWriter output = new();

        int exitCode = runner.Run(new[] { "compare", capturePath, goldenPath, diffPath }, output);

        Assert.Equal(0, exitCode);
        Assert.StartsWith("PASS 0", output.ToString());
    }
}
