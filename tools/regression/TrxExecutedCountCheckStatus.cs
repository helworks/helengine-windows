namespace helengine.windows.regression;

/// <summary>
/// The outcome of checking a test run's summary against its recorded executed-test count.
/// </summary>
public enum TrxExecutedCountCheckStatus {
    /// <summary>
    /// The run completed normally and executed exactly the recorded number of tests.
    /// </summary>
    Pass,

    /// <summary>
    /// The run completed normally but executed a different number of tests, still at least 95% of the record.
    /// </summary>
    Warn,

    /// <summary>
    /// The run errored or aborted, or executed fewer than 95% of the recorded number of tests.
    /// </summary>
    Fail
}
