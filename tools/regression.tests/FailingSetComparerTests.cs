namespace helengine.windows.regression.tests;

/// <summary>
/// Verifies <see cref="FailingSetComparer"/> identifies new and fixed failures between a baseline
/// and a current failing test set.
/// </summary>
public sealed class FailingSetComparerTests {
    /// <summary>
    /// Verifies a current set with one new failure and one fixed failure reports both and fails.
    /// </summary>
    [Fact]
    public void Compare_reports_new_and_fixed_failures_and_fails() {
        FailingSetComparison comparison = FailingSetComparer.Compare(new[] { "A", "B" }, new[] { "B", "C" });

        Assert.Equal(new[] { "C" }, comparison.NewFailures);
        Assert.Equal(new[] { "A" }, comparison.FixedFailures);
        Assert.False(comparison.Passed);
    }

    /// <summary>
    /// Verifies a current set that is a subset of the baseline passes with no new failures.
    /// </summary>
    [Fact]
    public void Compare_current_subset_of_baseline_passes() {
        FailingSetComparison comparison = FailingSetComparer.Compare(new[] { "A", "B", "C" }, new[] { "B" });

        Assert.Empty(comparison.NewFailures);
        Assert.True(comparison.Passed);
    }

    /// <summary>
    /// Verifies a new failure that is listed as known-flaky is reported as a flaky failure instead of a new
    /// failure, so the comparison still passes.
    /// </summary>
    [Fact]
    public void Compare_new_failure_in_flaky_list_is_reported_as_flaky_and_passes() {
        FailingSetComparison comparison = FailingSetComparer.Compare(new[] { "A" }, new[] { "A", "F" }, new[] { "F" });

        Assert.Empty(comparison.NewFailures);
        Assert.Equal(new[] { "F" }, comparison.FlakyFailures);
        Assert.True(comparison.Passed);
    }

    /// <summary>
    /// Verifies a flaky list does not excuse a new failure that is not on it.
    /// </summary>
    [Fact]
    public void Compare_new_failure_not_in_flaky_list_still_fails() {
        FailingSetComparison comparison = FailingSetComparer.Compare(new[] { "A" }, new[] { "A", "F", "N" }, new[] { "F" });

        Assert.Equal(new[] { "N" }, comparison.NewFailures);
        Assert.Equal(new[] { "F" }, comparison.FlakyFailures);
        Assert.False(comparison.Passed);
    }
}
