#nullable enable
using System.Text.Json;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;

namespace helengine.windows.builder;

/// <summary>
/// Implements the Windows builder execution flow for staged payloads.
/// </summary>
public static class WindowsBuildWorkspace {
    /// <summary>
    /// Executes one Windows build request by copying staged payloads into the output root and writing a manifest.
    /// </summary>
    /// <param name="request">Resolved build request to process.</param>
    /// <param name="progressReporter">Progress reporter that receives streaming updates.</param>
    /// <param name="diagnosticReporter">Diagnostic reporter that receives streaming diagnostics.</param>
    /// <param name="cancellationToken">Cancellation token that can stop the build cooperatively.</param>
    /// <returns>The final build report.</returns>
    internal static Task<PlatformBuildReport> BuildAsync(
        PlatformBuildRequest request,
        IPlatformBuildProgressReporter progressReporter,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        IWindowsNativeBuildExecutor nativeBuildExecutor,
        CancellationToken cancellationToken) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        } else if (progressReporter == null) {
            throw new ArgumentNullException(nameof(progressReporter));
        } else if (diagnosticReporter == null) {
            throw new ArgumentNullException(nameof(diagnosticReporter));
        } else if (nativeBuildExecutor == null) {
            throw new ArgumentNullException(nameof(nativeBuildExecutor));
        }

        string stagingRoot = Directory.GetCurrentDirectory();
        string builderWorkingRoot = ResolveBuilderWorkingRoot(request.WorkingRoot, stagingRoot);

        ResetDirectoryIfPresent(request.OutputRoot);
        ResetDirectoryIfPresent(builderWorkingRoot);
        Directory.CreateDirectory(request.OutputRoot);
        Directory.CreateDirectory(builderWorkingRoot);

        List<PlatformBuildDiagnostic> diagnostics = [];
        List<PlatformBuildItemOutcome> sceneOutcomes = [];
        List<PlatformBuildItemOutcome> looseAssetOutcomes = [];
        List<WindowsBuildManifestEntry> sceneEntries = [];
        List<WindowsBuildManifestEntry> looseAssetEntries = [];

        int totalItems = request.Manifest.Scenes.Length + request.Manifest.LooseAssets.Length + request.Manifest.PlatformCookWorkItems.Length + 1;
        int completedItems = 0;
        bool nativeBuildSucceeded = false;
        string repositoryRoot = ResolveRepositoryRoot();
        string generatedCoreRoot = ResolveGeneratedCoreRoot(request);
        WriteRuntimeNativeManifest(generatedCoreRoot, request.Manifest, request.SelectedGraphicsOptionValues);
        for (int sceneIndex = 0; sceneIndex < request.Manifest.Scenes.Length; sceneIndex++) {
            cancellationToken.ThrowIfCancellationRequested();

            PlatformBuildScene scene = request.Manifest.Scenes[sceneIndex];
            CopyPayload(
                scene.SceneId,
                scene.SourceIdentity,
                BuildCookedOutputRoot(request.OutputRoot),
                diagnostics,
                diagnosticReporter,
                out bool copied,
                out string outputPath);

            sceneOutcomes.Add(new PlatformBuildItemOutcome(
                scene.SceneId,
                copied ? PlatformBuildItemOutcomeKind.Succeeded : PlatformBuildItemOutcomeKind.Failed));

            completedItems++;
            progressReporter.Report(new PlatformBuildProgressUpdate(
                "Stage Payloads",
                scene.SceneId,
                completedItems,
                totalItems,
                copied ? $"Staged scene '{scene.SceneName}'." : $"Failed to stage scene '{scene.SceneName}'."));

            if (copied) {
                sceneEntries.Add(new WindowsBuildManifestEntry(scene.SceneId, scene.SourceIdentity, outputPath));
            }
        }

        for (int assetIndex = 0; assetIndex < request.Manifest.LooseAssets.Length; assetIndex++) {
            cancellationToken.ThrowIfCancellationRequested();

            PlatformBuildAsset asset = request.Manifest.LooseAssets[assetIndex];
            CopyPayload(
                asset.AssetId,
                asset.SourceIdentity,
                BuildCookedOutputRoot(request.OutputRoot),
                diagnostics,
                diagnosticReporter,
                out bool copied,
                out string outputPath);

            looseAssetOutcomes.Add(new PlatformBuildItemOutcome(
                asset.AssetId,
                copied ? PlatformBuildItemOutcomeKind.Succeeded : PlatformBuildItemOutcomeKind.Failed));

            completedItems++;
            progressReporter.Report(new PlatformBuildProgressUpdate(
                "Stage Payloads",
                asset.AssetId,
                completedItems,
                totalItems,
                copied ? $"Staged asset '{asset.AssetName}'." : $"Failed to stage asset '{asset.AssetName}'."));

            if (copied) {
                looseAssetEntries.Add(new WindowsBuildManifestEntry(asset.AssetId, asset.SourceIdentity, outputPath));
            }
        }

        for (int workItemIndex = 0; workItemIndex < request.Manifest.PlatformCookWorkItems.Length; workItemIndex++) {
            cancellationToken.ThrowIfCancellationRequested();

            PlatformCookWorkItem workItem = request.Manifest.PlatformCookWorkItems[workItemIndex];
            CopyPlatformCookWorkItem(
                workItem,
                request.OutputRoot,
                diagnostics,
                diagnosticReporter,
                out bool copied,
                out string outputPath);

            completedItems++;
            string workItemLabel = workItem == null || string.IsNullOrWhiteSpace(workItem.OutputLogicalArtifactId)
                ? $"platform-work-item-{workItemIndex}"
                : workItem.OutputLogicalArtifactId;
            progressReporter.Report(new PlatformBuildProgressUpdate(
                "Stage Builder-Owned Assets",
                workItemLabel,
                completedItems,
                totalItems,
                copied
                    ? $"Staged builder-owned asset '{workItemLabel}'."
                    : $"Failed to stage builder-owned asset '{workItemLabel}'."));
        }

        WriteBuildManifest(request, builderWorkingRoot, sceneEntries, looseAssetEntries);
        CopyStagedPayloadTreeToOutputRoot(request.OutputRoot);

        cancellationToken.ThrowIfCancellationRequested();
        progressReporter.Report(new PlatformBuildProgressUpdate(
            "Native Build",
            "helengine_windows",
            completedItems,
            totalItems,
            "Running native Windows build."));

        string nativeBuildRoot = Path.Combine(builderWorkingRoot, "native");

        try {
            string nativeExecutablePath = nativeBuildExecutor.Build(
                repositoryRoot,
                nativeBuildRoot,
                generatedCoreRoot,
                Path.Combine(stagingRoot, "code"),
                cancellationToken);
            string destinationExecutablePath = Path.Combine(request.OutputRoot, Path.GetFileName(nativeExecutablePath));
            Directory.CreateDirectory(request.OutputRoot);
            File.Copy(nativeExecutablePath, destinationExecutablePath, true);

            string sourcePdbPath = Path.ChangeExtension(nativeExecutablePath, ".pdb");
            if (File.Exists(sourcePdbPath)) {
                string destinationPdbPath = Path.ChangeExtension(destinationExecutablePath, ".pdb");
                File.Copy(sourcePdbPath, destinationPdbPath, true);
            }

            nativeBuildSucceeded = true;
            completedItems++;
            progressReporter.Report(new PlatformBuildProgressUpdate(
                "Native Build",
                "helengine_windows",
                completedItems,
                totalItems,
                $"Built native Windows player at '{destinationExecutablePath}'."));
        } catch (Exception ex) {
            AddDiagnostic(
                diagnostics,
                diagnosticReporter,
                PlatformBuildDiagnosticSeverity.Error,
                "WINBUILD003",
                $"Native Windows build failed: {ex.Message}",
                string.Empty,
                string.Empty,
                request.OutputRoot);

            completedItems++;
            progressReporter.Report(new PlatformBuildProgressUpdate(
                "Native Build",
                "helengine_windows",
                completedItems,
                totalItems,
                "Native Windows build failed."));
        }

        bool succeeded = diagnostics.Count == 0
            && sceneOutcomes.TrueForAll(outcome => outcome.OutcomeKind == PlatformBuildItemOutcomeKind.Succeeded)
            && looseAssetOutcomes.TrueForAll(outcome => outcome.OutcomeKind == PlatformBuildItemOutcomeKind.Succeeded)
            && nativeBuildSucceeded;

        return Task.FromResult(new PlatformBuildReport(
            succeeded,
            [.. diagnostics],
            [.. sceneOutcomes],
            [.. looseAssetOutcomes]));
    }

    /// <summary>
    /// Copies one staged payload into the output root and records any failure as a diagnostic.
    /// </summary>
    /// <param name="itemId">Resolved item identifier.</param>
    /// <param name="sourceIdentity">Source identity recorded in the build request.</param>
    /// <param name="outputRoot">Final output root for the build.</param>
    /// <param name="diagnostics">Diagnostic list collecting failures.</param>
    /// <param name="diagnosticReporter">Diagnostic reporter that mirrors collected failures.</param>
    /// <param name="copied">Returns whether the payload was copied.</param>
    /// <param name="outputPath">Returns the destination path for the copied payload.</param>
    static void CopyPayload(
        string itemId,
        string sourceIdentity,
        string outputRoot,
        List<PlatformBuildDiagnostic> diagnostics,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        out bool copied,
        out string outputPath) {
        copied = false;
        outputPath = string.Empty;

        if (string.IsNullOrWhiteSpace(sourceIdentity)) {
            AddDiagnostic(
                diagnostics,
                diagnosticReporter,
                PlatformBuildDiagnosticSeverity.Error,
                "WINBUILD001",
                $"Item '{itemId}' is missing a source identity.",
                string.Empty,
                itemId,
                itemId);
            return;
        }

        string sourcePath = ResolveSourcePath(sourceIdentity);
        if (!File.Exists(sourcePath)) {
            AddDiagnostic(
                diagnostics,
                diagnosticReporter,
                PlatformBuildDiagnosticSeverity.Error,
                "WINBUILD002",
                $"Payload source '{sourceIdentity}' was not found.",
                string.Empty,
                itemId,
                sourceIdentity);
            return;
        }

        outputPath = ResolveOutputPath(outputRoot, sourceIdentity);
        string destinationDirectory = Path.GetDirectoryName(outputPath);
        if (!string.IsNullOrWhiteSpace(destinationDirectory)) {
            Directory.CreateDirectory(destinationDirectory);
        }

        File.Copy(sourcePath, outputPath, true);
        copied = true;
    }

    /// <summary>
    /// Copies one builder-owned cooked artifact into the output root using its resolved runtime-relative output path.
    /// </summary>
    /// <param name="workItem">Builder-owned cook work item to materialize.</param>
    /// <param name="outputRoot">Final output root for the build.</param>
    /// <param name="diagnostics">Diagnostic list collecting failures.</param>
    /// <param name="diagnosticReporter">Diagnostic reporter that mirrors collected failures.</param>
    /// <param name="copied">Returns whether the cooked artifact was copied.</param>
    /// <param name="outputPath">Returns the destination path for the copied artifact.</param>
    static void CopyPlatformCookWorkItem(
        PlatformCookWorkItem workItem,
        string outputRoot,
        List<PlatformBuildDiagnostic> diagnostics,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        out bool copied,
        out string outputPath) {
        copied = false;
        outputPath = string.Empty;

        if (workItem == null) {
            AddDiagnostic(
                diagnostics,
                diagnosticReporter,
                PlatformBuildDiagnosticSeverity.Error,
                "WINBUILD004",
                "Builder-owned cook work item was missing.",
                string.Empty,
                string.Empty,
                string.Empty);
            return;
        }

        string sourcePath = ResolveSourcePath(workItem.SourceAssetPath);
        if (!File.Exists(sourcePath)) {
            AddDiagnostic(
                diagnostics,
                diagnosticReporter,
                PlatformBuildDiagnosticSeverity.Error,
                "WINBUILD005",
                $"Builder-owned source asset '{workItem.SourceAssetPath}' was not found.",
                string.Empty,
                workItem.OutputLogicalArtifactId,
                workItem.SourceAssetPath);
            return;
        }

        outputPath = Path.GetFullPath(Path.Combine(outputRoot, ResolveCookedRelativePath(workItem.OutputRelativePath)));
        string? destinationDirectory = Path.GetDirectoryName(outputPath);
        if (!string.IsNullOrWhiteSpace(destinationDirectory)) {
            Directory.CreateDirectory(destinationDirectory);
        }

        File.Copy(sourcePath, outputPath, true);
        copied = true;
    }

    /// <summary>
    /// Adds one diagnostic to the shared list and mirrors it to the reporter.
    /// </summary>
    /// <param name="diagnostics">Collected diagnostics.</param>
    /// <param name="diagnosticReporter">Diagnostic reporter to mirror.</param>
    /// <param name="severity">Diagnostic severity.</param>
    /// <param name="code">Diagnostic code.</param>
    /// <param name="message">Diagnostic message.</param>
    /// <param name="sceneId">Scene identifier for the diagnostic.</param>
    /// <param name="assetId">Asset identifier for the diagnostic.</param>
    /// <param name="sourceIdentity">Source identity for the diagnostic.</param>
    static void AddDiagnostic(
        List<PlatformBuildDiagnostic> diagnostics,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        PlatformBuildDiagnosticSeverity severity,
        string code,
        string message,
        string sceneId,
        string assetId,
        string sourceIdentity) {
        PlatformBuildDiagnostic diagnostic = new(severity, code, message, sceneId, assetId, sourceIdentity);
        diagnostics.Add(diagnostic);
        diagnosticReporter.Report(diagnostic);
    }

    /// <summary>
    /// Writes the build manifest to the working root for traceability.
    /// </summary>
    /// <param name="request">Resolved build request.</param>
    /// <param name="sceneEntries">Resolved scene entries.</param>
    /// <param name="looseAssetEntries">Resolved loose asset entries.</param>
    static void WriteBuildManifest(
        PlatformBuildRequest request,
        string builderWorkingRoot,
        IReadOnlyList<WindowsBuildManifestEntry> sceneEntries,
        IReadOnlyList<WindowsBuildManifestEntry> looseAssetEntries) {
        string workingManifestPath = Path.Combine(builderWorkingRoot, "windows-build-manifest.json");
        object manifest = new {
            request.Manifest.ProjectId,
            request.Manifest.ProjectVersion,
            request.Manifest.RequiredEngineVersion,
            request.Manifest.StartupSceneId,
            request.OutputRoot,
            Scenes = sceneEntries,
            LooseAssets = looseAssetEntries
        };

        string manifestJson = JsonSerializer.Serialize(manifest, JsonOptions);
        File.WriteAllText(workingManifestPath, manifestJson);
    }

    /// <summary>
    /// Resolves one relative source identity into a full path from the current working directory.
    /// </summary>
    /// <param name="sourceIdentity">The source identity recorded in the request.</param>
    /// <returns>The full source path.</returns>
    static string ResolveSourcePath(string sourceIdentity) {
        string normalizedSourceIdentity = sourceIdentity.Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar);
        if (Path.IsPathRooted(normalizedSourceIdentity)) {
            return Path.GetFullPath(normalizedSourceIdentity);
        }

        return Path.GetFullPath(Path.Combine(Directory.GetCurrentDirectory(), normalizedSourceIdentity));
    }

    /// <summary>
    /// Resolves one output path under the supplied output root.
    /// </summary>
    /// <param name="outputRoot">The final output root.</param>
    /// <param name="sourceIdentity">The request source identity.</param>
    /// <returns>The full output path.</returns>
    static string ResolveOutputPath(string outputRoot, string sourceIdentity) {
        string normalizedSourceIdentity = sourceIdentity.Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar);
        if (Path.IsPathRooted(normalizedSourceIdentity)) {
            normalizedSourceIdentity = Path.GetFileName(normalizedSourceIdentity);
        }

        return Path.GetFullPath(Path.Combine(outputRoot, ResolveCookedRelativePath(normalizedSourceIdentity)));
    }

    /// <summary>
    /// Resolves the cooked content root inside the final output directory.
    /// </summary>
    /// <param name="outputRoot">Final platform output root.</param>
    /// <returns>Absolute cooked content root.</returns>
    static string BuildCookedOutputRoot(string outputRoot) {
        return outputRoot;
    }

    /// <summary>
    /// Resolves the final cooked relative path for one staged payload entry.
    /// </summary>
    /// <param name="relativePath">Runtime-relative path under the staged package root.</param>
    /// <returns>Cooked relative path under the final output root.</returns>
    static string ResolveCookedRelativePath(string relativePath) {
        string normalizedRelativePath = relativePath.Replace('\\', '/').TrimStart('/');
        const string CookedPrefix = "cooked/";
        if (normalizedRelativePath.StartsWith(CookedPrefix, StringComparison.OrdinalIgnoreCase)) {
            return normalizedRelativePath.Replace('/', Path.DirectorySeparatorChar);
        }

        const string GeneratedPrefix = "generated/";
        if (normalizedRelativePath.StartsWith(GeneratedPrefix, StringComparison.OrdinalIgnoreCase)) {
            normalizedRelativePath = normalizedRelativePath.Substring(GeneratedPrefix.Length);
        }

        return Path.Combine("cooked", normalizedRelativePath.Replace('/', Path.DirectorySeparatorChar));
    }

    /// <summary>
    /// Resolves the scratch working root used by the builder without colliding with the staged package current directory.
    /// </summary>
    /// <param name="requestedWorkingRoot">Working root supplied by the build request.</param>
    /// <param name="stagingRoot">Current staged package root.</param>
    /// <returns>Absolute working root for manifest and native-build scratch files.</returns>
    static string ResolveBuilderWorkingRoot(string requestedWorkingRoot, string stagingRoot) {
        string normalizedStagingRoot = Path.GetFullPath(string.IsNullOrWhiteSpace(stagingRoot)
            ? Directory.GetCurrentDirectory()
            : stagingRoot);
        string normalizedRequestedWorkingRoot = string.IsNullOrWhiteSpace(requestedWorkingRoot)
            ? Path.Combine(normalizedStagingRoot, "_builder")
            : Path.GetFullPath(requestedWorkingRoot);

        if (string.Equals(normalizedRequestedWorkingRoot, normalizedStagingRoot, StringComparison.OrdinalIgnoreCase)) {
            return Path.Combine(normalizedRequestedWorkingRoot, "_builder");
        }

        return normalizedRequestedWorkingRoot;
    }

    /// <summary>
    /// Writes the generated runtime startup and code-module metadata into the shared generated-core root consumed by the native build.
    /// </summary>
    /// <param name="generatedCoreRoot">Shared generated-core root consumed by CMake.</param>
    /// <param name="manifest">Resolved build manifest that carries runtime startup and code-module metadata.</param>
    static void WriteRuntimeNativeManifest(
        string generatedCoreRoot,
        PlatformBuildManifest manifest,
        IReadOnlyDictionary<string, string> selectedGraphicsOptionValues) {
        WindowsRuntimeNativeManifestWriter writer = new();
        writer.Write(generatedCoreRoot, manifest, selectedGraphicsOptionValues);
    }

    /// <summary>
    /// Deletes one directory tree when it already exists.
    /// </summary>
    /// <param name="path">Directory path to clear.</param>
    static void ResetDirectoryIfPresent(string path) {
        if (string.IsNullOrWhiteSpace(path)) {
            return;
        }

        if (Directory.Exists(path)) {
            Directory.Delete(path, true);
        }
    }

    /// <summary>
    /// Resolves the Windows repository root from the builder assembly location.
    /// </summary>
    /// <returns>The Windows repository root path.</returns>
    static string ResolveRepositoryRoot() {
        string assemblyLocation = typeof(WindowsBuildWorkspace).Assembly.Location;
        if (string.IsNullOrWhiteSpace(assemblyLocation)) {
            throw new InvalidOperationException("Unable to resolve the Windows builder assembly location.");
        }

        string baseDirectory = Path.GetDirectoryName(assemblyLocation)
            ?? throw new InvalidOperationException($"Unable to resolve the Windows builder base directory from '{assemblyLocation}'.");

        return Path.GetFullPath(Path.Combine(baseDirectory, "..", "..", "..", ".."));
    }

    /// <summary>
    /// Resolves the generated-core output root used for one build execution.
    /// </summary>
    /// <param name="request">Resolved build request.</param>
    /// <returns>Absolute generated-core output root.</returns>
    static string ResolveGeneratedCoreRoot(PlatformBuildRequest request) {
        string generatedCoreRoot = string.IsNullOrWhiteSpace(request.GeneratedCoreCppRootPath)
            ? Path.Combine(request.WorkingRoot, "generated-core")
            : request.GeneratedCoreCppRootPath;

        return Path.GetFullPath(generatedCoreRoot);
    }

    /// <summary>
    /// Copies the full staged payload tree into the final output root so runtime asset references resolve correctly.
    /// </summary>
    /// <param name="outputRoot">Final output root for the build.</param>
    static void CopyStagedPayloadTreeToOutputRoot(string outputRoot) {
        string stagingRoot = Directory.GetCurrentDirectory();
        string[] stagedFilePaths = Directory.GetFiles(stagingRoot, "*", SearchOption.AllDirectories);
        for (int index = 0; index < stagedFilePaths.Length; index++) {
            string stagedFilePath = stagedFilePaths[index];
            string relativePath = Path.GetRelativePath(stagingRoot, stagedFilePath);
            if (ShouldSkipStagedPayloadPath(relativePath)) {
                continue;
            }

            string outputPath = Path.Combine(outputRoot, ResolveCookedRelativePath(relativePath));
            string? outputDirectory = Path.GetDirectoryName(outputPath);
            if (!string.IsNullOrWhiteSpace(outputDirectory)) {
                Directory.CreateDirectory(outputDirectory);
            }

            File.Copy(stagedFilePath, outputPath, true);
        }
    }

    /// <summary>
    /// Returns true when one staged path is build scaffolding or editor-only metadata that must not ship in the final package.
    /// </summary>
    /// <param name="relativePath">Relative path under the staged package root.</param>
    /// <returns>True when the path should be skipped.</returns>
    static bool ShouldSkipStagedPayloadPath(string relativePath) {
        if (string.IsNullOrWhiteSpace(relativePath)) {
            return true;
        }

        string normalizedRelativePath = relativePath.Replace('\\', '/').TrimStart('/');
        if (normalizedRelativePath.StartsWith("_builder/", StringComparison.OrdinalIgnoreCase)) {
            return true;
        }

        string fileName = Path.GetFileName(normalizedRelativePath);
        if (string.Equals(fileName, "runtime-startup.json", StringComparison.OrdinalIgnoreCase)
            || string.Equals(fileName, "runtime-code-modules.json", StringComparison.OrdinalIgnoreCase)) {
            return true;
        }

        return false;
    }

    /// <summary>
    /// Represents one payload entry written to the build manifest.
    /// </summary>
    /// <param name="ItemId">The staged item identifier.</param>
    /// <param name="SourceIdentity">The request source identity.</param>
    /// <param name="OutputPath">The output path written by the builder.</param>
    sealed record WindowsBuildManifestEntry(string ItemId, string SourceIdentity, string OutputPath);

    static readonly JsonSerializerOptions JsonOptions = new() {
        WriteIndented = true
    };
}
