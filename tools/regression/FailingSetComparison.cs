namespace helengine.windows.regression;

/// <summary>
/// Result of comparing a baseline failing-test set against a current failing-test set.
/// </summary>
public sealed class FailingSetComparison {
    /// <summary>
    /// Initializes a failing-set comparison result.
    /// </summary>
    /// <param name="newFailures">Names present in the current set but not the baseline.</param>
    /// <param name="fixedFailures">Names present in the baseline but not the current set.</param>
    /// <param name="passed">Whether no new failures were introduced.</param>
    public FailingSetComparison(IReadOnlyList<string> newFailures, IReadOnlyList<string> fixedFailures, bool passed) {
        NewFailures = newFailures;
        FixedFailures = fixedFailures;
        Passed = passed;
    }

    /// <summary>
    /// Gets the sorted test names that fail now but did not fail in the baseline.
    /// </summary>
    public IReadOnlyList<string> NewFailures { get; }

    /// <summary>
    /// Gets the sorted test names that failed in the baseline but no longer fail, informational only.
    /// </summary>
    public IReadOnlyList<string> FixedFailures { get; }

    /// <summary>
    /// Gets whether the comparison passed, meaning no new failures were introduced.
    /// </summary>
    public bool Passed { get; }
}
