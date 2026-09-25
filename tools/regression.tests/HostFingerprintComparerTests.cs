namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="HostFingerprintComparer"/> fails on any recorded host-field difference, on missing Presents and
/// on Present failures, and only warns on coarse pacing changes.
/// </summary>
public sealed class HostFingerprintComparerTests {
    /// <summary>
    /// Verifies identical fingerprints pass with no failures and no warnings.
    /// </summary>
    [Fact]
    public void Compare_identical_fingerprints_passes() {
        HostFingerprint fingerprint = HostFingerprint.Parse(HostFingerprintTests.SampleLine);

        HostFingerprintComparison comparison = HostFingerprintComparer.Compare("axis_test", fingerprint, fingerprint);

        Assert.True(comparison.Passed);
        Assert.Empty(comparison.Failures);
        Assert.Empty(comparison.Warnings);
    }

    /// <summary>
    /// Verifies a differing field fails with the documented "fingerprint &lt;scene&gt; &lt;field&gt; recorded= actual=" line.
    /// </summary>
    [Fact]
    public void Compare_reports_each_differing_field() {
        HostFingerprint recorded = HostFingerprint.Parse(HostFingerprintTests.SampleLine);
        HostFingerprint actual = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("buffers=2", "buffers=3").Replace("style=0x16CF0000", "style=0x06CF0000"));

        HostFingerprintComparison comparison = HostFingerprintComparer.Compare("axis_test", recorded, actual);

        Assert.False(comparison.Passed);
        Assert.Equal(
            new[] {
                "fingerprint axis_test buffers recorded=2 actual=3",
                "fingerprint axis_test style recorded=0x16CF0000 actual=0x06CF0000"
            },
            comparison.Failures);
    }

    /// <summary>
    /// Verifies a run that presented fewer times than it rendered frames fails even when the record had the same
    /// shortfall.
    /// </summary>
    [Fact]
    public void Compare_fails_when_present_count_is_below_frames() {
        HostFingerprint fingerprint = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("presentCount=30", "presentCount=29"));

        HostFingerprintComparison comparison = HostFingerprintComparer.Compare("axis_test", fingerprint, fingerprint);

        Assert.False(comparison.Passed);
        Assert.Contains("fingerprint axis_test presentCount actual=29 is below frames=30", comparison.Failures);
    }

    /// <summary>
    /// Verifies any Present failure fails even when the record had the same failures.
    /// </summary>
    [Fact]
    public void Compare_fails_when_present_failures_are_not_zero() {
        HostFingerprint fingerprint = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("presentFailures=0", "presentFailures=2"));

        HostFingerprintComparison comparison = HostFingerprintComparer.Compare("axis_test", fingerprint, fingerprint);

        Assert.False(comparison.Passed);
        Assert.Contains("fingerprint axis_test presentFailures actual=2 must be 0", comparison.Failures);
    }

    /// <summary>
    /// Verifies the elapsed time is never compared exactly: within a factor of three it neither fails nor warns.
    /// </summary>
    [Fact]
    public void Compare_ignores_pacing_within_a_factor_of_three() {
        HostFingerprint recorded = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("elapsedMs=483", "elapsedMs=500"));
        HostFingerprint slower = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("elapsedMs=483", "elapsedMs=1500"));
        HostFingerprint faster = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("elapsedMs=483", "elapsedMs=167"));

        Assert.Empty(HostFingerprintComparer.Compare("axis_test", recorded, slower).Warnings);
        Assert.Empty(HostFingerprintComparer.Compare("axis_test", recorded, faster).Warnings);
    }

    /// <summary>
    /// Verifies pacing more than three times slower or faster than the record only warns and never fails.
    /// </summary>
    [Fact]
    public void Compare_warns_on_pacing_outside_a_factor_of_three() {
        HostFingerprint recorded = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("elapsedMs=483", "elapsedMs=500"));
        HostFingerprint slower = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("elapsedMs=483", "elapsedMs=1501"));
        HostFingerprint faster = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("elapsedMs=483", "elapsedMs=166"));

        HostFingerprintComparison slowerComparison = HostFingerprintComparer.Compare("axis_test", recorded, slower);
        HostFingerprintComparison fasterComparison = HostFingerprintComparer.Compare("axis_test", recorded, faster);

        Assert.True(slowerComparison.Passed);
        Assert.Equal(new[] { "pacing axis_test recorded=500ms actual=1501ms" }, slowerComparison.Warnings);
        Assert.True(fasterComparison.Passed);
        Assert.Equal(new[] { "pacing axis_test recorded=500ms actual=166ms" }, fasterComparison.Warnings);
    }

    /// <summary>
    /// Verifies the single-run health check used at record time fails on missing Presents and Present failures.
    /// </summary>
    [Fact]
    public void CheckHealth_reports_missing_presents_and_present_failures() {
        HostFingerprint healthy = HostFingerprint.Parse(HostFingerprintTests.SampleLine);
        HostFingerprint unhealthy = HostFingerprint.Parse(HostFingerprintTests.SampleLine.Replace("presentCount=30", "presentCount=10").Replace("presentFailures=0", "presentFailures=1"));

        Assert.Empty(HostFingerprintComparer.CheckHealth("axis_test", healthy));
        Assert.Equal(
            new[] {
                "fingerprint axis_test presentCount actual=10 is below frames=30",
                "fingerprint axis_test presentFailures actual=1 must be 0"
            },
            HostFingerprintComparer.CheckHealth("axis_test", unhealthy));
    }
}
