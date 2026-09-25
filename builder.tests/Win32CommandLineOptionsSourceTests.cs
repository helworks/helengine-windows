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
    /// Verifies the presented-frame counter and the idle and active frame counters are only advanced when <c>--frames</c>
    /// was supplied, so a no-argument run never touches them (and cannot overflow them after a long uptime).
    /// </summary>
    [Fact]
    public void Win32Application_counts_frames_only_when_frame_limit_supplied() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        Assert.Single(Regex.Matches(applicationSource, @"RenderedFrameCount\+\+"));
        Assert.Single(Regex.Matches(applicationSource, @"IdleFrameCount\+\+"));
        Assert.Single(Regex.Matches(applicationSource, @"ActiveFrameCount\+\+"));
        Assert.Matches(
            new Regex(@"if \(CommandLineOptions\.HasFrameLimit\(\)\) \{\s*if \(HostFingerprint->RecordPresent\(presentResult\)\) \{[^}]*\}\s*RenderedFrameCount\+\+;\s*if \(CurrentFrameIsIdle\) \{\s*IdleFrameCount\+\+;\s*\} else \{\s*ActiveFrameCount\+\+;\s*\}\s*if \(RenderedFrameCount >= CommandLineOptions\.GetFrameLimit\(\)\) \{[^}]*PostQuitMessage\(0\);\s*\}\s*\}"),
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
    /// Verifies the parser recognizes the three idle-throttle flags as known flags, wires them into explicit
    /// <c>else if</c> branches placed before the trailing <c>--capture</c> branch, and enforces their value rules
    /// (<c>on</c>/<c>off</c> for <c>--idle-throttle</c>, a positive whole number for <c>--idle-after-ms</c>, and a
    /// 1-30 whole number for <c>--idle-fps</c>).
    /// </summary>
    [Fact]
    public void Win32CommandLineOptions_parses_idle_throttle_flags() {
        string parserSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_command_line_options.cpp");
        string parserHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_command_line_options.hpp");

        Assert.Contains("\"--idle-throttle\"", parserSource, StringComparison.Ordinal);
        Assert.Contains("\"--idle-after-ms\"", parserSource, StringComparison.Ordinal);
        Assert.Contains("\"--idle-fps\"", parserSource, StringComparison.Ordinal);

        int isKnownFlagIndex = parserSource.IndexOf("bool Win32CommandLineOptions::IsKnownFlag", StringComparison.Ordinal);
        Assert.True(isKnownFlagIndex >= 0, "IsKnownFlag definition was not found.");
        int isKnownFlagEndIndex = parserSource.IndexOf("\n    }", isKnownFlagIndex, StringComparison.Ordinal);
        string isKnownFlagBody = parserSource.Substring(isKnownFlagIndex, isKnownFlagEndIndex - isKnownFlagIndex);
        Assert.Contains("argument == L\"--idle-throttle\"", isKnownFlagBody, StringComparison.Ordinal);
        Assert.Contains("argument == L\"--idle-after-ms\"", isKnownFlagBody, StringComparison.Ordinal);
        Assert.Contains("argument == L\"--idle-fps\"", isKnownFlagBody, StringComparison.Ordinal);

        int idleThrottleBranchIndex = parserSource.IndexOf("flag == L\"--idle-throttle\"", StringComparison.Ordinal);
        int idleAfterMsBranchIndex = parserSource.IndexOf("flag == L\"--idle-after-ms\"", StringComparison.Ordinal);
        int idleFpsBranchIndex = parserSource.IndexOf("flag == L\"--idle-fps\"", StringComparison.Ordinal);
        int captureBranchIndex = parserSource.IndexOf("Command-line flag --capture was given more than once.", StringComparison.Ordinal);
        Assert.True(idleThrottleBranchIndex >= 0, "The --idle-throttle branch was not found.");
        Assert.True(idleAfterMsBranchIndex >= 0, "The --idle-after-ms branch was not found.");
        Assert.True(idleFpsBranchIndex >= 0, "The --idle-fps branch was not found.");
        Assert.True(captureBranchIndex >= 0, "The --capture branch was not found.");
        Assert.True(idleThrottleBranchIndex < captureBranchIndex, "The --idle-throttle branch must precede the --capture branch.");
        Assert.True(idleAfterMsBranchIndex < captureBranchIndex, "The --idle-after-ms branch must precede the --capture branch.");
        Assert.True(idleFpsBranchIndex < captureBranchIndex, "The --idle-fps branch must precede the --capture branch.");

        Assert.Contains("L\"on\"", parserSource, StringComparison.Ordinal);
        Assert.Contains("L\"off\"", parserSource, StringComparison.Ordinal);

        Assert.Contains("bool HasIdleThrottle() const;", parserHeader, StringComparison.Ordinal);
        Assert.Contains("bool GetIdleThrottleEnabled() const;", parserHeader, StringComparison.Ordinal);
        Assert.Contains("bool HasIdleAfterMilliseconds() const;", parserHeader, StringComparison.Ordinal);
        Assert.Contains("int GetIdleAfterMilliseconds() const;", parserHeader, StringComparison.Ordinal);
        Assert.Contains("bool HasIdleFramesPerSecond() const;", parserHeader, StringComparison.Ordinal);
        Assert.Contains("int GetIdleFramesPerSecond() const;", parserHeader, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies --frames, --idle-after-ms and --idle-fps share a single bounded-integer parsing helper
    /// (<c>ParseBoundedInteger</c>) instead of each duplicating the empty/whitespace check, the <c>wcstol</c> call and
    /// the full-consumption/overflow checks, while each flag keeps its own bounds and its own byte-identical error
    /// message text.
    /// </summary>
    [Fact]
    public void Win32CommandLineOptions_routes_bounded_integer_flags_through_shared_parser() {
        string parserSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_command_line_options.cpp");
        string parserHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_command_line_options.hpp");

        Assert.Contains(
            "static int ParseBoundedInteger(const std::wstring& value, int minimumValue, int maximumValue, const std::string& invalidValueMessagePrefix);",
            parserHeader, StringComparison.Ordinal);

        int boundedIntegerIndex = parserSource.IndexOf(
            "Win32CommandLineOptions::ParseBoundedInteger(const std::wstring& value, int minimumValue, int maximumValue, const std::string& invalidValueMessagePrefix)",
            StringComparison.Ordinal);
        Assert.True(boundedIntegerIndex >= 0, "ParseBoundedInteger definition was not found.");
        string boundedIntegerBody = parserSource.Substring(boundedIntegerIndex, parserSource.IndexOf("\n    }", boundedIntegerIndex, StringComparison.Ordinal) - boundedIntegerIndex);
        Assert.Contains("std::wcstol(value.c_str(), &end, 10)", boundedIntegerBody, StringComparison.Ordinal);
        Assert.Contains("parsed < minimumValue", boundedIntegerBody, StringComparison.Ordinal);
        Assert.Contains("parsed > maximumValue", boundedIntegerBody, StringComparison.Ordinal);

        // The three flags call the shared parser with their own bounds and their own, byte-identical message text.
        int frameLimitIndex = parserSource.IndexOf("Win32CommandLineOptions::ParseFrameLimit", StringComparison.Ordinal);
        Assert.True(frameLimitIndex >= 0, "ParseFrameLimit definition was not found.");
        string frameLimitBody = parserSource.Substring(frameLimitIndex, parserSource.IndexOf("\n    }", frameLimitIndex, StringComparison.Ordinal) - frameLimitIndex);
        Assert.Contains(
            "ParseBoundedInteger(value, 1, INT_MAX, \"Command-line flag --frames requires a whole number of at least 1, got: \")",
            frameLimitBody, StringComparison.Ordinal);

        int idleAfterMsIndex = parserSource.IndexOf("Win32CommandLineOptions::ParseIdleAfterMilliseconds", StringComparison.Ordinal);
        Assert.True(idleAfterMsIndex >= 0, "ParseIdleAfterMilliseconds definition was not found.");
        string idleAfterMsBody = parserSource.Substring(idleAfterMsIndex, parserSource.IndexOf("\n    }", idleAfterMsIndex, StringComparison.Ordinal) - idleAfterMsIndex);
        Assert.Contains(
            "ParseBoundedInteger(value, 1, INT_MAX, \"Command-line flag --idle-after-ms requires a whole number of at least 1, got: \")",
            idleAfterMsBody, StringComparison.Ordinal);

        int idleFpsIndex = parserSource.IndexOf("Win32CommandLineOptions::ParseIdleFramesPerSecond", StringComparison.Ordinal);
        Assert.True(idleFpsIndex >= 0, "ParseIdleFramesPerSecond definition was not found.");
        string idleFpsBody = parserSource.Substring(idleFpsIndex, parserSource.IndexOf("\n    }", idleFpsIndex, StringComparison.Ordinal) - idleFpsIndex);
        Assert.Contains(
            "ParseBoundedInteger(value, 1, 30, \"Command-line flag --idle-fps requires a whole number from 1 to 30, got: \")",
            idleFpsBody, StringComparison.Ordinal);

        // Only ParseBoundedInteger itself should still call wcstol: the three flags must not keep their own copy.
        Assert.Single(Regex.Matches(parserSource, @"std::wcstol\(value\.c_str\(\), &end, 10\)"));
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
