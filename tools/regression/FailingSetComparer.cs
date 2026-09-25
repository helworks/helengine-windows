namespace helengine.windows.regression;

/// <summary>
/// Compares a baseline set of known-failing test names against a current set, to detect newly
/// introduced regressions while tolerating already-known, pre-existing failures and, optionally,
/// explicitly listed known-flaky tests.
/// </summary>
public static class FailingSetComparer {
    /// <summary>
    /// Compares the baseline and current failing test name sets with no known-flaky tests, so every
    /// failure that is missing from the baseline counts as new.
    /// </summary>
    /// <param name="baseline">Previously recorded failing test names.</param>
    /// <param name="current">Failing test names from the current run.</param>
    /// <returns>The comparison, listing new and fixed failures.</returns>
    public static FailingSetComparison Compare(IReadOnlyList<string> baseline, IReadOnlyList<string> current) {
        return Compare(baseline, current, Array.Empty<string>());
    }

    /// <summary>
    /// Compares the baseline and current failing test name sets. A failure missing from the baseline
    /// that appears in the known-flaky list is reported as flaky and does not fail the comparison;
    /// every other failure missing from the baseline is new and fails it.
    /// </summary>
    /// <param name="baseline">Previously recorded failing test names.</param>
    /// <param name="current">Failing test names from the current run.</param>
    /// <param name="flaky">Test names known to pass or fail from run to run.</param>
    /// <returns>The comparison, listing new, flaky and fixed failures.</returns>
    public static FailingSetComparison Compare(IReadOnlyList<string> baseline, IReadOnlyList<string> current, IReadOnlyList<string> flaky) {
        HashSet<string> baselineSet = new(baseline);
        HashSet<string> currentSet = new(current);
        HashSet<string> flakySet = new(flaky);

        List<string> unexpectedFailures = current.Where(name => !baselineSet.Contains(name)).ToList();
        List<string> newFailures = unexpectedFailures.Where(name => !flakySet.Contains(name)).ToList();
        List<string> flakyFailures = unexpectedFailures.Where(name => flakySet.Contains(name)).ToList();
        List<string> fixedFailures = baseline.Where(name => !currentSet.Contains(name)).ToList();
        newFailures.Sort(StringComparer.Ordinal);
        flakyFailures.Sort(StringComparer.Ordinal);
        fixedFailures.Sort(StringComparer.Ordinal);

        return new FailingSetComparison(newFailures, flakyFailures, fixedFailures, newFailures.Count == 0);
    }
}
