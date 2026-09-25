namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="TrxExecutedCountChecker"/> fails aborted or errored runs and runs that executed less than 95% of
/// the recorded tests, and only warns on smaller differences in either direction.
/// </summary>
public sealed class TrxExecutedCountCheckerTests {
    /// <summary>
    /// Verifies a completed or failed run with exactly the recorded count passes.
    /// </summary>
    [Fact]
    public void Check_passes_when_executed_matches_record() {
        TrxExecutedCountCheck check = TrxExecutedCountChecker.Check(new TrxResultSummary("Failed", 200), 200);

        Assert.Equal(TrxExecutedCountCheckStatus.Pass, check.Status);
        Assert.Equal("executed 200 matches recorded 200", check.Detail);
    }

    /// <summary>
    /// Verifies an Error or Aborted outcome fails even when the executed count matches.
    /// </summary>
    [Theory]
    [InlineData("Error")]
    [InlineData("Aborted")]
    public void Check_fails_on_error_or_aborted_outcome(string outcome) {
        TrxExecutedCountCheck check = TrxExecutedCountChecker.Check(new TrxResultSummary(outcome, 200), 200);

        Assert.Equal(TrxExecutedCountCheckStatus.Fail, check.Status);
        Assert.Equal($"outcome {outcome}", check.Detail);
    }

    /// <summary>
    /// Verifies a run below 95% of the recorded count fails, and exactly 95% only warns.
    /// </summary>
    [Fact]
    public void Check_fails_below_ninety_five_percent_and_warns_at_it() {
        TrxExecutedCountCheck below = TrxExecutedCountChecker.Check(new TrxResultSummary("Completed", 189), 200);
        TrxExecutedCountCheck atLimit = TrxExecutedCountChecker.Check(new TrxResultSummary("Completed", 190), 200);

        Assert.Equal(TrxExecutedCountCheckStatus.Fail, below.Status);
        Assert.Equal("executed 189 is below 95% of recorded 200", below.Detail);
        Assert.Equal(TrxExecutedCountCheckStatus.Warn, atLimit.Status);
        Assert.Equal("executed 190 differs from recorded 200", atLimit.Detail);
    }

    /// <summary>
    /// Verifies more executed tests than recorded only warns.
    /// </summary>
    [Fact]
    public void Check_warns_when_more_tests_executed() {
        TrxExecutedCountCheck check = TrxExecutedCountChecker.Check(new TrxResultSummary("Completed", 260), 200);

        Assert.Equal(TrxExecutedCountCheckStatus.Warn, check.Status);
        Assert.Equal("executed 260 differs from recorded 200", check.Detail);
    }

    /// <summary>
    /// Verifies the record-time check fails an Error or Aborted run and accepts any other outcome.
    /// </summary>
    [Fact]
    public void IsRecordable_rejects_error_and_aborted_outcomes() {
        Assert.False(TrxExecutedCountChecker.IsRecordable(new TrxResultSummary("Aborted", 10)));
        Assert.False(TrxExecutedCountChecker.IsRecordable(new TrxResultSummary("Error", 10)));
        Assert.True(TrxExecutedCountChecker.IsRecordable(new TrxResultSummary("Failed", 10)));
        Assert.True(TrxExecutedCountChecker.IsRecordable(new TrxResultSummary("Completed", 10)));
    }
}
