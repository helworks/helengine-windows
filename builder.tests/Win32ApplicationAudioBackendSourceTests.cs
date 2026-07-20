namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the native Windows host owns and registers its audio backend.
/// </summary>
public sealed class Win32ApplicationAudioBackendSourceTests {
    /// <summary>
    /// Verifies the Win32 application declares, creates, registers, and destroys the native audio backend.
    /// </summary>
    [Fact]
    public void Win32Application_owns_and_registers_audio_backend() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string headerPath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_application.hpp");
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_application.cpp");

        string headerSource = File.ReadAllText(headerPath);
        string sourceCode = File.ReadAllText(sourcePath);

        Assert.Contains("class Win32AudioBackend;", headerSource, StringComparison.Ordinal);
        Assert.Contains("Win32AudioBackend* EngineAudioBackend;", headerSource, StringComparison.Ordinal);
        Assert.Contains("#include \"platform/windows/win32/win32_audio_backend.hpp\"", sourceCode, StringComparison.Ordinal);
        Assert.Contains("EngineAudioBackend = new Win32AudioBackend();", sourceCode, StringComparison.Ordinal);
        Assert.Contains("EngineCore->SetAudioBackend(EngineAudioBackend);", sourceCode, StringComparison.Ordinal);
        Assert.Contains("delete EngineAudioBackend;", sourceCode, StringComparison.Ordinal);
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
