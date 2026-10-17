using System.Runtime.CompilerServices;

namespace helengine.windows.builder.tests;

/// <summary>Locates this real Windows checkout independently of the test binary output directory.</summary>
static class WindowsTestRepository {
    /// <summary>Uses the test's compiled source path so out-of-tree artifacts still inspect the actual checkout.</summary>
    /// <param name="testSourceFilePath">Absolute source path supplied by the compiler at the test call site.</param>
    /// <returns>The repository containing the calling builder test source.</returns>
    public static string ResolveRootPath([CallerFilePath] string testSourceFilePath = "") {
        if (string.IsNullOrWhiteSpace(testSourceFilePath)) {
            throw new ArgumentException("The calling test source path is required.", nameof(testSourceFilePath));
        }
        return Path.GetFullPath(Path.Combine(Path.GetDirectoryName(testSourceFilePath), ".."));
    }
}
