namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Windows profiler capture workflow remains explicit, bounded, and traceable.
/// </summary>
public sealed class WindowsProfilerCaptureWorkflowSourceTests {
    /// <summary>
    /// Verifies the capture helper validates explicit tools and package manifests before starting a bounded session.
    /// </summary>
    [Fact]
    public void Capture_helper_requires_explicit_inputs_and_preserves_package_artifacts() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string scriptPath = Path.Combine(repositoryRootPath, "scripts", "capture-windows-profiler.ps1");

        Assert.True(File.Exists(scriptPath), $"Expected Windows profiler capture helper at '{scriptPath}'.");

        string scriptSource = File.ReadAllText(scriptPath);
        Assert.Contains("ProfilerPackagePath", scriptSource, StringComparison.Ordinal);
        Assert.Contains("WindowsBuildManifestPath", scriptSource, StringComparison.Ordinal);
        Assert.Contains("WorkloadExecutablePath", scriptSource, StringComparison.Ordinal);
        Assert.Contains("WorkloadProjectPath", scriptSource, StringComparison.Ordinal);
        Assert.Contains("WorkloadArguments", scriptSource, StringComparison.Ordinal);
        Assert.Contains("TracyCapturePath", scriptSource, StringComparison.Ordinal);
        Assert.Contains("CaptureSeconds", scriptSource, StringComparison.Ordinal);
        Assert.Contains("generated_profiler_manifest.json", scriptSource, StringComparison.Ordinal);
        Assert.Contains("windows-build-manifest.json", scriptSource, StringComparison.Ordinal);
        Assert.Contains("Get-FileHash", scriptSource, StringComparison.Ordinal);
        Assert.Contains("Get-ChildItem", scriptSource, StringComparison.Ordinal);
        Assert.Contains("Copy-Item", scriptSource, StringComparison.Ordinal);
        Assert.Contains("Stop-Process", scriptSource, StringComparison.Ordinal);
        Assert.Contains("Start-Process", scriptSource, StringComparison.Ordinal);
        Assert.Contains("-o", scriptSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Remove-Item", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the documented example uses absolute paths and identifies the saved capture report.
    /// </summary>
    [Fact]
    public void Capture_workflow_documentation_uses_absolute_paths_and_describes_report_outputs() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string documentationPath = Path.Combine(repositoryRootPath, "docs", "WindowsProfilerCapture.md");

        Assert.True(File.Exists(documentationPath), $"Expected Windows profiler capture documentation at '{documentationPath}'.");

        string documentationSource = File.ReadAllText(documentationPath);
        Assert.Contains("C:\\", documentationSource, StringComparison.Ordinal);
        Assert.Contains("capture.tracy", documentationSource, StringComparison.Ordinal);
        Assert.Contains("capture-report.json", documentationSource, StringComparison.Ordinal);
        Assert.Contains("windows-build-manifest.json", documentationSource, StringComparison.Ordinal);
        Assert.Contains("generated_profiler_manifest.json", documentationSource, StringComparison.Ordinal);
        Assert.Contains("TracyCapturePath", documentationSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Resolves the Windows native-player repository root from the current test assembly location.
    /// </summary>
    /// <returns>Absolute repository root path.</returns>
    static string ResolveWindowsRepositoryRootPath() {
        string assemblyDirectoryPath = AppContext.BaseDirectory;
        string repositoryRootPath = Path.GetFullPath(Path.Combine(assemblyDirectoryPath, "..", "..", "..", ".."));
        if (!Directory.Exists(repositoryRootPath)) {
            throw new InvalidOperationException($"Could not resolve the Windows repository root from '{assemblyDirectoryPath}'.");
        }

        return repositoryRootPath;
    }
}
