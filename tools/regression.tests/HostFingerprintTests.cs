namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="HostFingerprint.Parse"/> reads the player's HOST_FINGERPRINT line strictly: every documented
/// field must be present exactly once, and anything else is rejected instead of guessed.
/// </summary>
public sealed class HostFingerprintTests {
    /// <summary>
    /// A complete fingerprint line exactly as the player writes it (after the "[Host] " log prefix).
    /// </summary>
    public const string SampleLine = "HOST_FINGERPRINT format=87 alpha=3 swapEffect=4 buffers=2 scaling=0 style=0x16CF0000 exStyle=0x00000100 client=640x360 presentCount=30 presentFailures=0 frames=30 idleThrottle=off idleFrames=0 activeFrames=30 elapsedMs=483";

    /// <summary>
    /// Verifies every compared field and the elapsed time are read from a complete line.
    /// </summary>
    [Fact]
    public void Parse_reads_every_field_and_the_elapsed_time() {
        HostFingerprint fingerprint = HostFingerprint.Parse(SampleLine);

        Assert.Equal("87", fingerprint.Fields["format"]);
        Assert.Equal("0x16CF0000", fingerprint.Fields["style"]);
        Assert.Equal("640x360", fingerprint.Fields["client"]);
        Assert.Equal("30", fingerprint.Fields["frames"]);
        Assert.Equal("off", fingerprint.Fields["idleThrottle"]);
        Assert.Equal("0", fingerprint.Fields["idleFrames"]);
        Assert.Equal("30", fingerprint.Fields["activeFrames"]);
        Assert.Equal(HostFingerprint.ComparedFieldNames.Count, fingerprint.Fields.Count);
        Assert.False(fingerprint.Fields.ContainsKey("elapsedMs"));
        Assert.Equal(483, fingerprint.ElapsedMilliseconds);
    }

    /// <summary>
    /// Verifies the leading HOST_FINGERPRINT marker is optional, so a line rebuilt from the manifest's fields parses
    /// the same way, and field order does not matter.
    /// </summary>
    [Fact]
    public void Parse_accepts_fields_without_the_marker_in_any_order() {
        HostFingerprint fingerprint = HostFingerprint.Parse("elapsedMs=10 activeFrames=30 idleFrames=0 idleThrottle=off frames=30 presentFailures=0 presentCount=30 client=640x360 exStyle=0x00000100 style=0x16CF0000 scaling=0 buffers=2 swapEffect=4 alpha=3 format=87");

        Assert.Equal("4", fingerprint.Fields["swapEffect"]);
        Assert.Equal("off", fingerprint.Fields["idleThrottle"]);
        Assert.Equal(10, fingerprint.ElapsedMilliseconds);
    }

    /// <summary>
    /// Verifies a line missing a documented field is rejected.
    /// </summary>
    [Fact]
    public void Parse_rejects_a_missing_field() {
        Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine.Replace(" buffers=2", string.Empty)));
    }

    /// <summary>
    /// Verifies a line written before the idle throttle existed, which has no idleThrottle, idleFrames or activeFrames
    /// field, is rejected instead of being read with guessed idle counts.
    /// </summary>
    [Fact]
    public void Parse_rejects_an_old_line_without_the_idle_fields() {
        string oldLine = "HOST_FINGERPRINT format=87 alpha=3 swapEffect=4 buffers=2 scaling=0 style=0x16CF0000 exStyle=0x00000100 client=640x360 presentCount=30 presentFailures=0 frames=30 elapsedMs=483";

        Assert.Throws<FormatException>(() => HostFingerprint.Parse(oldLine));
    }

    /// <summary>
    /// Verifies a line missing the elapsed time is rejected.
    /// </summary>
    [Fact]
    public void Parse_rejects_a_missing_elapsed_time() {
        Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine.Replace(" elapsedMs=483", string.Empty)));
    }

    /// <summary>
    /// Verifies an unknown field, a repeated field and a token without '=' are all rejected.
    /// </summary>
    [Fact]
    public void Parse_rejects_unknown_repeated_and_malformed_tokens() {
        Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine + " vsync=1"));
        Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine + " format=87"));
        Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine + " garbage"));
    }

    /// <summary>
    /// Verifies non-numeric counters are rejected, because presentCount, presentFailures, frames, idleFrames, activeFrames and elapsedMs are
    /// compared as numbers.
    /// </summary>
    [Fact]
    public void Parse_rejects_non_numeric_counters() {
        Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine.Replace("presentCount=30", "presentCount=many")));
        Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine.Replace("elapsedMs=483", "elapsedMs=")));
        Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine.Replace("idleFrames=0", "idleFrames=some")));
        Assert.Throws<FormatException>(() => HostFingerprint.Parse(SampleLine.Replace("activeFrames=30", "activeFrames=-1")));
    }
}
