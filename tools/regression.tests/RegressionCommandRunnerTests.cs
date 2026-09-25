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
    /// Verifies compare-fingerprint prints PASS and returns 0 for matching fingerprints.
    /// </summary>
    [Fact]
    public void Run_compare_fingerprint_matching_passes_and_returns_0() {
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare-fingerprint", "axis_test", HostFingerprintTests.SampleLine, HostFingerprintTests.SampleLine }, output);

        Assert.Equal(0, exitCode);
        Assert.Equal("PASS" + Environment.NewLine, output.ToString());
    }

    /// <summary>
    /// Verifies compare-fingerprint prints one "FAIL fingerprint" line per differing field and returns 1, and prints a
    /// pacing WARN line without failing on its own.
    /// </summary>
    [Fact]
    public void Run_compare_fingerprint_prints_fail_and_warn_lines() {
        string actualLine = HostFingerprintTests.SampleLine.Replace("buffers=2", "buffers=3").Replace("elapsedMs=483", "elapsedMs=5000");
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare-fingerprint", "axis_test", HostFingerprintTests.SampleLine, actualLine }, output);

        Assert.Equal(1, exitCode);
        string[] lines = output.ToString().Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries);
        Assert.Equal(new[] { "FAIL", "FAIL fingerprint axis_test buffers recorded=2 actual=3", "WARN pacing axis_test recorded=483ms actual=5000ms" }, lines);
    }

    /// <summary>
    /// Verifies a pacing warning alone keeps compare-fingerprint passing.
    /// </summary>
    [Fact]
    public void Run_compare_fingerprint_pacing_warning_alone_returns_0() {
        string actualLine = HostFingerprintTests.SampleLine.Replace("elapsedMs=483", "elapsedMs=5000");
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "compare-fingerprint", "axis_test", HostFingerprintTests.SampleLine, actualLine }, output);

        Assert.Equal(0, exitCode);
        Assert.StartsWith("PASS", output.ToString());
        Assert.Contains("WARN pacing axis_test recorded=483ms actual=5000ms", output.ToString());
    }

    /// <summary>
    /// Verifies check-fingerprint fails a single run with Present failures.
    /// </summary>
    [Fact]
    public void Run_check_fingerprint_fails_on_present_failures() {
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "check-fingerprint", "axis_test", HostFingerprintTests.SampleLine.Replace("presentFailures=0", "presentFailures=1") }, output);

        Assert.Equal(1, exitCode);
        Assert.Contains("FAIL fingerprint axis_test presentFailures actual=1 must be 0", output.ToString());
    }

    /// <summary>
    /// Verifies record-executed writes the executed count of a healthy run as a single integer.
    /// </summary>
    [Fact]
    public void Run_record_executed_writes_the_executed_count() {
        string trxPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "record-executed.trx");
        string executedPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "record-executed.txt");
        File.WriteAllText(trxPath, BuildSummaryTrx("Failed", 42));
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "record-executed", trxPath, executedPath }, output);

        Assert.Equal(0, exitCode);
        Assert.Equal("42\n", File.ReadAllText(executedPath));
        Assert.StartsWith("RECORDED 42", output.ToString());
    }

    /// <summary>
    /// Verifies record-executed refuses an aborted run and writes nothing.
    /// </summary>
    [Fact]
    public void Run_record_executed_refuses_an_aborted_run() {
        string trxPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "record-aborted.trx");
        string executedPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "record-aborted.txt");
        File.WriteAllText(trxPath, BuildSummaryTrx("Aborted", 3));
        if (File.Exists(executedPath)) {
            File.Delete(executedPath);
        }
        StringWriter output = new();

        int exitCode = new RegressionCommandRunner().Run(new[] { "record-executed", trxPath, executedPath }, output);

        Assert.Equal(1, exitCode);
        Assert.StartsWith("FAIL outcome Aborted", output.ToString());
        Assert.False(File.Exists(executedPath));
    }

    /// <summary>
    /// Verifies check-executed prints PASS, WARN or FAIL with the documented exit codes.
    /// </summary>
    [Fact]
    public void Run_check_executed_prints_status_and_exit_code() {
        string executedPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "check-executed.txt");
        string matchingPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "check-matching.trx");
        string warnPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "check-warn.trx");
        string failPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "check-fail.trx");
        File.WriteAllText(executedPath, "100\n");
        File.WriteAllText(matchingPath, BuildSummaryTrx("Failed", 100));
        File.WriteAllText(warnPath, BuildSummaryTrx("Completed", 97));
        File.WriteAllText(failPath, BuildSummaryTrx("Completed", 50));
        RegressionCommandRunner runner = new();
        StringWriter matchingOutput = new();
        StringWriter warnOutput = new();
        StringWriter failOutput = new();

        Assert.Equal(0, runner.Run(new[] { "check-executed", matchingPath, executedPath }, matchingOutput));
        Assert.Equal(0, runner.Run(new[] { "check-executed", warnPath, executedPath }, warnOutput));
        Assert.Equal(1, runner.Run(new[] { "check-executed", failPath, executedPath }, failOutput));
        Assert.Equal("PASS executed 100 matches recorded 100" + Environment.NewLine, matchingOutput.ToString());
        Assert.Equal("WARN executed 97 differs from recorded 100" + Environment.NewLine, warnOutput.ToString());
        Assert.Equal("FAIL executed 50 is below 95% of recorded 100" + Environment.NewLine, failOutput.ToString());
    }

    /// <summary>
    /// Builds a minimal TRX document with the given ResultSummary outcome and executed counter.
    /// </summary>
    static string BuildSummaryTrx(string outcome, int executed) {
        return $"<?xml version=\"1.0\" encoding=\"UTF-8\"?><TestRun xmlns=\"http://microsoft.com/schemas/VisualStudio/TeamTest/2010\"><ResultSummary outcome=\"{outcome}\"><Counters total=\"{executed}\" executed=\"{executed}\" /></ResultSummary></TestRun>";
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
