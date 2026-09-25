namespace helengine.windows.regression;

/// <summary>
/// Applies the regression net's idle-scenario rules to one run's host fingerprint. The idle scenario runs the smoke scene
/// with the opt-in idle throttle on, so its fingerprint must show that the throttle was enabled, that no Present
/// failed, that enough frames ran idle, and that the run took long enough to prove the throttle actually slept. The
/// idle and active frame split and the elapsed time depend on load timing, so they are checked against minimums rather
/// than compared with a record.
/// </summary>
public static class IdleScenarioChecker {
    /// <summary>
    /// The idleThrottle value the player writes when the idle throttle was enabled.
    /// </summary>
    const string ThrottleOnValue = "on";

    /// <summary>
    /// Checks an idle-scenario run's fingerprint against the scenario's minimums.
    /// </summary>
    /// <param name="fingerprint">The idle-scenario run's fingerprint.</param>
    /// <param name="minimumIdleFrames">The fewest frames that must have run idle.</param>
    /// <param name="minimumElapsedMilliseconds">The shortest wall-clock time, first to last Present, the run may take.</param>
    /// <returns>
    /// One reason per failed check, in the order throttle, Present failures, idle frames, elapsed time; empty when the
    /// run passed.
    /// </returns>
    public static IReadOnlyList<string> Check(HostFingerprint fingerprint, long minimumIdleFrames, long minimumElapsedMilliseconds) {
        List<string> failures = new();
        if (!string.Equals(fingerprint.IdleThrottle, ThrottleOnValue, StringComparison.Ordinal)) {
            failures.Add($"idleThrottle={fingerprint.IdleThrottle} must be {ThrottleOnValue}");
        }

        if (fingerprint.PresentFailures != 0) {
            failures.Add($"presentFailures={fingerprint.PresentFailures} must be 0");
        }

        if (fingerprint.IdleFrames < minimumIdleFrames) {
            failures.Add($"idleFrames={fingerprint.IdleFrames} is below {minimumIdleFrames}");
        }

        if (fingerprint.ElapsedMilliseconds < minimumElapsedMilliseconds) {
            failures.Add($"elapsedMs={fingerprint.ElapsedMilliseconds} is below {minimumElapsedMilliseconds}");
        }

        return failures;
    }
}
