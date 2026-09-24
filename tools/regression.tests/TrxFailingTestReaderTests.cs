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
}
