namespace helengine.windows.regression;

/// <summary>
/// Result of checking a test run's summary against its recorded executed-test count: a status and a readable detail.
/// </summary>
public sealed class TrxExecutedCountCheck {
    /// <summary>
    /// Initializes an executed-count check result.
    /// </summary>
    /// <param name="status">Whether the run passes, only warns or fails.</param>
    /// <param name="detail">The readable reason, for example "executed 189 is below 95% of recorded 200".</param>
    public TrxExecutedCountCheck(TrxExecutedCountCheckStatus status, string detail) {
        Status = status;
        Detail = detail;
    }

    /// <summary>
    /// Gets whether the run passes, only warns or fails.
    /// </summary>
    public TrxExecutedCountCheckStatus Status { get; }

    /// <summary>
    /// Gets the readable reason for the status.
    /// </summary>
    public string Detail { get; }
}
