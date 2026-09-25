namespace helengine.windows.regression;

using System.Globalization;
using System.Xml.Linq;

/// <summary>
/// Reads the ResultSummary outcome and the executed-test counter out of a Visual Studio TRX test result file.
/// </summary>
public static class TrxResultSummaryReader {
    /// <summary>
    /// XML namespace used by Visual Studio TRX test result files.
    /// </summary>
    static readonly XNamespace TrxNamespace = "http://microsoft.com/schemas/VisualStudio/TeamTest/2010";

    /// <summary>
    /// Reads a TRX file's ResultSummary. Throws <see cref="InvalidDataException"/> when the ResultSummary, its outcome
    /// or its Counters executed value is missing or not a whole number, because a truncated result file must never
    /// read as a healthy run.
    /// </summary>
    /// <param name="trxPath">Path to the TRX file to read.</param>
    /// <returns>The run outcome and executed-test count.</returns>
    public static TrxResultSummary Read(string trxPath) {
        XDocument document = XDocument.Load(trxPath);
        XElement resultSummary = document.Descendants(TrxNamespace + "ResultSummary").FirstOrDefault();
        if (resultSummary == null) {
            throw new InvalidDataException($"TRX file has no ResultSummary: {trxPath}");
        }

        string outcome = (string)resultSummary.Attribute("outcome");
        if (string.IsNullOrEmpty(outcome)) {
            throw new InvalidDataException($"TRX ResultSummary has no outcome: {trxPath}");
        }

        XElement counters = resultSummary.Element(TrxNamespace + "Counters");
        string executedText = counters == null ? null : (string)counters.Attribute("executed");
        if (!int.TryParse(executedText, NumberStyles.None, CultureInfo.InvariantCulture, out int executed)) {
            throw new InvalidDataException($"TRX ResultSummary has no whole-number Counters executed value: {trxPath}");
        }

        return new TrxResultSummary(outcome, executed);
    }
}
