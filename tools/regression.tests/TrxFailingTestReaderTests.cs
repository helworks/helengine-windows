namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="TrxFailingTestReader"/> extracts the sorted, distinct set of failed test
/// names from a Visual Studio TRX result file.
/// </summary>
public sealed class TrxFailingTestReaderTests {
    /// <summary>
    /// Verifies failed test names are returned sorted and de-duplicated, with passed tests
    /// excluded from the result.
    /// </summary>
    [Fact]
    public void Read_returns_sorted_distinct_failed_test_names() {
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "results.trx");
        File.WriteAllText(path, """
            <?xml version="1.0" encoding="UTF-8"?>
            <TestRun xmlns="http://microsoft.com/schemas/VisualStudio/TeamTest/2010">
              <Results>
                <UnitTestResult testName="Namespace.ClassC.Test_Three" outcome="Failed" />
                <UnitTestResult testName="Namespace.ClassB.Test_Two" outcome="Passed" />
                <UnitTestResult testName="Namespace.ClassA.Test_One" outcome="Failed" />
                <UnitTestResult testName="Namespace.ClassA.Test_One" outcome="Failed" />
              </Results>
            </TestRun>
            """);

        IReadOnlyList<string> failing = TrxFailingTestReader.Read(path);

        Assert.Equal(new[] { "Namespace.ClassA.Test_One", "Namespace.ClassC.Test_Three" }, failing);
    }

    /// <summary>
    /// Verifies every outcome other than Passed and NotExecuted counts as failing, including Error, Timeout, Aborted
    /// and outcomes the reader does not know, so a broken or cut-short test can never read as passing.
    /// </summary>
    [Fact]
    public void Read_counts_every_outcome_except_passed_and_not_executed_as_failing() {
        string path = Path.Combine(RegressionTestFixtures.TestOutputDirectory, "outcomes.trx");
        File.WriteAllText(path, """
            <?xml version="1.0" encoding="UTF-8"?>
            <TestRun xmlns="http://microsoft.com/schemas/VisualStudio/TeamTest/2010">
              <Results>
                <UnitTestResult testName="T.Passed" outcome="Passed" />
                <UnitTestResult testName="T.Skipped" outcome="NotExecuted" />
                <UnitTestResult testName="T.Failed" outcome="Failed" />
                <UnitTestResult testName="T.Error" outcome="Error" />
                <UnitTestResult testName="T.Timeout" outcome="Timeout" />
                <UnitTestResult testName="T.Aborted" outcome="Aborted" />
                <UnitTestResult testName="T.Unknown" outcome="SomethingNew" />
              </Results>
            </TestRun>
            """);

        IReadOnlyList<string> failing = TrxFailingTestReader.Read(path);

        Assert.Equal(new[] { "T.Aborted", "T.Error", "T.Failed", "T.Timeout", "T.Unknown" }, failing);
    }
}
