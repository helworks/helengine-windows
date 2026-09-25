using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the opt-in window-mode settings of the Windows player: the three window-mode enums each live in their
/// own header, <c>Win32WindowModeSettings</c> resolves the command line over the profile like
/// <c>Win32IdleThrottleSettings</c> does, the hit-test-probe combination is validated only when settings resolve,
/// and <c>CreateMainWindow</c> resolves the settings next to the idle-throttle settings so an invalid configuration
/// already exits the process with code 2.
/// </summary>
public sealed class Win32WindowModeSettingsSourceTests {
    /// <summary>
    /// Verifies each window-mode enum is declared as a small <c>enum class</c> in its own header, with the two
    /// documented values, following the repository's one-type-per-file rule.
    /// </summary>
    [Fact]
    public void Window_mode_enums_are_each_declared_in_their_own_header() {
        string windowModeHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window_mode.hpp");
        string overlayBoundsHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_overlay_bounds.hpp");
        string overlayBackgroundHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_overlay_background.hpp");

        Assert.Contains("enum class Win32WindowMode {", windowModeHeader, StringComparison.Ordinal);
        Assert.Contains("Normal", windowModeHeader, StringComparison.Ordinal);
        Assert.Contains("Overlay", windowModeHeader, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32OverlayBounds", windowModeHeader, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32OverlayBackground", windowModeHeader, StringComparison.Ordinal);

        Assert.Contains("enum class Win32OverlayBounds {", overlayBoundsHeader, StringComparison.Ordinal);
        Assert.Contains("Monitor", overlayBoundsHeader, StringComparison.Ordinal);
        Assert.Contains("Profile", overlayBoundsHeader, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32WindowMode", overlayBoundsHeader, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32OverlayBackground", overlayBoundsHeader, StringComparison.Ordinal);

        Assert.Contains("enum class Win32OverlayBackground {", overlayBackgroundHeader, StringComparison.Ordinal);
        Assert.Contains("Camera", overlayBackgroundHeader, StringComparison.Ordinal);
        Assert.Contains("Transparent", overlayBackgroundHeader, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32WindowMode", overlayBackgroundHeader, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32OverlayBounds", overlayBackgroundHeader, StringComparison.Ordinal);

        Assert.Single(Regex.Matches(windowModeHeader, "enum class"));
        Assert.Single(Regex.Matches(overlayBoundsHeader, "enum class"));
        Assert.Single(Regex.Matches(overlayBackgroundHeader, "enum class"));
    }

