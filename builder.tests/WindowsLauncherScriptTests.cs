namespace helengine.windows.builder.tests;

/// <summary>
/// Guards the canonical Windows launcher contract.
/// </summary>
public sealed class WindowsLauncherScriptTests {
    /// <summary>
    /// Ensures the canonical launcher requires one explicit artifact path and starts the built Windows executable directly.
    /// </summary>
    [Fact]
    public void Launcher_RequiresArtifactPath_AndLaunchesWindowsExecutable() {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string scriptPath = Path.Combine(repositoryRootPath, "scripts", "launch_in_emulator.ps1");

        Assert.True(File.Exists(scriptPath), "Expected scripts/launch_in_emulator.ps1 to exist.");

        string scriptSource = File.ReadAllText(scriptPath);

        Assert.Contains("[string]$ArtifactPath", scriptSource, StringComparison.Ordinal);
        Assert.Contains(".exe", scriptSource, StringComparison.Ordinal);
        Assert.Contains("Start-Process -FilePath $resolvedArtifactPath", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the root README documents the canonical launcher entrypoint.
    /// </summary>
    [Fact]
    public void Readme_DocumentsCanonicalLauncherWorkflow() {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string readmeSource = File.ReadAllText(Path.Combine(repositoryRootPath, "README.md"));

        Assert.Contains("launch_in_emulator.ps1", readmeSource, StringComparison.Ordinal);
        Assert.Contains("-ArtifactPath", readmeSource, StringComparison.Ordinal);
    }
}
