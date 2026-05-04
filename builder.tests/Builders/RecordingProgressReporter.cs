using helengine.baseplatform.Builders;
using helengine.baseplatform.Reporting;

namespace helengine.windows.builder.tests.Builders;

/// <summary>
/// Records build progress updates for assertions.
/// </summary>
public sealed class RecordingProgressReporter : IPlatformBuildProgressReporter {
    /// <summary>
    /// Initializes one empty progress recorder.
    /// </summary>
    public RecordingProgressReporter() {
        Updates = [];
    }

    /// <summary>
    /// Gets the progress updates captured during a test run.
    /// </summary>
    public List<PlatformBuildProgressUpdate> Updates { get; }

    /// <summary>
    /// Records one progress update.
    /// </summary>
    /// <param name="update">The update to capture.</param>
    public void Report(PlatformBuildProgressUpdate update) {
        Updates.Add(update);
    }
}
