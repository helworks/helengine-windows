namespace helengine.windows.regression;

using System.Xml.Linq;

/// <summary>
/// Reads the sorted, distinct set of failed test names out of a Visual Studio TRX test result
/// file, so a regression run can be compared against a known-failing baseline.
/// </summary>
public static class TrxFailingTestReader {
    /// <summary>
    /// XML namespace used by Visual Studio TRX test result files.
    /// </summary>
    static readonly XNamespace TrxNamespace = "http://microsoft.com/schemas/VisualStudio/TeamTest/2010";

    /// <summary>
    /// Reads a TRX file and returns the sorted, distinct names of every test whose outcome was
    /// "Failed".
    /// </summary>
    /// <param name="trxPath">Path to the TRX file to read.</param>
    /// <returns>Sorted, distinct failed test names.</returns>
    public static IReadOnlyList<string> Read(string trxPath) {
        XDocument document = XDocument.Load(trxPath);
        HashSet<string> failingNames = new();
        foreach (XElement result in document.Descendants(TrxNamespace + "UnitTestResult")) {
            if (string.Equals((string)result.Attribute("outcome"), "Failed", StringComparison.Ordinal)) {
                failingNames.Add((string)result.Attribute("testName"));
            }
        }

        List<string> sorted = new(failingNames);
        sorted.Sort(StringComparer.Ordinal);
        return sorted;
    }
}
