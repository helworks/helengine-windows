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
    /// Ensures Verify builds the project commit recorded in the manifest instead of the project's HEAD: the pin is read
    /// from the committed manifest before the archive is made, a manifest without projectCommit or a commit the project
    /// source lacks stops the run with a clear message, -ProjectCommit is refused with -Verify, Record and BuildOnly build
    /// -ProjectCommit (default HEAD), and a moved HEAD is only a WARN that says the pinned commit was built.
    /// </summary>
    [Fact]
    public void RegressionScript_PinsVerifyToTheRecordedProjectCommit() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("[string]$ProjectCommit = ''", scriptSource, StringComparison.Ordinal);

        string resolveSource = ReadFunctionSource(scriptSource, "Resolve-ProjectCommit");
        Assert.Contains("cat-file -e \"$CommitText^{commit}\"", resolveSource, StringComparison.Ordinal);
        Assert.Contains("rev-parse --verify \"$CommitText^{commit}\"", resolveSource, StringComparison.Ordinal);
        Assert.Contains("was not found in the project source", resolveSource, StringComparison.Ordinal);

        int pinIndex = scriptSource.IndexOf("$projectCommit = Resolve-ProjectCommit -ProjectSourcePath $ProjectSource -CommitText \"$($pinnedManifest.projectCommit)\" -Purpose 'recorded DemoDisc commit'", StringComparison.Ordinal);
        int archiveIndex = scriptSource.IndexOf("& git -C $ProjectSource archive --format=tar -o $projectArchivePath $projectCommit", StringComparison.Ordinal);
        Assert.True(pinIndex >= 0, "Verify must pin the project to the manifest's projectCommit.");
        Assert.True(archiveIndex > pinIndex, "The pinned commit must be resolved before the project archive is made.");
        int pinBlockIndex = scriptSource.LastIndexOf("if ($Verify) {", pinIndex, StringComparison.Ordinal);
        string pinSource = scriptSource.Substring(pinBlockIndex, archiveIndex - pinBlockIndex);
        Assert.Contains("-ProjectCommit cannot be used with -Verify", pinSource, StringComparison.Ordinal);
        Assert.Contains("The manifest has no projectCommit to pin the project to (re-record required)", pinSource, StringComparison.Ordinal);
        Assert.Contains("the manifest is missing (re-record required)", pinSource, StringComparison.Ordinal);
        Assert.Contains("Resolve-ProjectCommit -ProjectSourcePath $ProjectSource -CommitText $ProjectCommit -Purpose '-ProjectCommit'", pinSource, StringComparison.Ordinal);
        Assert.Contains("$projectCommit = $projectHeadCommit", pinSource, StringComparison.Ordinal);
        Assert.DoesNotContain("$projectCommit = (& git -C $ProjectSource rev-parse HEAD)", scriptSource, StringComparison.Ordinal);

        Assert.Contains("but Verify built the pinned recorded commit", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the regression README documents that Verify is pinned to the recorded project commit and how to move the
    /// pin forward deliberately.
    /// </summary>
    [Fact]
    public void RegressionReadme_DocumentsProjectPinning() {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string readmeSource = File.ReadAllText(Path.Combine(repositoryRootPath, "regression", "README.md"));

        Assert.Contains("## Project pinning", readmeSource, StringComparison.Ordinal);
        Assert.Contains("-Record -ProjectCommit <sha>", readmeSource, StringComparison.Ordinal);
        Assert.Contains("never follows the project's HEAD", readmeSource, StringComparison.Ordinal);
        Assert.Contains("recorded DemoDisc commit", readmeSource, StringComparison.Ordinal);
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
    /// Ensures the manifest also records hashes of every other input that changes the output (the project's source build
    /// config, its generated code tree and helengine's uncommitted work), and that verify warns when any of them changed.
    /// </summary>
    [Fact]
    public void RegressionScript_HashesEveryOutputChangingInput() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("buildConfigSourceHash", scriptSource, StringComparison.Ordinal);
        Assert.Contains("generatedCodeHash", scriptSource, StringComparison.Ordinal);
        Assert.Contains("helengineWorkingTreeHash", scriptSource, StringComparison.Ordinal);
        Assert.Contains("user_settings\\generated_code", scriptSource, StringComparison.Ordinal);
        Assert.Contains("diff HEAD", scriptSource, StringComparison.Ordinal);
        Assert.Contains("ls-files --others --exclude-standard", scriptSource, StringComparison.Ordinal);
        Assert.Contains("SHA256", scriptSource, StringComparison.Ordinal);
        Assert.Contains("changed since record", scriptSource, StringComparison.Ordinal);

        int sourceHashIndex = scriptSource.IndexOf("$buildConfigSourceHash =", StringComparison.Ordinal);
        int userSettingsCopyIndex = scriptSource.IndexOf("& robocopy", StringComparison.Ordinal);
        Assert.True(sourceHashIndex >= 0 && sourceHashIndex < userSettingsCopyIndex, "The source build config must be hashed before the user_settings copy is made and overridden.");
    }

    /// <summary>
    /// Ensures every scene run is bounded: the launcher is called with a 120 second timeout and a timed-out run is a
    /// "FAIL scene &lt;id&gt; timeout" check.
    /// </summary>
    [Fact]
    public void RegressionScript_TimesOutHungPlayers() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("-TimeoutSeconds 120", scriptSource, StringComparison.Ordinal);
        Assert.Contains("^EXIT_CODE=timeout$", scriptSource, StringComparison.Ordinal);
        Assert.Contains("Add-CheckResult -Status FAIL -Kind scene -Name $SceneId -Detail 'timeout'", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the script records each run's HOST_FINGERPRINT line into the manifest and has the regression tool check it
    /// at record time and compare it at verify time.
    /// </summary>
    [Fact]
    public void RegressionScript_RecordsAndComparesHostFingerprints() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("HOST_FINGERPRINT", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'check-fingerprint'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'compare-fingerprint'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("fingerprints = (ConvertTo-ManifestFingerprints -FingerprintsByWindow $runA.FingerprintsByWindow)", scriptSource, StringComparison.Ordinal);
        Assert.DoesNotContain("fingerprint = $fingerprintFields", scriptSource, StringComparison.Ordinal);
        Assert.DoesNotContain("elapsedMs = $sceneElapsedMilliseconds", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures a scene run collects every HOST_FINGERPRINT line of its startup log into an ordered map keyed by the
    /// line's window tag, and that a run with no fingerprint line, a line without a window tag or two lines for the same
    /// window is a FAIL rather than a silently dropped fingerprint.
    /// </summary>
    [Fact]
    public void RegressionScript_CollectsEveryFingerprintLineByWindow() {
        string scriptSource = ReadRegressionScriptSource();

        string playerSceneSource = ReadFunctionSource(scriptSource, "Invoke-PlayerScene");
        Assert.Contains("$fingerprintLines.Add($Matches[1])", playerSceneSource, StringComparison.Ordinal);
        Assert.Contains("Get-FingerprintsByWindow -FingerprintLines $fingerprintLines.ToArray()", playerSceneSource, StringComparison.Ordinal);
        Assert.Contains("FingerprintsByWindow = $fingerprintCollection.FingerprintsByWindow", playerSceneSource, StringComparison.Ordinal);
        Assert.Contains("FingerprintProblem = $fingerprintCollection.Problem", playerSceneSource, StringComparison.Ordinal);
        Assert.DoesNotContain("$fingerprintLine = $Matches[1]", playerSceneSource, StringComparison.Ordinal);

        string collectionSource = ReadFunctionSource(scriptSource, "Get-FingerprintsByWindow");
        Assert.Contains("New-Object System.Collections.Specialized.OrderedDictionary", collectionSource, StringComparison.Ordinal);
        Assert.Contains("'(?:^| )window=(\\S+)(?: |$)'", collectionSource, StringComparison.Ordinal);
        Assert.Contains("missing: the startup log has no HOST_FINGERPRINT line", collectionSource, StringComparison.Ordinal);
        Assert.Contains("duplicate window", collectionSource, StringComparison.Ordinal);
        Assert.Contains("has no window field", collectionSource, StringComparison.Ordinal);

        string runSucceededSource = ReadFunctionSource(scriptSource, "Test-PlayerRunSucceeded");
        Assert.Contains("$null -ne $PlayerRun.FingerprintProblem", runSucceededSource, StringComparison.Ordinal);
        Assert.Contains("-Kind fingerprint -Name $SceneId -Detail $PlayerRun.FingerprintProblem", runSucceededSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures fingerprints are stored and compared per window: Record checks run a's windows, compares run b's windows
    /// with run a's and stores a <c>fingerprints</c> map (window tag to fields, each window with its own elapsedMs);
    /// Verify fails a manifest entry in the old single-<c>fingerprint</c> shape with a re-record request, fails a missing
    /// or extra window by name and compares the fields of every window through compare-fingerprint.
    /// </summary>
    [Fact]
    public void RegressionScript_StoresAndComparesFingerprintsPerWindow() {
        string scriptSource = ReadRegressionScriptSource();

        string compareSource = ReadFunctionSource(scriptSource, "Compare-FingerprintsByWindow");
        Assert.Contains("window missing", compareSource, StringComparison.Ordinal);
        Assert.Contains("window extra", compareSource, StringComparison.Ordinal);
        Assert.Contains("@('compare-fingerprint', \"$Name@$windowName\"", compareSource, StringComparison.Ordinal);

        string healthSource = ReadFunctionSource(scriptSource, "Invoke-FingerprintHealthCheck");
        Assert.Contains("@('check-fingerprint', \"$Name@$windowName\"", healthSource, StringComparison.Ordinal);

        string manifestSource = ReadFunctionSource(scriptSource, "ConvertTo-ManifestFingerprints");
        Assert.Contains("ConvertTo-FingerprintFields -FingerprintLine $FingerprintsByWindow[$windowName]", manifestSource, StringComparison.Ordinal);
        Assert.Contains("[long]$windowFields['elapsedMs']", manifestSource, StringComparison.Ordinal);

        string recordedSource = ReadFunctionSource(scriptSource, "ConvertTo-RecordedFingerprintsByWindow");
        Assert.Contains("$RecordedFingerprints.PSObject.Properties", recordedSource, StringComparison.Ordinal);
        Assert.DoesNotContain("function ConvertTo-RecordedFingerprintLine", scriptSource, StringComparison.Ordinal);

        int recordIndex = scriptSource.IndexOf("# 11R.", StringComparison.Ordinal);
        int verifyIndex = scriptSource.IndexOf("# 11V.", StringComparison.Ordinal);
        string recordSource = scriptSource.Substring(recordIndex, verifyIndex - recordIndex);
        Assert.Contains("Invoke-FingerprintHealthCheck -Name $sceneId -FingerprintsByWindow $runA.FingerprintsByWindow -PassDetail 'run a healthy'", recordSource, StringComparison.Ordinal);
        Assert.Contains("Compare-FingerprintsByWindow -Name $sceneId -RecordedFingerprintsByWindow $runA.FingerprintsByWindow -ActualFingerprintsByWindow $runB.FingerprintsByWindow -PassDetail 'run b matches run a'", recordSource, StringComparison.Ordinal);

        string verifySource = scriptSource.Substring(verifyIndex);
        Assert.Contains("$null -ne $recordedScene.PSObject.Properties['fingerprint'] -or $null -eq $recordedScene.fingerprints", verifySource, StringComparison.Ordinal);
        Assert.Contains("uses the old single 'fingerprint' shape or has no 'fingerprints' (re-record required)", verifySource, StringComparison.Ordinal);
        Assert.Contains("Compare-FingerprintsByWindow -Name $sceneId -RecordedFingerprintsByWindow (ConvertTo-RecordedFingerprintsByWindow -RecordedFingerprints $recordedScene.fingerprints) -ActualFingerprintsByWindow $verifyRun.FingerprintsByWindow -PassDetail 'matches record'", verifySource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the script runs the opt-in idle-throttle scenario on the smoke scene with the player's idle flags, records
    /// it in the manifest as an idle entry, checks it with the regression tool's check-idle command and compares its
    /// capture with the smoke scene's golden, and fails a verify against a manifest that has no idle entry.
    /// </summary>
    [Fact]
    public void RegressionScript_RunsAndChecksTheIdleThrottleScenario() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("[string]$FrameCount = '30'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'--frames', $FrameCount", scriptSource, StringComparison.Ordinal);
        Assert.Contains("-ExtraArguments $script:idlePlayerArguments", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'--idle-throttle', 'on'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'--idle-after-ms', '1'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'--idle-fps', '10'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'check-idle'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'25'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'2400'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("kind = 'idle'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("-eq 'idle'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("idle entry missing from manifest", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures an idle diff image from an earlier run can never be mistaken for this run's: the idle scenario deletes a
    /// stale diff before it runs, and Verify writes the idle diff into the diffs folder it wipes at the start.
    /// </summary>
    [Fact]
    public void RegressionScript_NeverLeavesAStaleIdleDiff() {
        string scriptSource = ReadRegressionScriptSource();

        int functionIndex = scriptSource.IndexOf("function Invoke-IdleScenario {", StringComparison.Ordinal);
        Assert.True(functionIndex >= 0, "The script must define Invoke-IdleScenario.");
        int staleDiffRemovalIndex = scriptSource.IndexOf("Remove-Item -LiteralPath $DiffPath -Force", functionIndex, StringComparison.Ordinal);
        int idleRunIndex = scriptSource.IndexOf("$idleRun = Invoke-PlayerScene", functionIndex, StringComparison.Ordinal);
        Assert.True(staleDiffRemovalIndex > functionIndex, "Invoke-IdleScenario must delete a stale diff image.");
        Assert.True(idleRunIndex > staleDiffRemovalIndex, "Invoke-IdleScenario must delete the stale diff image before the run.");

        int diffsWipeIndex = scriptSource.IndexOf("Remove-Item -LiteralPath $diffsRootPath -Recurse -Force", StringComparison.Ordinal);
        int verifyIdleDiffIndex = scriptSource.IndexOf("$idleDiffPath = Join-Path $diffsRootPath", StringComparison.Ordinal);
        Assert.True(diffsWipeIndex >= 0, "Verify must wipe the diffs folder.");
        Assert.True(verifyIdleDiffIndex > diffsWipeIndex, "Verify must write the idle diff into the diffs folder it wipes.");
    }

    /// <summary>
    /// Ensures Verify checks the idle scenario against the minimums recorded in the manifest's idle entry rather than
    /// the script's current constants, which Record keeps writing into the entry, and fails with a re-record request when
    /// the entry lacks either minimum.
    /// </summary>
    [Fact]
    public void RegressionScript_VerifiesIdleThresholdsFromTheManifest() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("[string]$MinimumIdleFrames", scriptSource, StringComparison.Ordinal);
        Assert.Contains("[string]$MinimumElapsedMilliseconds", scriptSource, StringComparison.Ordinal);
        Assert.Contains("@('check-idle', $FingerprintsByWindow[$windowName], $MinimumIdleFrames, $MinimumElapsedMilliseconds)", scriptSource, StringComparison.Ordinal);
        Assert.DoesNotContain("$script:idleMinimumIdleFrames, $script:idleMinimumElapsedMilliseconds)", scriptSource, StringComparison.Ordinal);

        Assert.Contains("minIdleFrames = [int]$idleMinimumIdleFrames; minElapsedMs = [int]$idleMinimumElapsedMilliseconds", scriptSource, StringComparison.Ordinal);
        Assert.Contains("-MinimumIdleFrames $idleMinimumIdleFrames -MinimumElapsedMilliseconds $idleMinimumElapsedMilliseconds", scriptSource, StringComparison.Ordinal);
        Assert.Contains("$null -eq $recordedIdleScene.minIdleFrames -or $null -eq $recordedIdleScene.minElapsedMs", scriptSource, StringComparison.Ordinal);
        Assert.Contains("idle entry lacks minIdleFrames or minElapsedMs (re-record required)", scriptSource, StringComparison.Ordinal);
        Assert.Contains("-MinimumIdleFrames \"$($recordedIdleScene.minIdleFrames)\" -MinimumElapsedMilliseconds \"$($recordedIdleScene.minElapsedMs)\"", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the regression README documents the idle scenario, the physics caveat and the check-idle command.
    /// </summary>
    [Fact]
    public void RegressionReadme_DocumentsTheIdleScenario() {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string readmeSource = File.ReadAllText(Path.Combine(repositoryRootPath, "regression", "README.md"));

        Assert.Contains("check-idle", readmeSource, StringComparison.Ordinal);
        Assert.Contains("--idle-throttle on --idle-after-ms 1 --idle-fps 10", readmeSource, StringComparison.Ordinal);
        Assert.Contains("physics", readmeSource, StringComparison.Ordinal);
        Assert.Contains("idle entry missing from manifest", readmeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the overlay scenario launches the smoke scene with the overlay window flags plus one hit-test probe, reads
    /// the probe's HIT_TEST line from the startup log, and checks every overlay capture with the alpha-aware commands only:
    /// the premultiplied invariant at 1% transparent and 1% opaque, and the four-channel comparison with the overlay golden.
    /// </summary>
    [Fact]
    public void RegressionScript_RunsTheOverlayScenarioWithAlphaAwareChecks() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("$overlayPlayerArguments = @('--window-mode', 'overlay', '--overlay-bounds', 'profile', '--overlay-background', 'transparent')", scriptSource, StringComparison.Ordinal);
        Assert.Contains("-ExtraArguments ($script:overlayPlayerArguments + $ExtraArguments + @('--hit-test-probe', $Probe))", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'(HIT_TEST .+)$'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("HitTest = $hitTestLine", scriptSource, StringComparison.Ordinal);
        Assert.Contains("@('check-premultiplied', $CapturePath, '0.01', '0.01')", scriptSource, StringComparison.Ordinal);

        string overlayScenarioSource = ReadFunctionSource(scriptSource, "Invoke-OverlayScenario");
        Assert.Contains("Invoke-OverlayProbeRun -Kind overlay", overlayScenarioSource, StringComparison.Ordinal);
        Assert.Contains("Test-CaptureMatchesGolden -Kind overlay -SceneId $SceneId -CompareCommand compare-rgba", overlayScenarioSource, StringComparison.Ordinal);
        Assert.DoesNotContain("-CompareCommand compare ", overlayScenarioSource, StringComparison.Ordinal);
        Assert.Contains("Compare-FingerprintsByWindow", overlayScenarioSource, StringComparison.Ordinal);
        Assert.Contains("Test-OverlayPremultiplied", overlayScenarioSource, StringComparison.Ordinal);
        Assert.Contains("Test-OverlayHitTest", overlayScenarioSource, StringComparison.Ordinal);

        string captureSource = ReadFunctionSource(scriptSource, "Test-CaptureMatchesGolden");
        Assert.Contains("[ValidateSet('compare', 'compare-rgba')]", captureSource, StringComparison.Ordinal);
        Assert.Contains("@($CompareCommand, $CapturePath, $GoldenPath, $DiffPath)", captureSource, StringComparison.Ordinal);

        string probeRunSource = ReadFunctionSource(scriptSource, "Invoke-OverlayProbeRun");
        int staleDiffRemovalIndex = probeRunSource.IndexOf("Remove-Item -LiteralPath $DiffPath -Force", StringComparison.Ordinal);
        int overlayRunIndex = probeRunSource.IndexOf("$overlayRun = Invoke-PlayerScene", StringComparison.Ordinal);
        Assert.True(staleDiffRemovalIndex >= 0 && overlayRunIndex > staleDiffRemovalIndex, "Invoke-OverlayProbeRun must delete a stale diff image before the run.");
    }

    /// <summary>
    /// Ensures the HIT_TEST check is an exact match that includes the window's extended style after the probe's
    /// click-through toggle: eight upper-case hexadecimal digits, WS_EX_TRANSPARENT (0x20) set for clickThrough=on and
    /// cleared for clickThrough=off, and equal to the recorded value when one is given (a damaged recorded value is a FAIL
    /// asking for a re-record).
    /// </summary>
    [Fact]
    public void RegressionScript_MatchesTheHitTestExStyleExactly() {
        string scriptSource = ReadRegressionScriptSource();

        string hitTestSource = ReadFunctionSource(scriptSource, "Test-OverlayHitTest");
        Assert.Contains("clickThrough=$ExpectedClickThrough exStyle=(0x[0-9A-F]{8})$", hitTestSource, StringComparison.Ordinal);
        Assert.Contains("$HitTestLine -cnotmatch $expectedHitTestPattern", hitTestSource, StringComparison.Ordinal);
        Assert.Contains("-band 0x20", hitTestSource, StringComparison.Ordinal);
        Assert.Contains("$ExpectedExStyle -cnotmatch '^0x[0-9A-F]{8}$'", hitTestSource, StringComparison.Ordinal);
        Assert.Contains("$observedExStyle -cne $ExpectedExStyle", hitTestSource, StringComparison.Ordinal);
        Assert.Contains("return $observedExStyle", hitTestSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures a probe value is validated as <c>x,y</c> before it is passed to the player or used in a regex: a malformed
    /// value (for example a hand-edited manifest) is a FAIL asking for a re-record, and the HIT_TEST pattern escapes the
    /// probe coordinates it embeds.
    /// </summary>
    [Fact]
    public void RegressionScript_ValidatesOverlayProbesBeforeUsingThem() {
        string scriptSource = ReadRegressionScriptSource();

        string probeRunSource = ReadFunctionSource(scriptSource, "Invoke-OverlayProbeRun");
        int validationIndex = probeRunSource.IndexOf("if ($Probe -notmatch '^\\d+,\\d+$') {", StringComparison.Ordinal);
        int overlayRunIndex = probeRunSource.IndexOf("$overlayRun = Invoke-PlayerScene", StringComparison.Ordinal);
        Assert.True(validationIndex >= 0, "Invoke-OverlayProbeRun must validate the probe with ^\\d+,\\d+$.");
        Assert.True(overlayRunIndex > validationIndex, "The probe must be validated before it is passed to the player.");
        string validationBlock = probeRunSource.Substring(validationIndex, overlayRunIndex - validationIndex);
        Assert.Contains("Add-CheckResult -Status FAIL -Kind $Kind", validationBlock, StringComparison.Ordinal);
        Assert.Contains("(re-record required)", validationBlock, StringComparison.Ordinal);
        Assert.Contains("return", validationBlock, StringComparison.Ordinal);

        string hitTestSource = ReadFunctionSource(scriptSource, "Test-OverlayHitTest");
        int hitTestValidationIndex = hitTestSource.IndexOf("if ($Probe -notmatch '^\\d+,\\d+$') {", StringComparison.Ordinal);
        int patternIndex = hitTestSource.IndexOf("$expectedHitTestPattern = ", StringComparison.Ordinal);
        Assert.True(hitTestValidationIndex >= 0 && patternIndex > hitTestValidationIndex, "Test-OverlayHitTest must validate the probe before building its regex.");
        Assert.Contains("[regex]::Escape($probeCoordinates[0])", hitTestSource, StringComparison.Ordinal);
        Assert.Contains("[regex]::Escape($probeCoordinates[1])", hitTestSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures Record runs the overlay scenario in the ruled order: one run to record the alpha-preserving overlay golden
    /// into the staging folder, find-probes on that golden, then a transparent-probe run that must report clickThrough=on
    /// and an opaque-probe run that must report clickThrough=off (each checked for health and overlay shape in record
    /// mode), and only then the manifest's overlay entry with both probes, both observed exStyle values and one
    /// fingerprint map per probe.
    /// </summary>
    [Fact]
    public void RegressionScript_RecordsTheOverlayGoldenProbesAndFingerprint() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("$overlayRecordProbe = '2,2'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("$overlayGoldenFileName = \"$($smokeSceneId.Replace('/', '__')).overlay.png\"", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'^TRANSPARENT (\\d+,\\d+)$'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'^OPAQUE (\\d+,\\d+)$'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("kind = 'overlay'; transparentProbe = $overlayTransparentProbe; opaqueProbe = $overlayOpaqueProbe; transparentExStyle = $overlayTransparentResult.ExStyle; opaqueExStyle = $overlayOpaqueResult.ExStyle; fingerprintsByProbe = $overlayFingerprintsByProbe", scriptSource, StringComparison.Ordinal);
        Assert.Contains("transparent = (ConvertTo-ManifestFingerprints -FingerprintsByWindow $overlayTransparentResult.FingerprintsByWindow)", scriptSource, StringComparison.Ordinal);
        Assert.Contains("opaque = (ConvertTo-ManifestFingerprints -FingerprintsByWindow $overlayOpaqueResult.FingerprintsByWindow)", scriptSource, StringComparison.Ordinal);

        int recordIndex = scriptSource.IndexOf("# 11R.", StringComparison.Ordinal);
        int verifyIndex = scriptSource.IndexOf("# 11V.", StringComparison.Ordinal);
        Assert.True(recordIndex >= 0 && verifyIndex > recordIndex, "The script must have a Record branch before the Verify branch.");
        string recordSource = scriptSource.Substring(recordIndex, verifyIndex - recordIndex);

        int recordRunIndex = recordSource.IndexOf("$overlayRecordRun = Invoke-PlayerScene", StringComparison.Ordinal);
        int healthIndex = recordSource.IndexOf("Invoke-FingerprintHealthCheck -Name \"$smokeSceneId.overlay\" -FingerprintsByWindow $overlayRecordRun.FingerprintsByWindow", StringComparison.Ordinal);
        int shapeIndex = recordSource.IndexOf("Test-OverlayFingerprintShape -SceneId $smokeSceneId -FingerprintLine $overlayRecordRun.FingerprintsByWindow[$overlayWindowName]", StringComparison.Ordinal);
        int premultipliedIndex = recordSource.IndexOf("Test-OverlayPremultiplied -Kind overlay -SceneId $smokeSceneId -ProbeName record", StringComparison.Ordinal);
        int recordGoldenIndex = recordSource.IndexOf("@('record-golden-rgba', $overlayRecordCapturePath, $overlayStagingGoldenPath)", StringComparison.Ordinal);
        int findProbesIndex = recordSource.IndexOf("@('find-probes', $overlayStagingGoldenPath)", StringComparison.Ordinal);
        int transparentRunIndex = recordSource.IndexOf("-ProbeName transparent -Probe $overlayTransparentProbe -ExpectedClickThrough on -ExpectedExStyle '' -RecordMode", StringComparison.Ordinal);
        int opaqueRunIndex = recordSource.IndexOf("-ProbeName opaque -Probe $overlayOpaqueProbe -ExpectedClickThrough off -ExpectedExStyle '' -RecordMode", StringComparison.Ordinal);
        int manifestEntryIndex = recordSource.IndexOf("kind = 'overlay'", StringComparison.Ordinal);

        Assert.True(recordRunIndex >= 0, "Record must run the overlay scenario once to record its golden.");
        Assert.True(healthIndex > recordRunIndex, "Record must check that the overlay record run is healthy.");
        Assert.True(shapeIndex > recordRunIndex, "Record must check the overlay record run's window mode, alpha mode and extended styles.");
        Assert.True(premultipliedIndex > recordRunIndex, "Record must check that the overlay record capture is valid premultiplied alpha.");
        Assert.True(recordGoldenIndex > premultipliedIndex, "Record must record the overlay golden after checking the capture.");
        Assert.True(findProbesIndex > recordGoldenIndex, "Record must find the probes on the staged overlay golden.");
        Assert.True(transparentRunIndex > findProbesIndex, "Record must run the transparent probe after finding the probes.");
        Assert.True(opaqueRunIndex > transparentRunIndex, "Record must run the opaque probe after the transparent probe.");
        Assert.True(manifestEntryIndex > opaqueRunIndex, "Record must add the overlay manifest entry after both probe runs.");
        Assert.Contains("-GoldenPath $overlayStagingGoldenPath", recordSource, StringComparison.Ordinal);

        string overlayScenarioSource = ReadFunctionSource(scriptSource, "Invoke-OverlayScenario");
        Assert.Contains("[switch]$RecordMode", overlayScenarioSource, StringComparison.Ordinal);
        Assert.Contains("Invoke-FingerprintHealthCheck -Name $fingerprintName", overlayScenarioSource, StringComparison.Ordinal);
        Assert.Contains("Test-OverlayFingerprintShape", overlayScenarioSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures Verify reads the manifest's overlay entry, fails with a re-record request when the entry is missing, uses
    /// the old single-fingerprint shape or lacks a probe, a recorded exStyle or a probe's fingerprint map, and otherwise
    /// runs the transparent probe (clickThrough=on) and then the opaque probe (clickThrough=off) against the committed
    /// overlay golden, each with its own recorded exStyle and fingerprint map, and writes the overlay diffs into the diffs
    /// folder it wipes at the start.
    /// </summary>
    [Fact]
    public void RegressionScript_VerifiesTheOverlayScenarioFromTheManifest() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("-eq 'overlay'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("overlay entry missing from manifest (re-record required)", scriptSource, StringComparison.Ordinal);
        Assert.Contains("$null -ne $recordedOverlayScene.PSObject.Properties['fingerprint']", scriptSource, StringComparison.Ordinal);
        Assert.Contains("overlay entry uses the old single 'fingerprint' shape (re-record required)", scriptSource, StringComparison.Ordinal);
        Assert.Contains("$null -eq $recordedOverlayScene.transparentProbe -or $null -eq $recordedOverlayScene.opaqueProbe -or $null -eq $recordedOverlayScene.transparentExStyle -or $null -eq $recordedOverlayScene.opaqueExStyle -or $null -eq $recordedOverlayScene.fingerprintsByProbe.transparent -or $null -eq $recordedOverlayScene.fingerprintsByProbe.opaque", scriptSource, StringComparison.Ordinal);
        Assert.Contains("overlay entry lacks transparentProbe, opaqueProbe, transparentExStyle, opaqueExStyle or fingerprintsByProbe.transparent/opaque (re-record required)", scriptSource, StringComparison.Ordinal);

        int verifyIndex = scriptSource.IndexOf("# 11V.", StringComparison.Ordinal);
        Assert.True(verifyIndex >= 0, "The script must have a Verify branch.");
        string verifySource = scriptSource.Substring(verifyIndex);

        int diffsWipeIndex = verifySource.IndexOf("Remove-Item -LiteralPath $diffsRootPath -Recurse -Force", StringComparison.Ordinal);
        int overlayDiffIndex = verifySource.IndexOf("$overlayTransparentDiffPath = Join-Path $diffsRootPath", StringComparison.Ordinal);
        int transparentRunIndex = verifySource.IndexOf("-ProbeName transparent -Probe \"$($recordedOverlayScene.transparentProbe)\" -ExpectedClickThrough on -ExpectedExStyle \"$($recordedOverlayScene.transparentExStyle)\"", StringComparison.Ordinal);
        int opaqueRunIndex = verifySource.IndexOf("-ProbeName opaque -Probe \"$($recordedOverlayScene.opaqueProbe)\" -ExpectedClickThrough off -ExpectedExStyle \"$($recordedOverlayScene.opaqueExStyle)\"", StringComparison.Ordinal);
        Assert.True(diffsWipeIndex >= 0 && overlayDiffIndex > diffsWipeIndex, "Verify must write the overlay diffs into the diffs folder it wipes.");
        Assert.True(transparentRunIndex > overlayDiffIndex, "Verify must run the transparent probe.");
        Assert.True(opaqueRunIndex > transparentRunIndex, "Verify must run the opaque probe after the transparent probe.");
        Assert.Contains("-GoldenPath (Join-Path $goldenRootPath $overlayGoldenFileName)", verifySource, StringComparison.Ordinal);
        Assert.Contains("-RecordedFingerprintsByWindow (ConvertTo-RecordedFingerprintsByWindow -RecordedFingerprints $recordedOverlayScene.fingerprintsByProbe.transparent)", verifySource, StringComparison.Ordinal);
        Assert.Contains("-RecordedFingerprintsByWindow (ConvertTo-RecordedFingerprintsByWindow -RecordedFingerprints $recordedOverlayScene.fingerprintsByProbe.opaque)", verifySource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the overlay+idle scenario runs the smoke scene in the overlay window with the idle-throttle flags and the
    /// recorded transparent probe, and checks it with check-idle, check-premultiplied, compare-rgba against the overlay
    /// golden and an exact HIT_TEST (clickThrough=on, the recorded transparent exStyle) but never compares its
    /// fingerprint; Record adds an overlayIdle entry with its fingerprints for reference, and Verify fails a manifest
    /// without that entry and takes the check-idle minimums from the idle entry.
    /// </summary>
    [Fact]
    public void RegressionScript_RunsTheOverlayIdleScenario() {
        string scriptSource = ReadRegressionScriptSource();

        string overlayIdleSource = ReadFunctionSource(scriptSource, "Invoke-OverlayIdleScenario");
        Assert.Contains("Invoke-OverlayProbeRun -Kind overlayIdle -SceneId $SceneId -ProbeName transparent -Probe $Probe -CapturePath $CapturePath -DiffPath $DiffPath -ExtraArguments $script:idlePlayerArguments", overlayIdleSource, StringComparison.Ordinal);
        Assert.Contains("Test-OverlayHitTest -Kind overlayIdle -SceneId $SceneId -ProbeName transparent -Probe $Probe -ExpectedClickThrough on -ExpectedExStyle $ExpectedExStyle", overlayIdleSource, StringComparison.Ordinal);
        Assert.Contains("Test-IdleFingerprints -Kind overlayIdle", overlayIdleSource, StringComparison.Ordinal);
        Assert.Contains("Test-OverlayPremultiplied -Kind overlayIdle", overlayIdleSource, StringComparison.Ordinal);
        Assert.Contains("Test-CaptureMatchesGolden -Kind overlayIdle -SceneId $SceneId -CompareCommand compare-rgba", overlayIdleSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Compare-FingerprintsByWindow", overlayIdleSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Invoke-PlayerScene", overlayIdleSource, StringComparison.Ordinal);

        int recordIndex = scriptSource.IndexOf("# 11R.", StringComparison.Ordinal);
        int verifyIndex = scriptSource.IndexOf("# 11V.", StringComparison.Ordinal);
        string recordSource = scriptSource.Substring(recordIndex, verifyIndex - recordIndex);
        Assert.Contains("Invoke-OverlayIdleScenario -SceneId $smokeSceneId -Probe $overlayTransparentProbe -ExpectedExStyle $overlayTransparentResult.ExStyle", recordSource, StringComparison.Ordinal);
        Assert.Contains("-MinimumIdleFrames $idleMinimumIdleFrames -MinimumElapsedMilliseconds $idleMinimumElapsedMilliseconds", recordSource, StringComparison.Ordinal);
        Assert.Contains("kind = 'overlayIdle'; fingerprints = (ConvertTo-ManifestFingerprints -FingerprintsByWindow $overlayIdleFingerprintsByWindow)", recordSource, StringComparison.Ordinal);

        string verifySource = scriptSource.Substring(verifyIndex);
        Assert.Contains("-eq 'overlayIdle'", verifySource, StringComparison.Ordinal);
        Assert.Contains("overlayIdle entry missing from manifest (re-record required)", verifySource, StringComparison.Ordinal);
        Assert.Contains("Invoke-OverlayIdleScenario -SceneId $smokeSceneId -Probe \"$($recordedOverlayScene.transparentProbe)\" -ExpectedExStyle \"$($recordedOverlayScene.transparentExStyle)\"", verifySource, StringComparison.Ordinal);
        Assert.Contains("-GoldenPath (Join-Path $goldenRootPath $overlayGoldenFileName) -MinimumIdleFrames \"$($recordedIdleScene.minIdleFrames)\" -MinimumElapsedMilliseconds \"$($recordedIdleScene.minElapsedMs)\"", verifySource, StringComparison.Ordinal);
        int diffsWipeIndex = verifySource.IndexOf("Remove-Item -LiteralPath $diffsRootPath -Recurse -Force", StringComparison.Ordinal);
        int overlayIdleDiffIndex = verifySource.IndexOf("$overlayIdleDiffPath = Join-Path $diffsRootPath", StringComparison.Ordinal);
        Assert.True(diffsWipeIndex >= 0 && overlayIdleDiffIndex > diffsWipeIndex, "Verify must write the overlayIdle diff into the diffs folder it wipes.");
    }

    /// <summary>
    /// Ensures the idle, overlay and overlay+idle scenarios share their launch and check logic instead of copying it:
    /// check-idle is called in one function, the capture comparison in one function, and the overlay launches go through
    /// one probe-run function.
    /// </summary>
    [Fact]
    public void RegressionScript_SharesScenarioChecksInsteadOfCopyingThem() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Equal(1, CountOccurrences(scriptSource, "@('check-idle',"));
        Assert.Equal(1, CountOccurrences(scriptSource, "@($CompareCommand, $CapturePath, $GoldenPath, $DiffPath)"));
        Assert.Equal(1, CountOccurrences(scriptSource, "$script:overlayPlayerArguments + $ExtraArguments"));

        string idleScenarioSource = ReadFunctionSource(scriptSource, "Invoke-IdleScenario");
        Assert.Contains("Test-IdleFingerprints -Kind idle", idleScenarioSource, StringComparison.Ordinal);
        Assert.Contains("Test-CaptureMatchesGolden -Kind idle -SceneId $SceneId -CompareCommand compare", idleScenarioSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the regression README documents the per-window and per-probe fingerprints, the exStyle in HIT_TEST, the
    /// overlay+idle scenario, the one-time migration of the manifest shape and the live-cursor blind spot.
    /// </summary>
    [Fact]
    public void RegressionReadme_DocumentsPerWindowFingerprintsAndTheOverlayIdleScenario() {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string readmeSource = File.ReadAllText(Path.Combine(repositoryRootPath, "regression", "README.md"));

        Assert.Contains("## Overlay + idle scenario", readmeSource, StringComparison.Ordinal);
        Assert.Contains("\"fingerprints\"", readmeSource, StringComparison.Ordinal);
        Assert.Contains("fingerprintsByProbe", readmeSource, StringComparison.Ordinal);
        Assert.Contains("transparentExStyle", readmeSource, StringComparison.Ordinal);
        Assert.Contains("opaqueExStyle", readmeSource, StringComparison.Ordinal);
        Assert.Contains("clickThrough=on exStyle=0x", readmeSource, StringComparison.Ordinal);
        Assert.Contains("window missing", readmeSource, StringComparison.Ordinal);
        Assert.Contains("window extra", readmeSource, StringComparison.Ordinal);
        Assert.Contains("duplicate window", readmeSource, StringComparison.Ordinal);
        Assert.Contains("overlayIdle entry missing from manifest (re-record required)", readmeSource, StringComparison.Ordinal);
        Assert.Contains("uses the old single 'fingerprint' shape", readmeSource, StringComparison.Ordinal);
        Assert.Contains("live cursor", readmeSource, StringComparison.Ordinal);
        Assert.Contains("windowRect", readmeSource, StringComparison.Ordinal);
        Assert.Contains("dpiAwareness", readmeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the regression README documents the overlay scenario, its flags and probes, its manifest entry and checks,
    /// and that on-screen DWM composition and real clicks are covered only by the manual proofs.
    /// </summary>
    [Fact]
    public void RegressionReadme_DocumentsTheOverlayScenario() {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string readmeSource = File.ReadAllText(Path.Combine(repositoryRootPath, "regression", "README.md"));

        Assert.Contains("## Overlay scenario", readmeSource, StringComparison.Ordinal);
        Assert.Contains("--window-mode overlay --overlay-bounds profile --overlay-background transparent", readmeSource, StringComparison.Ordinal);
        Assert.Contains("--hit-test-probe", readmeSource, StringComparison.Ordinal);
        Assert.Contains("transparentProbe", readmeSource, StringComparison.Ordinal);
        Assert.Contains("opaqueProbe", readmeSource, StringComparison.Ordinal);
        Assert.Contains("find-probes", readmeSource, StringComparison.Ordinal);
        Assert.Contains("record-golden-rgba", readmeSource, StringComparison.Ordinal);
        Assert.Contains("check-premultiplied <capture.bmp> 0.01 0.01", readmeSource, StringComparison.Ordinal);
        Assert.Contains("compare-rgba", readmeSource, StringComparison.Ordinal);
        Assert.Contains("axis_test.overlay.png", readmeSource, StringComparison.Ordinal);
        Assert.Contains("clickThrough=on", readmeSource, StringComparison.Ordinal);
        Assert.Contains("clickThrough=off", readmeSource, StringComparison.Ordinal);
        Assert.Contains("overlay entry missing from manifest (re-record required)", readmeSource, StringComparison.Ordinal);
        Assert.Contains("manual proofs", readmeSource, StringComparison.Ordinal);
        Assert.Contains("DWM", readmeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures each suite's executed-test count is recorded beside its failing baseline and checked on verify, so an
    /// aborted or truncated run cannot pass.
    /// </summary>
    [Fact]
    public void RegressionScript_RecordsAndChecksExecutedTestCounts() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("'record-executed'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("'check-executed'", scriptSource, StringComparison.Ordinal);
        Assert.Contains(".executed.txt", scriptSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures a record is staged under the work root and only replaces the committed goldens and baselines when the
    /// whole record had no FAIL.
    /// </summary>
    [Fact]
    public void RegressionScript_PublishesRecordOnlyWhenItPassed() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains("'record-staging'", scriptSource, StringComparison.Ordinal);
        Assert.Contains("RECORD_STAGING=", scriptSource, StringComparison.Ordinal);
        Assert.Matches(
            new System.Text.RegularExpressions.Regex(@"if \(\$Record\) \{\s*if \(\$failingCheckCount -eq 0\) \{\s*Publish-RecordStaging"),
            scriptSource);
    }

    /// <summary>
    /// Ensures the script marks the work roots it creates and refuses a non-empty folder without that marker, and refuses
    /// a project whose copy uses Git LFS.
    /// </summary>
    [Fact]
    public void RegressionScript_GuardsWorkRootAndLfs() {
        string scriptSource = ReadRegressionScriptSource();

        Assert.Contains(".helengine-regression-workroot", scriptSource, StringComparison.Ordinal);
        Assert.Contains("refusing to use a non-empty folder that was not created by run-regression.ps1", scriptSource, StringComparison.Ordinal);
        Assert.Contains("filter=lfs", scriptSource, StringComparison.Ordinal);
        int extractIndex = scriptSource.IndexOf("-xf $projectArchivePath", StringComparison.Ordinal);
        int lfsIndex = scriptSource.IndexOf("filter=lfs", StringComparison.Ordinal);
        Assert.True(extractIndex >= 0 && lfsIndex > extractIndex, "The LFS check must run on the extracted copy.");
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

    /// <summary>
    /// Returns the source of one function of the regression script, from its "function &lt;name&gt; {" line up to the next
    /// function definition (or the end of the function block), failing the test when the function is absent.
    /// </summary>
    /// <param name="scriptSource">The full text of scripts/run-regression.ps1.</param>
    /// <param name="functionName">The PowerShell function's name, for example Invoke-OverlayScenario.</param>
    /// <returns>The function's source text.</returns>
    static string ReadFunctionSource(string scriptSource, string functionName) {
        int functionIndex = scriptSource.IndexOf("function " + functionName + " {", StringComparison.Ordinal);
        Assert.True(functionIndex >= 0, "The script must define " + functionName + ".");

        int nextFunctionIndex = scriptSource.IndexOf("\nfunction ", functionIndex + 1, StringComparison.Ordinal);
        if (nextFunctionIndex < 0) {
            nextFunctionIndex = scriptSource.IndexOf("$RepoRoot = (Resolve-Path", functionIndex, StringComparison.Ordinal);
        }

        return scriptSource.Substring(functionIndex, nextFunctionIndex - functionIndex);
    }

    /// <summary>
    /// Counts the non-overlapping ordinal occurrences of a text in the script source, so a test can require that a piece
    /// of logic exists in exactly one place.
    /// </summary>
    /// <param name="scriptSource">The full text of scripts/run-regression.ps1.</param>
    /// <param name="text">The exact text to count.</param>
    /// <returns>How many times the text occurs.</returns>
    static int CountOccurrences(string scriptSource, string text) {
        int count = 0;
        int searchIndex = scriptSource.IndexOf(text, StringComparison.Ordinal);
        while (searchIndex >= 0) {
            count++;
            searchIndex = scriptSource.IndexOf(text, searchIndex + text.Length, StringComparison.Ordinal);
        }

        return count;
    }
}
