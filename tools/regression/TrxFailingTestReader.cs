namespace helengine.windows.regression;

using System.Xml.Linq;

/// <summary>
/// Reads the sorted, distinct set of failing test names (every outcome except Passed and
/// NotExecuted) out of a Visual Studio TRX test result file, so a regression run can be compared
/// against a known-failing baseline.
/// </summary>
public static class TrxFailingTestReader {
    /// <summary>
    /// XML namespace used by Visual Studio TRX test result files.
    /// </summary>
    static readonly XNamespace TrxNamespace = "http://microsoft.com/schemas/VisualStudio/TeamTest/2010";

    /// <summary>
    /// Reads a TRX file and returns the sorted, distinct names of every test whose outcome was
    /// neither "Passed" nor "NotExecuted". That includes "Failed", "Error", "Timeout", "Aborted",
    /// a missing outcome and any outcome this reader does not know, so a broken or cut-short test
    /// can never read as passing.
    /// </summary>
    /// <param name="trxPath">Path to the TRX file to read.</param>
    /// <returns>Sorted, distinct failing test names.</returns>
    public static IReadOnlyList<string> Read(string trxPath) {
        XDocument document = XDocument.Load(trxPath);
        HashSet<string> failingNames = new();
        foreach (XElement result in document.Descendants(TrxNamespace + "UnitTestResult")) {
            string outcome = (string)result.Attribute("outcome");
            bool passed = string.Equals(outcome, "Passed", StringComparison.Ordinal);
            bool notExecuted = string.Equals(outcome, "NotExecuted", StringComparison.Ordinal);
            if (!passed && !notExecuted) {
                failingNames.Add((string)result.Attribute("testName"));
            }
        }

        List<string> sorted = new(failingNames);
        sorted.Sort(StringComparer.Ordinal);
        return sorted;
    }
}
