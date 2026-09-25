namespace helengine.windows.regression;

/// <summary>
/// Result of comparing a verify run's host fingerprint with the recorded one: the failing checks and the pacing
/// warnings, each already formatted as the detail of a regression-net check line.
/// </summary>
public sealed class HostFingerprintComparison {
    /// <summary>
    /// Initializes a host-fingerprint comparison result.
    /// </summary>
    /// <param name="failures">Failing checks, for example "fingerprint axis_test buffers recorded=2 actual=3".</param>
    /// <param name="warnings">Pacing warnings, for example "pacing axis_test recorded=483ms actual=1600ms".</param>
    public HostFingerprintComparison(IReadOnlyList<string> failures, IReadOnlyList<string> warnings) {
        Failures = failures;
        Warnings = warnings;
    }

    /// <summary>
    /// Gets the failing checks; any entry fails the comparison.
    /// </summary>
    public IReadOnlyList<string> Failures { get; }

    /// <summary>
    /// Gets the pacing warnings; they never fail the comparison.
    /// </summary>
    public IReadOnlyList<string> Warnings { get; }

    /// <summary>
    /// Gets whether the comparison passed, meaning there are no failing checks.
    /// </summary>
    public bool Passed => Failures.Count == 0;
}
