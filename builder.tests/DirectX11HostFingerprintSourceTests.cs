using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Windows player's opt-in host fingerprint: in <c>--frames</c> mode the player logs one
/// <c>HOST_FINGERPRINT</c> line describing the swap chain, the window styles, the client size, the Present count and
/// failures and the wall-clock time of the run, while a no-argument run never creates the fingerprint or inspects the
/// Present result.
/// </summary>
public sealed class DirectX11HostFingerprintSourceTests {
    /// <summary>
    /// Verifies the presenter hands the Present HRESULT back to its caller instead of dropping it.
    /// </summary>
    [Fact]
    public void DirectX11Presenter_returns_present_result() {
        string presenterHeader = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_presenter.hpp");
        string presenterSource = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_presenter.cpp");

        Assert.Contains("HRESULT RenderFrame();", presenterHeader, StringComparison.Ordinal);
        Assert.Contains("HRESULT DirectX11Presenter::RenderFrame() {", presenterSource, StringComparison.Ordinal);
        Assert.Contains("return Bootstrap.GetSwapChain()->Present(1, 0);", presenterSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the fingerprint reads the swap-chain description, the Present count, both window styles and the client
    /// rectangle, and writes every documented field in the documented order.
    /// </summary>
    [Fact]
    public void DirectX11HostFingerprint_describes_every_documented_field() {
        string fingerprintSource = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_host_fingerprint.cpp");

        Assert.Contains("GetDesc1(", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("GetLastPresentCount(", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("GWL_STYLE", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("GWL_EXSTYLE", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("GetClientRect(", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("std::chrono::steady_clock", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("FAILED(presentResult)", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("std::string DirectX11HostFingerprint::Describe(int frameCount, bool idleThrottleEnabled, int idleFrames, int activeFrames) const {", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("<< \" idleThrottle=\" << (idleThrottleEnabled ? \"on\" : \"off\")", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("<< \" idleFrames=\" << idleFrames", fingerprintSource, StringComparison.Ordinal);
        Assert.Contains("<< \" activeFrames=\" << activeFrames", fingerprintSource, StringComparison.Ordinal);

        string fingerprintHeader = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_host_fingerprint.hpp");
        Assert.Contains("std::string Describe(int frameCount, bool idleThrottleEnabled, int idleFrames, int activeFrames) const;", fingerprintHeader, StringComparison.Ordinal);

        string[] orderedFields = {
            "\"HOST_FINGERPRINT format=\"",
            "\" alpha=\"",
            "\" swapEffect=\"",
            "\" buffers=\"",
            "\" scaling=\"",
            "\" style=0x\"",
            "\" exStyle=0x\"",
            "\" client=\"",
            "\" presentCount=\"",
            "\" presentFailures=\"",
            "\" frames=\"",
            "\" idleThrottle=\"",
            "\" idleFrames=\"",
            "\" activeFrames=\"",
            "\" elapsedMs=\""
        };
        int previousIndex = -1;
        foreach (string field in orderedFields) {
            int fieldIndex = fingerprintSource.IndexOf(field, StringComparison.Ordinal);
            Assert.True(fieldIndex > previousIndex, $"Field {field} is missing or out of order.");
            previousIndex = fieldIndex;
        }
    }

    /// <summary>
    /// Verifies the application only creates the fingerprint when <c>--frames</c> was supplied, and only touches it
    /// (failure counting, failure logging and the final line) inside the existing frame-limit block, so the no-argument
    /// path only receives the Present result and ignores it.
    /// </summary>
    [Fact]
    public void Win32Application_uses_fingerprint_only_in_frame_limit_mode() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string applicationHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.hpp");

        Assert.Contains("std::unique_ptr<DirectX11HostFingerprint> HostFingerprint;", applicationHeader, StringComparison.Ordinal);
        Assert.Contains("int IdleFrameCount;", applicationHeader, StringComparison.Ordinal);
        Assert.Contains("int ActiveFrameCount;", applicationHeader, StringComparison.Ordinal);
        Assert.Matches(new Regex(@"CurrentFrameIsIdle\(false\),\s*IdleFrameCount\(0\),\s*ActiveFrameCount\(0\)"), applicationSource);
        Assert.Single(Regex.Matches(applicationSource, @"std::make_unique<DirectX11HostFingerprint>"));
        Assert.Matches(
            new Regex(@"if \(CommandLineOptions\.HasFrameLimit\(\)\) \{\s*HostFingerprint = std::make_unique<DirectX11HostFingerprint>\(\*Bootstrap, MainWindow->GetHandle\(\)\);\s*\}"),
            applicationSource);

        Assert.Contains("presentResult = Presenter->RenderFrame();", applicationSource, StringComparison.Ordinal);
        Match frameLimitBlock = Regex.Match(
            applicationSource,
            @"if \(CommandLineOptions\.HasFrameLimit\(\)\) \{\s*if \(HostFingerprint->RecordPresent\(presentResult\)\) \{\s*std::string presentFailureMessage = DirectX11HostFingerprint::DescribePresentFailure\(presentResult\);\s*WriteLifecycleLog\(presentFailureMessage\.c_str\(\)\);\s*\}\s*RenderedFrameCount\+\+;\s*if \(CurrentFrameIsIdle\) \{\s*IdleFrameCount\+\+;\s*\} else \{\s*ActiveFrameCount\+\+;\s*\}\s*if \(RenderedFrameCount >= CommandLineOptions\.GetFrameLimit\(\)\) \{\s*std::string fingerprintLine = HostFingerprint->Describe\(RenderedFrameCount, IdleFramePacer != nullptr, IdleFrameCount, ActiveFrameCount\);\s*WriteLifecycleLog\(fingerprintLine\.c_str\(\)\);\s*PostQuitMessage\(0\);\s*\}\s*\}");
        Assert.True(frameLimitBlock.Success, "The frame-limit block must count Present failures, split frames into idle and active counts, and log the fingerprint before PostQuitMessage(0).");

        // Every read of the fingerprint and of the Present result is one of the matched lines inside the block above.
        Assert.Equal(2, Regex.Matches(applicationSource, @"HostFingerprint->").Count);
        Assert.Equal(2, Regex.Matches(applicationSource, @"\(presentResult\)").Count);
        Assert.DoesNotContain("HOST_FINGERPRINT", applicationSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the native build compiles the fingerprint source.
    /// </summary>
    [Fact]
    public void CMakeLists_compiles_fingerprint_source() {
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("src/platform/windows/directx11/directx11_host_fingerprint.cpp", cmakeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Reads a source file relative to the Windows native-player repository root.
    /// </summary>
    /// <param name="relativePathSegments">Path segments below the repository root.</param>
    /// <returns>The file text.</returns>
    static string ReadRepositoryFile(params string[] relativePathSegments) {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string filePath = Path.Combine(repositoryRootPath, Path.Combine(relativePathSegments));
        if (!File.Exists(filePath)) {
            throw new FileNotFoundException($"Source file was not found under the Windows repository root '{repositoryRootPath}'.", filePath);
        }

        return File.ReadAllText(filePath);
    }
}
