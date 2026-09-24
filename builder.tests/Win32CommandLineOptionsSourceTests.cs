namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Windows player exposes the opt-in regression command-line options (scene, frame limit, fixed delta,
/// capture path) while keeping the default no-argument startup path unchanged.
/// </summary>
public sealed class Win32CommandLineOptionsSourceTests {
    /// <summary>
    /// Verifies the command-line parser recognizes every supported flag, reads the real process command line and
    /// reports parse failures through <c>std::invalid_argument</c>.
    /// </summary>
    [Fact]
    public void Win32CommandLineOptions_parses_supported_flags_from_process_command_line() {
        string parserSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_command_line_options.cpp");

        Assert.Contains("\"--scene\"", parserSource, StringComparison.Ordinal);
        Assert.Contains("\"--frames\"", parserSource, StringComparison.Ordinal);
        Assert.Contains("\"--fixed-delta\"", parserSource, StringComparison.Ordinal);
        Assert.Contains("\"--capture\"", parserSource, StringComparison.Ordinal);
        Assert.Contains("CommandLineToArgvW", parserSource, StringComparison.Ordinal);
        Assert.Contains("LocalFree", parserSource, StringComparison.Ordinal);
        Assert.Contains("std::invalid_argument", parserSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the native build compiles the parser and exit-request sources and links the shell library that
    /// provides <c>CommandLineToArgvW</c>.
    /// </summary>
    [Fact]
    public void CMakeLists_compiles_command_line_sources_and_links_shell32() {
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("win32_command_line_options.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("win32_exit_request.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("shell32", cmakeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the application parses the command line, honors the fixed delta, and requests shutdown only after a
    /// frame has been presented.
    /// </summary>
    [Fact]
    public void Win32Application_applies_command_line_options_to_the_frame_loop() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        Assert.Contains("ParseProcessCommandLine()", applicationSource, StringComparison.Ordinal);
        Assert.Contains("GetFixedDeltaSeconds()", applicationSource, StringComparison.Ordinal);

        int presentIndex = applicationSource.IndexOf("Presenter->RenderFrame();", StringComparison.Ordinal);
        int quitIndex = applicationSource.IndexOf("PostQuitMessage(0)", StringComparison.Ordinal);
        Assert.True(presentIndex >= 0, "Presenter->RenderFrame(); was not found.");
        Assert.True(quitIndex > presentIndex, "PostQuitMessage(0) must follow Presenter->RenderFrame();.");
    }

    /// <summary>
    /// Verifies the exit-request catch runs before the generic standard-exception catch so its exit code is not
    /// swallowed into a generic failure.
    /// </summary>
    [Fact]
    public void Win32Application_catches_exit_request_before_generic_exceptions() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        int exitRequestCatchIndex = applicationSource.IndexOf("catch (const Win32ExitRequest&", StringComparison.Ordinal);
        int genericCatchIndex = applicationSource.IndexOf("catch (const std::exception", StringComparison.Ordinal);
        Assert.True(exitRequestCatchIndex >= 0, "The Win32ExitRequest catch was not found.");
        Assert.True(exitRequestCatchIndex < genericCatchIndex, "The Win32ExitRequest catch must precede catch (const std::exception.");
    }

    /// <summary>
    /// Verifies that without arguments the player still loads the first catalog entry and advances the engine with
    /// the parameterless update call.
    /// </summary>
    [Fact]
    public void Win32Application_without_arguments_keeps_default_startup_path() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        Assert.Contains("(*catalogEntries)[0]", applicationSource, StringComparison.Ordinal);

        Assert.Matches(
            @"\}\s*else\s*\{\s*EngineCore->Update\(\);\s*\}",
            applicationSource);
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
