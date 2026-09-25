namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="IdleScenarioChecker"/> passes an idle-scenario run that meets every minimum and reports each
/// failed check, in the documented order throttle, Present failures, idle frames, elapsed time.
/// </summary>
public sealed class IdleScenarioCheckerTests {
    /// <summary>
    /// A healthy idle-scenario fingerprint line: throttle on, no Present failures, 28 idle frames and about 3 s elapsed.
    /// </summary>
    const string IdleSampleLine = "HOST_FINGERPRINT format=87 alpha=3 swapEffect=4 buffers=2 scaling=0 style=0x16CF0000 exStyle=0x00000100 client=640x360 presentCount=30 presentFailures=0 frames=30 idleThrottle=on idleFrames=28 activeFrames=2 elapsedMs=2900";

    /// <summary>
    /// The fewest idle frames the regression script requires of the idle scenario.
    /// </summary>
    const long MinimumIdleFrames = 25;

    /// <summary>
    /// The shortest elapsed time, in milliseconds, the regression script requires of the idle scenario.
    /// </summary>
    const long MinimumElapsedMilliseconds = 2400;

    /// <summary>
    /// Verifies a run that meets every minimum, including exactly at the minimums, passes with no failures.
    /// </summary>
    [Fact]
    public void Check_passes_a_run_that_meets_every_minimum() {
        HostFingerprint healthy = HostFingerprint.Parse(IdleSampleLine);
        HostFingerprint atMinimums = HostFingerprint.Parse(IdleSampleLine.Replace("idleFrames=28", "idleFrames=25").Replace("elapsedMs=2900", "elapsedMs=2400"));

        Assert.Empty(IdleScenarioChecker.Check(healthy, MinimumIdleFrames, MinimumElapsedMilliseconds));
        Assert.Empty(IdleScenarioChecker.Check(atMinimums, MinimumIdleFrames, MinimumElapsedMilliseconds));
    }

    /// <summary>
    /// Verifies a run with fewer idle frames than the minimum fails with the idle-frames reason only.
    /// </summary>
    [Fact]
    public void Check_fails_when_too_few_frames_ran_idle() {
        HostFingerprint fingerprint = HostFingerprint.Parse(IdleSampleLine.Replace("idleFrames=28", "idleFrames=24").Replace("activeFrames=2", "activeFrames=6"));

        Assert.Equal(
            new[] { "idleFrames=24 is below 25" },
            IdleScenarioChecker.Check(fingerprint, MinimumIdleFrames, MinimumElapsedMilliseconds));
    }

    /// <summary>
    /// Verifies a run that finished faster than the minimum elapsed time, meaning the throttle did not sleep long
    /// enough, fails with the elapsed-time reason only.
    /// </summary>
    [Fact]
    public void Check_fails_when_the_run_was_too_fast() {
        HostFingerprint fingerprint = HostFingerprint.Parse(IdleSampleLine.Replace("elapsedMs=2900", "elapsedMs=1000"));

        Assert.Equal(
            new[] { "elapsedMs=1000 is below 2400" },
            IdleScenarioChecker.Check(fingerprint, MinimumIdleFrames, MinimumElapsedMilliseconds));
    }

    /// <summary>
    /// Verifies a run whose idle throttle was off fails with the throttle reason, even when its counts would pass.
    /// </summary>
    [Fact]
    public void Check_fails_when_the_throttle_was_off() {
        HostFingerprint fingerprint = HostFingerprint.Parse(IdleSampleLine.Replace("idleThrottle=on", "idleThrottle=off"));

        Assert.Equal(
            new[] { "idleThrottle=off must be on" },
            IdleScenarioChecker.Check(fingerprint, MinimumIdleFrames, MinimumElapsedMilliseconds));
    }

    /// <summary>
    /// Verifies any Present failure fails the run with the Present-failures reason.
    /// </summary>
    [Fact]
    public void Check_fails_when_present_failures_are_not_zero() {
        HostFingerprint fingerprint = HostFingerprint.Parse(IdleSampleLine.Replace("presentFailures=0", "presentFailures=1"));

        Assert.Equal(
            new[] { "presentFailures=1 must be 0" },
            IdleScenarioChecker.Check(fingerprint, MinimumIdleFrames, MinimumElapsedMilliseconds));
    }

    /// <summary>
    /// Verifies a run failing every check reports all four reasons in the documented order: throttle, Present failures,
    /// idle frames, elapsed time.
    /// </summary>
    [Fact]
    public void Check_reports_multiple_failures_in_documented_order() {
        HostFingerprint fingerprint = HostFingerprint.Parse(IdleSampleLine
            .Replace("idleThrottle=on", "idleThrottle=off")
            .Replace("presentFailures=0", "presentFailures=2")
            .Replace("idleFrames=28", "idleFrames=0")
            .Replace("activeFrames=2", "activeFrames=30")
            .Replace("elapsedMs=2900", "elapsedMs=480"));

        Assert.Equal(
            new[] {
                "idleThrottle=off must be on",
                "presentFailures=2 must be 0",
                "idleFrames=0 is below 25",
                "elapsedMs=480 is below 2400"
            },
            IdleScenarioChecker.Check(fingerprint, MinimumIdleFrames, MinimumElapsedMilliseconds));
    }
}
