namespace helengine.windows.builder.tests;

/// <summary>
/// Resolves unique Windows builder test output beneath the visible workspace builds directory.
/// </summary>
static class WindowsTestOutputPathResolver {
    /// <summary>
    /// Creates one unique absolute test root beneath the shared workspace's visible build output.
    /// </summary>
    /// <param name="category">Stable test category used to group related output.</param>
    /// <returns>Unique absolute directory path that the caller may create and later remove.</returns>
    public static string CreateUniqueRoot(string category) {
        if (string.IsNullOrWhiteSpace(category)) {
            throw new ArgumentException("Windows test output category must be provided.", nameof(category));
        }
        if (Path.IsPathRooted(category) ||
            category == "." ||
            category == ".." ||
            category.IndexOf(Path.DirectorySeparatorChar) >= 0 ||
            category.IndexOf(Path.AltDirectorySeparatorChar) >= 0) {
            throw new ArgumentException("Windows test output category must be one safe path segment.", nameof(category));
        }

        DirectoryInfo currentDirectory = new DirectoryInfo(AppContext.BaseDirectory);
        while (currentDirectory != null) {
            string markerPath = Path.Combine(
                currentDirectory.FullName,
                "builder.tests",
                "helengine.windows.builder.tests.csproj");
            if (File.Exists(markerPath)) {
                DirectoryInfo repositoryRoot = currentDirectory;
                DirectoryInfo repositoryParent = repositoryRoot.Parent
                    ?? throw new InvalidOperationException("Windows repository parent directory could not be resolved for test output.");
                DirectoryInfo sharedRepositoryRoot = repositoryRoot;
                if (string.Equals(repositoryParent.Name, ".worktrees", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(repositoryParent.Name, "worktrees", StringComparison.OrdinalIgnoreCase)) {
                    sharedRepositoryRoot = repositoryParent.Parent
                        ?? throw new InvalidOperationException("Shared Windows repository root could not be resolved from the test worktree.");
                }

                DirectoryInfo workspaceRoot = sharedRepositoryRoot.Parent
                    ?? throw new InvalidOperationException("Shared Windows workspace root could not be resolved for test output.");
                string testOutputRootPath = Path.GetFullPath(Path.Combine(
                    workspaceRoot.FullName,
                    "builds",
                    "helphysics-demodisc-stack-test",
                    "tests"));
                string uniqueRootPath = Path.GetFullPath(Path.Combine(
                    testOutputRootPath,
                    category,
                    Guid.NewGuid().ToString("N")));
                string requiredPrefix = testOutputRootPath.TrimEnd(
                    Path.DirectorySeparatorChar,
                    Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
                if (!uniqueRootPath.StartsWith(requiredPrefix, StringComparison.OrdinalIgnoreCase)) {
                    throw new InvalidOperationException("Resolved Windows test output escaped the intended visible test root.");
                }

                return uniqueRootPath;
            }

            currentDirectory = currentDirectory.Parent;
        }

        throw new InvalidOperationException("Windows repository root could not be resolved from the test assembly location.");
    }
}
