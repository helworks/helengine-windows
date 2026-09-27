using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the opt-in DPI-awareness configuration of the Windows player: the enum lives in its own header,
/// <c>Win32DpiAwarenessNames</c> is the single conversion point for its accepted text, <c>Win32DpiAwarenessSettings</c>
/// resolves the command line over the profile like <c>Win32WindowModeSettings</c> does (command line wins, then the
/// profile value if present, then the default of Unaware), and the native build compiles the new sources.
/// </summary>
public sealed class Win32DpiAwarenessSettingsSourceTests {
    /// <summary>
    /// Verifies the DPI-awareness enum is declared as a small <c>enum class</c> in its own header, with the two
    /// documented values, following the repository's one-type-per-file rule.
    /// </summary>
    [Fact]
    public void Win32DpiAwareness_enum_is_declared_in_its_own_header() {
        string dpiAwarenessHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_dpi_awareness.hpp");

        Assert.Contains("enum class Win32DpiAwareness {", dpiAwarenessHeader, StringComparison.Ordinal);
        Assert.Contains("Unaware", dpiAwarenessHeader, StringComparison.Ordinal);
        Assert.Contains("PerMonitorV2", dpiAwarenessHeader, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(dpiAwarenessHeader, "enum class"));
    }

    /// <summary>
    /// Verifies <c>Win32DpiAwarenessNames</c> is the single place that knows the exact, case-sensitive accepted text
    /// for dpiAwareness ("unaware" / "permonitorv2") and how each enum value renders back to text, and that
    /// TryParse leaves the out-parameter unchanged on failure (by never assigning outside the two matched branches).
    /// </summary>
    [Fact]
    public void Win32DpiAwarenessNames_is_the_single_conversion_point() {
        string namesHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_dpi_awareness_names.hpp");
        string namesSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_dpi_awareness_names.cpp");

        Assert.Contains("static bool TryParse(const std::string& text, Win32DpiAwareness& dpiAwareness);", namesHeader, StringComparison.Ordinal);
        Assert.Contains("static const char* ToText(Win32DpiAwareness dpiAwareness);", namesHeader, StringComparison.Ordinal);

        Assert.Contains("text == \"unaware\"", namesSource, StringComparison.Ordinal);
        Assert.Contains("text == \"permonitorv2\"", namesSource, StringComparison.Ordinal);
        Assert.Contains("return false;", namesSource, StringComparison.Ordinal);

        Assert.Contains("case Win32DpiAwareness::Unaware:", namesSource, StringComparison.Ordinal);
        Assert.Contains("return \"unaware\";", namesSource, StringComparison.Ordinal);
        Assert.Contains("case Win32DpiAwareness::PerMonitorV2:", namesSource, StringComparison.Ordinal);
        Assert.Contains("return \"permonitorv2\";", namesSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the settings value type validates its source like <c>Win32WindowModeSettings</c>, lets the command
    /// line override the profile (falling back to the shared <c>Win32DpiAwarenessNames</c> conversion, not a
    /// private duplicate parser, when no override was supplied), defaults to Unaware when neither was supplied, and
    /// describes the effective configuration through the same shared conversion.
    /// </summary>
    [Fact]
    public void Win32DpiAwarenessSettings_validates_resolves_and_describes() {
        string settingsSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_dpi_awareness_settings.cpp");
        string settingsHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_dpi_awareness_settings.hpp");

        Assert.Contains(
            "static Win32DpiAwarenessSettings Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& commandLineOptions);",
            settingsHeader, StringComparison.Ordinal);
        Assert.Contains("Win32DpiAwareness GetDpiAwareness() const;", settingsHeader, StringComparison.Ordinal);
        Assert.Contains("bool IsPerMonitorV2() const;", settingsHeader, StringComparison.Ordinal);
        Assert.Contains("std::string Describe() const;", settingsHeader, StringComparison.Ordinal);

        Assert.Contains("std::invalid_argument", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"dpiAwareness=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\" source=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"profile\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"commandLine\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"default\"", settingsSource, StringComparison.Ordinal);

        Assert.Contains("Win32DpiAwarenessNames::TryParse(profile.DpiAwareness, dpiAwareness)", settingsSource, StringComparison.Ordinal);
        Assert.Contains("Win32DpiAwarenessNames::ToText(DpiAwareness)", settingsSource, StringComparison.Ordinal);

        // The settings class must not keep its own duplicate literal-table parser now that Win32DpiAwarenessNames
        // is the single conversion point.
        Assert.DoesNotContain("== Win32DpiAwareness::PerMonitorV2 ? \"permonitorv2\"", settingsSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies Resolve's precedence directly from source: --dpi-awareness overrides the profile when supplied,
    /// the profile value is used (and validated) when the command line did not supply one, and the default
    /// Win32DpiAwareness::Unaware is used as the initial value before either source is applied, so a profile
    /// without dpiAwareness and no command-line flag resolves to Unaware.
    /// </summary>
    [Fact]
    public void Win32DpiAwarenessSettings_Resolve_precedence_is_command_line_then_profile_then_default() {
        string settingsSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_dpi_awareness_settings.cpp");

        int resolveIndex = settingsSource.IndexOf("Win32DpiAwarenessSettings::Resolve(", StringComparison.Ordinal);
        Assert.True(resolveIndex >= 0, "Resolve must be defined in the settings source.");
        int resolveEndIndex = settingsSource.IndexOf("\n    }", resolveIndex, StringComparison.Ordinal);
        string resolveBody = settingsSource.Substring(resolveIndex, resolveEndIndex - resolveIndex);

        Assert.Contains("Win32DpiAwareness dpiAwareness = Win32DpiAwareness::Unaware;", resolveBody, StringComparison.Ordinal);
        Assert.Contains("commandLineOptions.HasDpiAwareness()", resolveBody, StringComparison.Ordinal);
        Assert.Contains("commandLineOptions.GetDpiAwareness()", resolveBody, StringComparison.Ordinal);
        Assert.Contains("Win32DpiAwarenessNames::TryParse(profile.DpiAwareness, dpiAwareness)", resolveBody, StringComparison.Ordinal);
        Assert.Contains("profile.DpiAwarenessFieldPresent", resolveBody, StringComparison.Ordinal);
        Assert.Contains("std::string source = \"default\";", resolveBody, StringComparison.Ordinal);

        int commandLineCheckIndex = resolveBody.IndexOf("commandLineOptions.HasDpiAwareness()", StringComparison.Ordinal);
        int profileFallbackIndex = resolveBody.IndexOf("Win32DpiAwarenessNames::TryParse(profile.DpiAwareness, dpiAwareness)", StringComparison.Ordinal);
        Assert.True(commandLineCheckIndex >= 0 && profileFallbackIndex > commandLineCheckIndex, "The command-line check must precede the profile fallback, so the command line wins.");
    }

    /// <summary>
    /// Verifies the native build compiles the new DPI-awareness names and settings sources.
    /// </summary>
    [Fact]
    public void CMakeLists_compiles_dpi_awareness_sources() {
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("src/platform/windows/win32/win32_dpi_awareness_names.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("src/platform/windows/win32/win32_dpi_awareness_settings.cpp", cmakeSource, StringComparison.Ordinal);
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
