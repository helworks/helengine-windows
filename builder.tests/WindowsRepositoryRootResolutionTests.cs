using helengine.windows.builder;

namespace helengine.windows.builder.tests;

/// <summary>Checks isolated Windows builders select a validated native source checkout without relying on their publication directory.</summary>
public sealed class WindowsRepositoryRootResolutionTests {
    /// <summary>Uses the configured checkout even when an isolated DLL's parent directories would select the unrelated workspace root.</summary>
    [Fact]
    public void Resolve_prefers_valid_explicit_repository_over_isolated_assembly_location() {
        string root = FixtureRoot(nameof(Resolve_prefers_valid_explicit_repository_over_isolated_assembly_location));
        WriteRepositoryMarkers(root);
        string resolved = WindowsBuildWorkspace.ResolveRepositoryRoot(
            Path.Combine(root, "unused", ".."),
            "C:/dev/helworks/builds/demodisc/windows/builder/helengine.windows.builder.dll");
        Assert.Equal(Path.GetFullPath(root), resolved);
    }

    /// <summary>Rejects a relative environment path instead of letting a changed builder working directory select a different source tree.</summary>
    [Fact]
    public void Resolve_rejects_relative_explicit_repository() {
        Assert.Throws<InvalidOperationException>(() => WindowsBuildWorkspace.ResolveRepositoryRoot("helengine-windows", "ignored.dll"));
    }

    /// <summary>Rejects missing CMake or Windows application markers before launching the native toolchain.</summary>
    [Theory]
    [InlineData(false, false)]
    [InlineData(true, false)]
    [InlineData(false, true)]
    public void Resolve_rejects_incomplete_explicit_repository(bool hasCmake, bool hasApplication) {
        string root = FixtureRoot(nameof(Resolve_rejects_incomplete_explicit_repository) + hasCmake + hasApplication);
        string cmake = Path.Combine(root, "CMakeLists.txt");
        string application = Path.Combine(root, "src", "platform", "windows", "win32", "win32_application.cpp");
        Directory.CreateDirectory(Path.GetDirectoryName(application));
        if (hasCmake) { File.WriteAllText(cmake, "fixture CMake marker"); } else { File.Delete(cmake); }
        if (hasApplication) { File.WriteAllText(application, "fixture native application marker"); } else { File.Delete(application); }
        Assert.Throws<InvalidOperationException>(() => WindowsBuildWorkspace.ResolveRepositoryRoot(root, "ignored.dll"));
    }

    /// <summary>Preserves the established installed-builder layout when no repository override is configured.</summary>
    [Fact]
    public void Resolve_without_override_preserves_installed_assembly_layout() {
        string root = FixtureRoot(nameof(Resolve_without_override_preserves_installed_assembly_layout));
        string assembly = Path.Combine(root, "builder", "bin", "Release", "net9.0", "helengine.windows.builder.dll");
        Assert.Equal(Path.GetFullPath(root), WindowsBuildWorkspace.ResolveRepositoryRoot(string.Empty, assembly));
    }

    /// <summary>Creates fixed visible fixtures without changing process-wide environment variables or temporary directories.</summary>
    static string FixtureRoot(string name) {
        string root = Path.Combine("C:/dev/helworks/builds/demodisc/windows/repository-root-tests", name);
        Directory.CreateDirectory(root);
        return root;
    }

    /// <summary>Writes only the two source markers required for a valid explicit native checkout.</summary>
    static void WriteRepositoryMarkers(string root) {
        string application = Path.Combine(root, "src", "platform", "windows", "win32", "win32_application.cpp");
        Directory.CreateDirectory(Path.GetDirectoryName(application));
        File.WriteAllText(application, "fixture native application marker");
        File.WriteAllText(Path.Combine(root, "CMakeLists.txt"), "fixture CMake marker");
    }
}
