namespace helengine.windows.regression;

/// <summary>
/// Applies the regression net's rules for truncated or aborted test runs: an Error or Aborted run outcome fails, a run
/// that executed fewer than 95% of the recorded tests fails, and any other difference in the executed count only warns.
/// </summary>
public static class TrxExecutedCountChecker {
    /// <summary>
    /// The lowest share of the recorded executed count, in percent, that a run may execute without failing.
    /// </summary>
    const long MinimumExecutedPercent = 95;

    /// <summary>
    /// Checks a run's summary against the executed count recorded with its baseline.
    /// </summary>
    /// <param name="summary">The run's ResultSummary outcome and executed count.</param>
    /// <param name="recordedExecuted">The executed count recorded at record time.</param>
    /// <returns>The status and a readable detail.</returns>
    public static TrxExecutedCountCheck Check(TrxResultSummary summary, int recordedExecuted) {
        if (!IsRecordable(summary)) {
            return new TrxExecutedCountCheck(TrxExecutedCountCheckStatus.Fail, $"outcome {summary.Outcome}");
        }

        if ((long)summary.Executed * 100 < (long)recordedExecuted * MinimumExecutedPercent) {
            return new TrxExecutedCountCheck(TrxExecutedCountCheckStatus.Fail, $"executed {summary.Executed} is below {MinimumExecutedPercent}% of recorded {recordedExecuted}");
        }

        if (summary.Executed != recordedExecuted) {
            return new TrxExecutedCountCheck(TrxExecutedCountCheckStatus.Warn, $"executed {summary.Executed} differs from recorded {recordedExecuted}");
        }

        return new TrxExecutedCountCheck(TrxExecutedCountCheckStatus.Pass, $"executed {summary.Executed} matches recorded {recordedExecuted}");
    }

    /// <summary>
    /// Returns whether a run may be recorded as a baseline, meaning its outcome is neither Error nor Aborted.
    /// </summary>
    /// <param name="summary">The run's ResultSummary outcome and executed count.</param>
    /// <returns>True when the run's outcome is neither Error nor Aborted.</returns>
    public static bool IsRecordable(TrxResultSummary summary) {
        return !string.Equals(summary.Outcome, "Error", StringComparison.Ordinal) && !string.Equals(summary.Outcome, "Aborted", StringComparison.Ordinal);
    }
}
