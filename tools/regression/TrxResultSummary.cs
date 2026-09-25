namespace helengine.windows.regression;

/// <summary>
/// The run-level result of a <c>dotnet test</c> TRX file: the ResultSummary outcome and how many tests were executed.
/// Used to catch test runs that aborted, errored or were cut short, which would otherwise report few failures.
/// </summary>
public sealed class TrxResultSummary {
    /// <summary>
    /// Initializes a TRX result summary.
    /// </summary>
    /// <param name="outcome">The ResultSummary outcome attribute, for example "Completed", "Failed", "Error" or "Aborted".</param>
    /// <param name="executed">The ResultSummary Counters executed attribute.</param>
    public TrxResultSummary(string outcome, int executed) {
        Outcome = outcome;
        Executed = executed;
    }

    /// <summary>
    /// Gets the ResultSummary outcome attribute as written by the test logger.
    /// </summary>
    public string Outcome { get; }

    /// <summary>
    /// Gets the number of tests the run executed.
    /// </summary>
    public int Executed { get; }
}
