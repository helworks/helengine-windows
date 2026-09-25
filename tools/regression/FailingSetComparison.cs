namespace helengine.windows.regression;

/// <summary>
/// Result of comparing a baseline failing-test set against a current failing-test set.
/// </summary>
public sealed class FailingSetComparison {
    /// <summary>
    /// Initializes a failing-set comparison result.
    /// </summary>
    /// <param name="newFailures">Names present in the current set but not the baseline, and not known-flaky.</param>
    /// <param name="flakyFailures">Names present in the current set but not the baseline that are known-flaky.</param>
    /// <param name="fixedFailures">Names present in the baseline but not the current set.</param>
    /// <param name="passed">Whether no new failures were introduced.</param>
    public FailingSetComparison(IReadOnlyList<string> newFailures, IReadOnlyList<string> flakyFailures, IReadOnlyList<string> fixedFailures, bool passed) {
        NewFailures = newFailures;
        FlakyFailures = flakyFailures;
        FixedFailures = fixedFailures;
        Passed = passed;
    }

    /// <summary>
    /// Gets the sorted test names that fail now, did not fail in the baseline and are not known-flaky.
    /// </summary>
    public IReadOnlyList<string> NewFailures { get; }

    /// <summary>
    /// Gets the sorted known-flaky test names that fail now but did not fail in the baseline. They are
    /// reported as warnings and never fail the comparison.
    /// </summary>
    public IReadOnlyList<string> FlakyFailures { get; }

    /// <summary>
    /// Gets the sorted test names that failed in the baseline but no longer fail, informational only.
    /// </summary>
    public IReadOnlyList<string> FixedFailures { get; }

    /// <summary>
    /// Gets whether the comparison passed, meaning no new failures were introduced.
    /// </summary>
    public bool Passed { get; }
}
