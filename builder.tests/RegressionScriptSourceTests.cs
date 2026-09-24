namespace helengine.windows.builder.tests;

/// <summary>
/// Guards the regression script's build stage so it always builds this checkout's player through the canonical helengine build script, in an isolated copy of the project source's committed HEAD.
/// </summary>
public sealed class RegressionScriptSourceTests {
    /// <summary>
    /// Ensures the regression script extracts the project source's committed HEAD, isolates the engine user settings, aligns the copied project's engine version, builds this checkout's builder, and reports which project commit and player source root it built.
    /// </summary>
    [Fact]
    public void RegressionScript_BuildsThisCheckoutThroughIsolatedEngineUserSettings() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("HELENGINE_ENGINE_USER_SETTINGS_ROOT", scriptSource, StringComparison.Ordinal);
        Assert.Contains("requiredEngineVersion", scriptSource, StringComparison.Ordinal);
        Assert.Contains("build-platform.ps1", scriptSource, StringComparison.Ordinal);
        Assert.Contains("-p:HelEngineRoot=", scriptSource, StringComparison.Ordinal);
        Assert.Contains("default-width", scriptSource, StringComparison.Ordinal);
        Assert.Contains("PLAYER_SOURCE_ROOT=", scriptSource, StringComparison.Ordinal);
        Assert.Contains("[switch]$BuildOnly", scriptSource, StringComparison.Ordinal);
        Assert.Contains("ProjectSource", scriptSource, StringComparison.Ordinal);
        Assert.Contains("archive", scriptSource, StringComparison.Ordinal);
        Assert.Contains("PROJECT_COMMIT=", scriptSource, StringComparison.Ordinal);
        Assert.Contains("scenes/rendering/", scriptSource, StringComparison.Ordinal);
        Assert.DoesNotContain("DemoDiscMainMenu.helen", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the regression script's record and verify stages launch every scene through the canonical launcher with the fixed frame settings, and drive the regression tool's golden, stability and test-failure commands.
    /// </summary>
    [Fact]
    public void RegressionScript_RecordsAndVerifiesScenesAndTestFailureBaselines() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("launch_in_emulator.ps1", scriptSource, StringComparison.Ordinal);
        Assert.Contains("-Wait", scriptSource, StringComparison.Ordinal);
        Assert.Contains("--fixed-delta", scriptSource, StringComparison.Ordinal);
        Assert.Contains("0.016666", scriptSource, StringComparison.Ordinal);
        Assert.Contains("--frames", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'30'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("record-golden", scriptSource, StringComparison.Ordinal);
        Assert.Contains("compare-failing", scriptSource, StringComparison.Ordinal);
        Assert.Contains("trx-failing", scriptSource, StringComparison.Ordinal);
        Assert.Contains("stable", scriptSource, StringComparison.Ordinal);
        Assert.Contains("manifest.json", scriptSource, StringComparison.Ordinal);
        Assert.Contains("profile.json", scriptSource, StringComparison.Ordinal);
        Assert.Contains("RESULT:", scriptSource, StringComparison.Ordinal);
        Assert.Contains("helengine.editor.tests.csproj", scriptSource, StringComparison.Ordinal);
        Assert.Contains("helengine.render.validation.tests.csproj", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the regression script records which project and helengine revisions its goldens came from, and warns on verify when either has moved since the record.
    /// </summary>
    [Fact]
    public void RegressionScript_RecordsProvenanceAndWarnsWhenItChanges() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("projectCommit", scriptSource, StringComparison.Ordinal);
        Assert.Contains("helengineCommit", scriptSource, StringComparison.Ordinal);
        Assert.Contains("helengineDirty", scriptSource, StringComparison.Ordinal);
        Assert.Contains("status --porcelain", scriptSource, StringComparison.Ordinal);
        Assert.Contains("WARN project changed since record", scriptSource, StringComparison.Ordinal);
        Assert.Contains("WARN helengine changed since record", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the regression script refuses a work root that overlaps the project source, the helengine checkout or this checkout, because the build stage deletes and rewrites folders under the work root.
    /// </summary>
    [Fact]
    public void RegressionScript_RejectsWorkRootOverlappingSourceTrees() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("Assert-WorkRootIsolated", scriptSource, StringComparison.Ordinal);
        Assert.Contains("OrdinalIgnoreCase", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the regression README documents how to run the net and how to re-record goldens deliberately.
    /// </summary>
    [Fact]
    public void RegressionReadme_DocumentsRunningAndReRecording() {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string readmePath = Path.Combine(repositoryRootPath, "regression", "README.md");

        Assert.True(File.Exists(readmePath), "Expected regression/README.md to exist.");

        string readmeSource = File.ReadAllText(readmePath);

        Assert.Contains("run-regression.ps1 -Verify", readmeSource, StringComparison.Ordinal);
        Assert.Contains("-Record", readmeSource, StringComparison.Ordinal);
        Assert.Contains("WARN", readmeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the regression script never bypasses the canonical build script by invoking the native or managed toolchains directly.
    /// </summary>
    [Fact]
    public void RegressionScript_DoesNotInvokeToolchainsDirectly() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.DoesNotContain("cmake", scriptSource, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("dotnet publish", scriptSource, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// Reads the regression script from the repository's scripts folder, failing the test when the script is absent.
    /// </summary>
    /// <returns>The full text of scripts/run-regression.ps1.</returns>
    static string ReadRegressionScriptSource() {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string scriptPath = Path.Combine(repositoryRootPath, "scripts", "run-regression.ps1");

        Assert.True(File.Exists(scriptPath), "Expected scripts/run-regression.ps1 to exist.");

        return File.ReadAllText(scriptPath);
    }
}