    /// <summary>
    /// Verifies the settings value type validates its source like <c>Win32IdleThrottleSettings</c>, lets the
    /// command line override the profile field by field, and describes the effective configuration with its
    /// source.
    /// </summary>
    [Fact]
    public void Win32WindowModeSettings_validates_resolves_and_describes() {
        string settingsSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window_mode_settings.cpp");
        string settingsHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window_mode_settings.hpp");

        Assert.Contains(
            "static Win32WindowModeSettings Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& options);",
            settingsHeader, StringComparison.Ordinal);
        Assert.Contains("std::string Describe() const;", settingsHeader, StringComparison.Ordinal);
        Assert.Contains("std::invalid_argument", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"windowMode=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\" bounds=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\" background=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\" source=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"profile\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"commandLine\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"mixed\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains(
            "options.HasWindowMode() ? options.GetWindowMode() : ParseProfileWindowMode(profile.WindowMode)",
            settingsSource, StringComparison.Ordinal);
        Assert.Contains(
            "options.HasOverlayBounds() ? options.GetOverlayBounds() : ParseProfileOverlayBounds(profile.OverlayBounds)",
            settingsSource, StringComparison.Ordinal);
        Assert.Contains(
            "options.HasOverlayBackground() ? options.GetOverlayBackground() : ParseProfileOverlayBackground(profile.OverlayBackground)",
            settingsSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies Resolve rejects --hit-test-probe unless both --frames and an effective overlay window mode are also
    /// present, since the combination depends on the profile (the window mode may come from profile.json alone) and
    /// therefore cannot be checked while parsing the command line in isolation.
    /// </summary>
    [Fact]
    public void Win32WindowModeSettings_Resolve_requires_frames_and_overlay_mode_for_the_hit_test_probe() {
        string settingsSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window_mode_settings.cpp");

        int resolveIndex = settingsSource.IndexOf("Win32WindowModeSettings::Resolve(", StringComparison.Ordinal);
        Assert.True(resolveIndex >= 0, "Resolve must be defined in the settings source.");
        int resolveEndIndex = settingsSource.IndexOf("\n    }", resolveIndex, StringComparison.Ordinal);
        string resolveBody = settingsSource.Substring(resolveIndex, resolveEndIndex - resolveIndex);

        Assert.Contains("options.HasHitTestProbe()", resolveBody, StringComparison.Ordinal);
        Assert.Contains("!options.HasFrameLimit()", resolveBody, StringComparison.Ordinal);
        Assert.Contains("windowMode != Win32WindowMode::Overlay", resolveBody, StringComparison.Ordinal);
        Assert.Contains("throw std::invalid_argument(", resolveBody, StringComparison.Ordinal);

        string parserSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_command_line_options.cpp");
        Assert.DoesNotContain("--hit-test-probe requires --frames", parserSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies CreateMainWindow resolves the window-mode settings right after the idle-throttle settings (before
    /// the window is created), and logs the effective configuration only in overlay mode, keeping normal-mode
    /// startup output byte-identical.
    /// </summary>
    [Fact]
    public void Win32Application_resolves_window_mode_settings_next_to_idle_throttle_settings() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        string createMainWindowBody = ExtractMethodBody(applicationSource, "Win32Application::CreateMainWindow(");
        Assert.Matches(
            new Regex(
                @"RuntimePlayerProfile profile = ResolveRuntimePlayerProfile\(\);\s*"
                + @"Win32WindowModeSettings windowModeSettings = ResolveWindowModeSettings\(profile\);\s*"
                + @"Win32IdleThrottleSettings idleThrottleSettings = Win32IdleThrottleSettings::Resolve\(profile, CommandLineOptions\);\s*"
                + @"MainWindow = std::make_unique<Win32Window>\(",
                RegexOptions.Singleline),
            createMainWindowBody);

        int windowModeIndex = createMainWindowBody.IndexOf("Win32WindowModeSettings windowModeSettings", StringComparison.Ordinal);
        int createIndex = createMainWindowBody.IndexOf("MainWindow->Create();", StringComparison.Ordinal);
        Assert.True(windowModeIndex >= 0 && createIndex > windowModeIndex, "Window-mode settings must resolve before the window is created.");

        Assert.Matches(
            new Regex(
                @"if \(windowModeSettings\.GetWindowMode\(\) == Win32WindowMode::Overlay\) \{\s*"
                + @"std::string windowModeMessage = ""Window mode configured: "" \+ windowModeSettings\.Describe\(\);\s*"
                + @"WriteLifecycleLog\(windowModeMessage\.c_str\(\)\);\s*"
                + @"\}",
                RegexOptions.Singleline),
            createMainWindowBody);
        Assert.Single(Regex.Matches(applicationSource, @"windowModeSettings\.Describe\(\)"));
    }

    /// <summary>
    /// Verifies Win32Application::ResolveWindowModeSettings catches std::invalid_argument and converts it into a
    /// Win32ExitRequest with exit code 2, matching how ResolveRuntimePlayerProfile converts
    /// RuntimePlayerProfileConfigurationError. The catch must not log the message itself: Run()'s Win32ExitRequest
    /// handler already logs it once.
    /// </summary>
    [Fact]
    public void Win32Application_converts_window_mode_configuration_error_into_exit_code_two() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        int methodStartIndex = applicationSource.IndexOf(
            "Win32Application::ResolveWindowModeSettings(",
            StringComparison.Ordinal);
        Assert.True(methodStartIndex >= 0, "ResolveWindowModeSettings must be defined in win32_application.cpp.");

        int catchIndex = applicationSource.IndexOf(
            "catch (const std::invalid_argument& configurationError)",
            methodStartIndex,
            StringComparison.Ordinal);
        Assert.True(catchIndex >= 0, "ResolveWindowModeSettings must catch std::invalid_argument.");

        int exitRequestIndex = applicationSource.IndexOf("Win32ExitRequest(2,", catchIndex, StringComparison.Ordinal);
        Assert.True(exitRequestIndex > catchIndex, "The configuration-error catch must throw Win32ExitRequest(2, ...).");

        string catchToThrowText = applicationSource.Substring(catchIndex, exitRequestIndex - catchIndex);
        Assert.DoesNotContain("WriteLifecycleLog", catchToThrowText, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the native build compiles the new window-mode settings source file.
    /// </summary>
    [Fact]
    public void CMakeLists_compiles_window_mode_settings_source() {
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("src/platform/windows/win32/win32_window_mode_settings.cpp", cmakeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Returns the text of one C++ method definition from its qualified name up to the closing brace that ends a
    /// namespace-level member definition.
    /// </summary>
    /// <param name="source">Full C++ source text; CRLF line endings are normalized to LF first.</param>
    /// <param name="qualifiedNamePrefix">Qualified method name including the opening parenthesis.</param>
    /// <returns>The method definition text.</returns>
    static string ExtractMethodBody(string source, string qualifiedNamePrefix) {
        string normalizedSource = source.Replace("\r\n", "\n", StringComparison.Ordinal);
        int startIndex = normalizedSource.IndexOf(qualifiedNamePrefix, StringComparison.Ordinal);
        Assert.True(startIndex >= 0, $"Method definition '{qualifiedNamePrefix}' was not found.");
        int endIndex = normalizedSource.IndexOf("\n    }\n", startIndex, StringComparison.Ordinal);
        Assert.True(endIndex >= 0, $"End of method definition '{qualifiedNamePrefix}' was not found.");
        return normalizedSource.Substring(startIndex, endIndex - startIndex);
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
