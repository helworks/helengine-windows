using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Windows player's opt-in <c>--capture</c> path: the DirectX11 back buffer of the last requested frame is
/// copied through a CPU-readable staging texture and written as a top-down 32-bit BMP, while runs without
/// <c>--capture</c> never create the capture object.
/// </summary>
public sealed class DirectX11BackBufferCaptureSourceTests {
    /// <summary>
    /// Verifies the capture copies the back buffer into a CPU-readable staging texture, honors the mapped row pitch and
    /// rejects any back-buffer format other than BGRA8.
    /// </summary>
    [Fact]
    public void DirectX11BackBufferCapture_reads_back_buffer_through_staging_texture() {
        string captureSource = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_back_buffer_capture.cpp");

        Assert.Contains("GetBuffer(0, __uuidof(ID3D11Texture2D)", captureSource, StringComparison.Ordinal);
        Assert.Contains("D3D11_USAGE_STAGING", captureSource, StringComparison.Ordinal);
        Assert.Contains("D3D11_CPU_ACCESS_READ", captureSource, StringComparison.Ordinal);
        Assert.Contains("CopyResource", captureSource, StringComparison.Ordinal);
        Assert.Contains("D3D11_MAP_READ", captureSource, StringComparison.Ordinal);
        Assert.Contains("RowPitch", captureSource, StringComparison.Ordinal);
        Assert.Contains("Unmap", captureSource, StringComparison.Ordinal);
        Assert.Contains("DXGI_FORMAT_B8G8R8A8_UNORM", captureSource, StringComparison.Ordinal);
        Assert.Contains("BmpImageWriter::WriteTopDownBgra32", captureSource, StringComparison.Ordinal);
        Assert.Contains("std::runtime_error", captureSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the BMP writer emits a top-down image by writing a negative height, and fails loudly on I/O errors.
    /// </summary>
    [Fact]
    public void BmpImageWriter_writes_top_down_bitmap_with_negative_height() {
        string writerSource = ReadRepositoryFile("src", "platform", "windows", "win32", "bmp_image_writer.cpp");

        Assert.Contains("-height", writerSource, StringComparison.Ordinal);
        Assert.Contains("std::ofstream", writerSource, StringComparison.Ordinal);
        Assert.Contains("std::ios::binary", writerSource, StringComparison.Ordinal);
        Assert.Contains("std::runtime_error", writerSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the application only constructs the back-buffer capture when <c>--capture</c> was supplied, so a
    /// no-argument run never creates it.
    /// </summary>
    [Fact]
    public void Win32Application_constructs_capture_only_when_capture_path_supplied() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string applicationHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.hpp");

        Assert.Contains("std::unique_ptr<DirectX11BackBufferCapture> BackBufferCapture;", applicationHeader, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(@"if \(CommandLineOptions\.HasCapturePath\(\)\) \{\s*BackBufferCapture = std::make_unique<DirectX11BackBufferCapture>\(\*Bootstrap\);\s*\}"),
            applicationSource);
        Assert.Single(Regex.Matches(applicationSource, @"std::make_unique<DirectX11BackBufferCapture>"));
    }

    /// <summary>
    /// Verifies the capture happens after the engine drew the final frame and before that frame is presented, and that
    /// capture failures become a deliberate exit with code 3.
    /// </summary>
    [Fact]
    public void Win32Application_captures_between_draw_and_present() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        int drawIndex = applicationSource.IndexOf("EngineCore->Draw();", StringComparison.Ordinal);
        int captureIndex = applicationSource.IndexOf("BackBufferCapture->CaptureToBmp(", StringComparison.Ordinal);
        int presentIndex = applicationSource.IndexOf("Presenter->RenderFrame();", StringComparison.Ordinal);
        Assert.True(drawIndex >= 0, "EngineCore->Draw(); was not found.");
        Assert.True(captureIndex > drawIndex, "CaptureToBmp must follow EngineCore->Draw();.");
        Assert.True(presentIndex > captureIndex, "Presenter->RenderFrame(); must follow CaptureToBmp.");

        Assert.Contains(
            "if (BackBufferCapture && CommandLineOptions.HasFrameLimit() && RenderedFrameCount + 1 == CommandLineOptions.GetFrameLimit())",
            applicationSource,
            StringComparison.Ordinal);
        Assert.Contains("throw Win32ExitRequest(3,", applicationSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the native build compiles both capture sources.
    /// </summary>
    [Fact]
    public void CMakeLists_compiles_capture_sources() {
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("src/platform/windows/directx11/directx11_back_buffer_capture.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("src/platform/windows/win32/bmp_image_writer.cpp", cmakeSource, StringComparison.Ordinal);
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
