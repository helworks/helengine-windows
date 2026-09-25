namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="TrxResultSummaryReader"/> reads the run outcome and the executed-test count from a TRX file's
/// ResultSummary element.
/// </summary>
public sealed class TrxResultSummaryReaderTests {
    /// <summary>
    /// Verifies the ResultSummary outcome and its Counters executed value are read.
    /// </summary>
    [Fact]
    public void Read_returns_outcome_and_executed_count() {
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "summary.trx");
        File.WriteAllText(path, BuildTrx("<ResultSummary outcome=\"Failed\"><Counters total=\"12\" executed=\"10\" passed=\"8\" failed=\"2\" /></ResultSummary>"));

        TrxResultSummary summary = TrxResultSummaryReader.Read(path);

        Assert.Equal("Failed", summary.Outcome);
        Assert.Equal(10, summary.Executed);
    }

    /// <summary>
    /// Verifies a TRX without a ResultSummary, without its outcome or without an executed counter is rejected, because
    /// a truncated result file must never read as a healthy run.
    /// </summary>
    [Fact]
    public void Read_rejects_a_missing_summary_outcome_or_counter() {
        string noSummaryPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "no-summary.trx");
        string noOutcomePath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "no-outcome.trx");
        string noExecutedPath = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "no-executed.trx");
        File.WriteAllText(noSummaryPath, BuildTrx(string.Empty));
        File.WriteAllText(noOutcomePath, BuildTrx("<ResultSummary><Counters executed=\"10\" /></ResultSummary>"));
        File.WriteAllText(noExecutedPath, BuildTrx("<ResultSummary outcome=\"Completed\"><Counters total=\"10\" /></ResultSummary>"));

        Assert.Throws<InvalidDataException>(() => TrxResultSummaryReader.Read(noSummaryPath));
        Assert.Throws<InvalidDataException>(() => TrxResultSummaryReader.Read(noOutcomePath));
        Assert.Throws<InvalidDataException>(() => TrxResultSummaryReader.Read(noExecutedPath));
    }

    /// <summary>
    /// Wraps a ResultSummary fragment in a minimal TRX document.
    /// </summary>
    static string BuildTrx(string resultSummaryXml) {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?><TestRun xmlns=\"http://microsoft.com/schemas/VisualStudio/TeamTest/2010\"><Results /> " + resultSummaryXml + "</TestRun>";
    }
}
