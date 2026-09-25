namespace helengine.windows.regression;

/// <summary>
/// Applies the regression net's host-fingerprint rules: every compared field must equal the record, the run must have
/// presented at least once per frame with no Present failures, and wall-clock pacing more than three times slower or
/// faster than the record is only a warning because pacing is noisy.
/// </summary>
public static class HostFingerprintComparer {
    /// <summary>
    /// The factor by which the elapsed time may differ from the record, in either direction, before a pacing warning.
    /// </summary>
    const long PacingWarningFactor = 3;

    /// <summary>
    /// Compares a verify run's fingerprint with the recorded one.
    /// </summary>
    /// <param name="sceneId">The scene the runs rendered, used in every reported line.</param>
    /// <param name="recorded">The fingerprint stored at record time.</param>
    /// <param name="actual">The fingerprint of the verify run.</param>
    /// <returns>The failing field differences and health checks, and any pacing warning.</returns>
    public static HostFingerprintComparison Compare(string sceneId, HostFingerprint recorded, HostFingerprint actual) {
        List<string> failures = new();
        foreach (string fieldName in HostFingerprint.ComparedFieldNames) {
            string recordedValue = recorded.Fields[fieldName];
            string actualValue = actual.Fields[fieldName];
            if (!string.Equals(recordedValue, actualValue, StringComparison.Ordinal)) {
                failures.Add($"fingerprint {sceneId} {fieldName} recorded={recordedValue} actual={actualValue}");
            }
        }

        failures.AddRange(CheckHealth(sceneId, actual));

        List<string> warnings = new();
        bool muchSlower = actual.ElapsedMilliseconds > recorded.ElapsedMilliseconds * PacingWarningFactor;
        bool muchFaster = actual.ElapsedMilliseconds * PacingWarningFactor < recorded.ElapsedMilliseconds;
        if (muchSlower || muchFaster) {
            warnings.Add($"pacing {sceneId} recorded={recorded.ElapsedMilliseconds}ms actual={actual.ElapsedMilliseconds}ms");
        }

        return new HostFingerprintComparison(failures, warnings);
    }

    /// <summary>
    /// Checks one run on its own: it must have presented at least once per rendered frame and no Present may have failed.
    /// </summary>
    /// <param name="sceneId">The scene the run rendered, used in every reported line.</param>
    /// <param name="fingerprint">The run's fingerprint.</param>
    /// <returns>The failing health checks; empty when the run is healthy.</returns>
    public static IReadOnlyList<string> CheckHealth(string sceneId, HostFingerprint fingerprint) {
        List<string> failures = new();
        if (fingerprint.PresentCount < fingerprint.Frames) {
            failures.Add($"fingerprint {sceneId} presentCount actual={fingerprint.PresentCount} is below frames={fingerprint.Frames}");
        }

        if (fingerprint.PresentFailures != 0) {
            failures.Add($"fingerprint {sceneId} presentFailures actual={fingerprint.PresentFailures} must be 0");
        }

        return failures;
    }
}
