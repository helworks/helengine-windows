namespace helengine.windows.regression;

/// <summary>
/// Compares a baseline set of known-failing test names against a current set, to detect newly
/// introduced regressions while tolerating already-known, pre-existing failures.
/// </summary>
public static class FailingSetComparer {
    /// <summary>
    /// Compares the baseline and current failing test name sets.
    /// </summary>
    /// <param name="baseline">Previously recorded failing test names.</param>
    /// <param name="current">Failing test names from the current run.</param>
    /// <returns>The comparison, listing new and fixed failures.</returns>
    public static FailingSetComparison Compare(IReadOnlyList<string> baseline, IReadOnlyList<string> current) {
        HashSet<string> baselineSet = new(baseline);
        HashSet<string> currentSet = new(current);

        List<string> newFailures = current.Where(name => !baselineSet.Contains(name)).ToList();
        List<string> fixedFailures = baseline.Where(name => !currentSet.Contains(name)).ToList();
        newFailures.Sort(StringComparer.Ordinal);
        fixedFailures.Sort(StringComparer.Ordinal);

        return new FailingSetComparison(newFailures, fixedFailures, newFailures.Count == 0);
    }
}
