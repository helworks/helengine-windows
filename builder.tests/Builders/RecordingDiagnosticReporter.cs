using helengine.baseplatform.Builders;
using helengine.baseplatform.Reporting;

namespace helengine.windows.builder.tests.Builders;

/// <summary>
/// Records build diagnostics for assertions.
/// </summary>
public sealed class RecordingDiagnosticReporter : IPlatformBuildDiagnosticReporter {
    /// <summary>
    /// Initializes one empty diagnostic recorder.
    /// </summary>
    public RecordingDiagnosticReporter() {
        Diagnostics = [];
    }

    /// <summary>
    /// Gets the diagnostics captured during a test run.
    /// </summary>
    public List<PlatformBuildDiagnostic> Diagnostics { get; }

    /// <summary>
    /// Records one diagnostic.
    /// </summary>
    /// <param name="diagnostic">The diagnostic to capture.</param>
    public void Report(PlatformBuildDiagnostic diagnostic) {
        Diagnostics.Add(diagnostic);
    }
}
