namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies visible Windows test output cannot escape its dedicated build root through caller-controlled categories.
/// </summary>
public sealed class WindowsTestOutputPathResolverTests {
    /// <summary>
    /// Ensures unsafe category shapes are rejected before any removable path is returned.
    /// </summary>
    /// <param name="category">Unsafe rooted, traversing, or nested category.</param>
    [Theory]
    [InlineData("..")]
    [InlineData("nested/path")]
    [InlineData("nested\\path")]
    [InlineData("C:\\outside")]
    public void CreateUniqueRoot_WhenCategoryIsUnsafe_Throws(string category) {
        Assert.Throws<ArgumentException>(() => WindowsTestOutputPathResolver.CreateUniqueRoot(category));
    }

    /// <summary>
    /// Ensures a valid category resolves beneath the dedicated visible test output root.
    /// </summary>
    [Fact]
    public void CreateUniqueRoot_WhenCategoryIsSafe_ReturnsVisibleContainedPath() {
        string rootPath = WindowsTestOutputPathResolver.CreateUniqueRoot("safe-category");

        Assert.Contains(
            Path.Combine("builds", "helphysics-demodisc-stack-test", "tests", "safe-category"),
            rootPath,
            StringComparison.OrdinalIgnoreCase);
    }
}
