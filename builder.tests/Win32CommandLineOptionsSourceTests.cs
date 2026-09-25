using System.Text.RegularExpressions;

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
    /// Verifies strict validation only applies once a known flag is present: a command line with arguments but no known
    /// flag (for example a file path that <c>CommandLineToArgvW</c> split into pieces) is kept as ignored text instead of
    /// being rejected, so existing launches that pass arguments keep working.
    /// </summary>
    [Fact]
    public void Win32CommandLineOptions_ignores_arguments_when_no_known_flag_is_present() {
        string parserSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_command_line_options.cpp");
        string parserHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_command_line_options.hpp");

        Assert.Contains("bool HasIgnoredArguments() const;", parserHeader, StringComparison.Ordinal);
        Assert.Contains("const std::string& GetIgnoredArguments() const;", parserHeader, StringComparison.Ordinal);
        Assert.Contains("static bool IsKnownFlag(const std::wstring& argument);", parserHeader, StringComparison.Ordinal);

        int knownFlagScanIndex = parserSource.IndexOf("IsKnownFlag(arguments[scanIndex])", StringComparison.Ordinal);
        int strictUnknownIndex = parserSource.IndexOf("Unknown command-line argument: ", StringComparison.Ordinal);
        Assert.True(knownFlagScanIndex >= 0, "Parse must scan for a known flag before validating.");
        Assert.True(knownFlagScanIndex < strictUnknownIndex, "The known-flag scan must run before the strict unknown-argument rejection.");
        Assert.Matches(
            new Regex(@"if \(!knownFlagPresent\) \{.*?options\.IgnoredArguments \+= ConvertToUtf8\(arguments\[ignoredIndex\]\);.*?options\.ArgumentsIgnored = argumentCount > 1;\s*return options;\s*\}", RegexOptions.Singleline),
            parserSource);
    }

    /// <summary>
    /// Verifies the application logs ignored arguments once, only when there are any, and then continues startup exactly
    /// as before.
    /// </summary>
    [Fact]
    public void Win32Application_logs_ignored_arguments_only_when_present() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        Assert.Single(Regex.Matches(applicationSource, "Ignoring command-line arguments: "));
        Assert.Matches(
            new Regex(@"if \(CommandLineOptions\.HasIgnoredArguments\(\)\) \{\s*std::string ignoredArgumentsMessage = ""Ignoring command-line arguments: "" \+ CommandLineOptions\.GetIgnoredArguments\(\);\s*WriteLifecycleLog\(ignoredArgumentsMessage\.c_str\(\)\);\s*\}"),
            applicationSource);
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
    /// Verifies the presented-frame counter is only advanced when <c>--frames</c> was supplied, so a no-argument run never
    /// touches it (and cannot overflow it after a long uptime).
    /// </summary>
    [Fact]
    public void Win32Application_counts_frames_only_when_frame_limit_supplied() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        Assert.Single(Regex.Matches(applicationSource, @"RenderedFrameCount\+\+"));
        Assert.Matches(
            new Regex(@"if \(CommandLineOptions\.HasFrameLimit\(\)\) \{\s*if \(HostFingerprint->RecordPresent\(presentResult\)\) \{[^}]*\}\s*RenderedFrameCount\+\+;\s*if \(RenderedFrameCount >= CommandLineOptions\.GetFrameLimit\(\)\) \{[^}]*PostQuitMessage\(0\);\s*\}\s*\}"),
            applicationSource);
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
