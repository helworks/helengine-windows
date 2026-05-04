using helengine.baseplatform.Definitions;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Targets;
using helengine.windows.builder;
using helengine.windows.builder.tests.Builders;
using Xunit;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Windows builder metadata and payload staging behavior.
/// </summary>
public class WindowsPlatformAssetBuilderTests {
    /// <summary>
    /// Verifies the builder exposes the expected Windows metadata.
    /// </summary>
    [Fact]
    public void Descriptor_and_definition_expose_windows_metadata() {
        WindowsPlatformAssetBuilder builder = new();

        Assert.Equal("helengine.windows.builder", builder.Descriptor.BuilderId);
        Assert.Equal("windows", builder.Descriptor.TargetPlatformId);
        Assert.Equal("windows", builder.Definition.PlatformId);
        Assert.Contains(builder.Definition.BuildProfiles, profile => profile.ProfileId == "debug");
        Assert.Contains(builder.Definition.BuildProfiles, profile => profile.ProfileId == "release");
        Assert.Contains(builder.Definition.GraphicsProfiles, profile => profile.ProfileId == "directx11");
        Assert.Contains(builder.Definition.StorageProfiles, profile =>
            profile.ProfileId == "loose-files" &&
            profile.RuntimeSpecializationId == "windows-loose-files");
        Assert.Contains(builder.Definition.ComponentCompatibilities, compatibility =>
            compatibility.ComponentTypeId == "helengine.fpscomponent" &&
            compatibility.CompatibilityKind == PlatformComponentCompatibilityKind.Transform);
        Assert.Contains(builder.Definition.ComponentCompatibilities, compatibility =>
            compatibility.ComponentTypeId == "helengine.meshcomponent" &&
            compatibility.CompatibilityKind == PlatformComponentCompatibilityKind.Transform);
    }

    /// <summary>
    /// Verifies the builder copies staged payloads into the output root.
    /// </summary>
    [Fact]
    public async Task BuildAsync_copies_payloads_into_the_output_root() {
        string workingRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        string outputRoot = Path.Combine(workingRoot, "out");
        string sourceRoot = Path.Combine(workingRoot, "project");
        string generatedCoreRoot = Path.Combine(workingRoot, "generated-core");
        string sceneSourcePath = Path.Combine(sourceRoot, "scenes", "main-menu.hasset");
        string textureSourcePath = Path.Combine(sourceRoot, "assets", "textures", "checker.png");

        Directory.CreateDirectory(Path.GetDirectoryName(sceneSourcePath)!);
        Directory.CreateDirectory(Path.GetDirectoryName(textureSourcePath)!);
            Directory.CreateDirectory(generatedCoreRoot);
            File.WriteAllText(sceneSourcePath, "scene payload");
            File.WriteAllText(textureSourcePath, "texture payload");

        string previousDirectory = Directory.GetCurrentDirectory();
        try {
            Directory.SetCurrentDirectory(sourceRoot);

            PlatformBuildManifest manifest = new(
                1,
                "project",
                "1.0.0",
                "1.0.0",
                [
                    new PlatformBuildScene(
                        "startup",
                        "Startup",
                        "scenes/main-menu.hasset",
                        [],
                        [new KeyValuePair<string, string>("cooked-relative-path", "scenes/main-menu.hasset")])
                ],
                [
                    new PlatformBuildAsset(
                        "checker",
                        "Checker Texture",
                        "assets/textures/checker.png",
                        new PlatformBuildPayloadReference("checker-payload", "assets/textures/checker.png"),
                        [])
                ]);

            PlatformBuildRequest request = new(
                manifest,
                [new PlatformBuildTargetVariant("windows-default", "windows", "windows", "debug")],
                [new PlatformCookProfile(
                    "debug",
                    "Debug",
                    new PlatformCookProfileCapabilities(
                        "windows",
                        "raw",
                        "rgba",
                        "windows-scene-v1",
                        PlatformSerializationEndianness.LittleEndian))],
                outputRoot,
                Path.Combine(workingRoot, "tmp"),
                string.Empty,
                string.Empty,
                string.Empty,
                new Dictionary<string, string>(),
                new Dictionary<string, string>(),
                new Dictionary<string, string>(),
                generatedCoreRoot);

            RecordingNativeBuildExecutor nativeBuildExecutor = new();
            WindowsPlatformAssetBuilder builder = new(nativeBuildExecutor);
            RecordingProgressReporter progressReporter = new();
            RecordingDiagnosticReporter diagnosticReporter = new();

            PlatformBuildReport report = await builder.BuildAsync(request, progressReporter, diagnosticReporter, CancellationToken.None);

            Assert.True(report.Succeeded);
            Assert.Empty(diagnosticReporter.Diagnostics);
            Assert.True(nativeBuildExecutor.WasCalled);
            Assert.True(progressReporter.Updates.Count >= 3);
            Assert.True(File.Exists(Path.Combine(outputRoot, "cooked", "scenes", "main-menu.hasset")));
            Assert.True(File.Exists(Path.Combine(outputRoot, "cooked", "assets", "textures", "checker.png")));
            Assert.True(File.Exists(Path.Combine(outputRoot, "helengine_windows.exe")));
            Assert.True(File.Exists(Path.Combine(workingRoot, "tmp", "windows-build-manifest.json")));
            Assert.False(File.Exists(Path.Combine(outputRoot, "windows-build-manifest.json")));
        } finally {
            try {
                Directory.SetCurrentDirectory(previousDirectory);
            } catch {
            }

            try {
                if (Directory.Exists(workingRoot)) {
                    Directory.Delete(workingRoot, recursive: true);
                }
            } catch {
            }
        }
    }

    /// <summary>
    /// Verifies the builder runs the native Windows build step and copies the produced executable into the output root.
    /// </summary>
    [Fact]
    public async Task BuildAsync_runs_the_native_build_step_and_copies_the_executable() {
        string workingRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        string outputRoot = Path.Combine(workingRoot, "out");
        string sourceRoot = Path.Combine(workingRoot, "project");
        string generatedCoreRoot = Path.Combine(workingRoot, "generated-core");
        string sceneSourcePath = Path.Combine(sourceRoot, "scenes", "main-menu.hasset");

        Directory.CreateDirectory(Path.GetDirectoryName(sceneSourcePath)!);
        Directory.CreateDirectory(generatedCoreRoot);
        File.WriteAllText(sceneSourcePath, "scene payload");

        string previousDirectory = Directory.GetCurrentDirectory();
        try {
            Directory.SetCurrentDirectory(sourceRoot);

            PlatformBuildManifest manifest = new(
                1,
                "project",
                "1.0.0",
                "1.0.0",
                [
                    new PlatformBuildScene(
                        "startup",
                        "Startup",
                        "scenes/main-menu.hasset",
                        [],
                        [new KeyValuePair<string, string>("cooked-relative-path", "scenes/main-menu.hasset")])
                ],
                []);

            PlatformBuildRequest request = new(
                manifest,
                [new PlatformBuildTargetVariant("windows-default", "windows", "windows", "debug")],
                [new PlatformCookProfile(
                    "debug",
                    "Debug",
                    new PlatformCookProfileCapabilities(
                        "windows",
                        "raw",
                        "rgba",
                        "windows-scene-v1",
                        PlatformSerializationEndianness.LittleEndian))],
                outputRoot,
                Path.Combine(workingRoot, "tmp"),
                string.Empty,
                string.Empty,
                string.Empty,
                new Dictionary<string, string>(),
                new Dictionary<string, string>(),
                new Dictionary<string, string>(),
                generatedCoreRoot);

            RecordingNativeBuildExecutor nativeBuildExecutor = new();
            WindowsPlatformAssetBuilder builder = new(nativeBuildExecutor);
            RecordingProgressReporter progressReporter = new();
            RecordingDiagnosticReporter diagnosticReporter = new();

            PlatformBuildReport report = await builder.BuildAsync(request, progressReporter, diagnosticReporter, CancellationToken.None);

            Assert.True(report.Succeeded);
            Assert.Empty(diagnosticReporter.Diagnostics);
            Assert.True(progressReporter.Updates.Count >= 3);
            Assert.True(nativeBuildExecutor.WasCalled);
            Assert.True(File.Exists(Path.Combine(outputRoot, "helengine_windows.exe")));
            Assert.True(File.Exists(Path.Combine(outputRoot, "cooked", "scenes", "main-menu.hasset")));
            Assert.True(File.Exists(Path.Combine(workingRoot, "tmp", "windows-build-manifest.json")));
            Assert.False(File.Exists(Path.Combine(outputRoot, "windows-build-manifest.json")));
        } finally {
            try {
                Directory.SetCurrentDirectory(previousDirectory);
            } catch {
            }

            try {
                if (Directory.Exists(workingRoot)) {
                    Directory.Delete(workingRoot, recursive: true);
                }
            } catch {
            }
        }
    }

    /// <summary>
    /// Verifies the builder succeeds when the requested working root matches the staged package current directory.
    /// </summary>
    [Fact]
    public async Task BuildAsync_succeeds_when_working_root_matches_staging_root() {
        string workingRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        string outputRoot = Path.Combine(workingRoot, "out");
        string sourceRoot = Path.Combine(workingRoot, "package");
        string generatedCoreRoot = Path.Combine(workingRoot, "generated-core");
        string sceneSourcePath = Path.Combine(sourceRoot, "scenes", "main-menu.hasset");

        Directory.CreateDirectory(Path.GetDirectoryName(sceneSourcePath)!);
        Directory.CreateDirectory(generatedCoreRoot);
        File.WriteAllText(sceneSourcePath, "scene payload");

        string previousDirectory = Directory.GetCurrentDirectory();
        try {
            Directory.SetCurrentDirectory(sourceRoot);

            PlatformBuildManifest manifest = new(
                1,
                "project",
                "1.0.0",
                "1.0.0",
                [
                    new PlatformBuildScene(
                        "startup",
                        "Startup",
                        "scenes/main-menu.hasset",
                        [],
                        [new KeyValuePair<string, string>("cooked-relative-path", "scenes/main-menu.hasset")])
                ],
                []);

            PlatformBuildRequest request = new(
                manifest,
                [new PlatformBuildTargetVariant("windows-default", "windows", "windows", "debug")],
                [new PlatformCookProfile(
                    "debug",
                    "Debug",
                    new PlatformCookProfileCapabilities(
                        "windows",
                        "raw",
                        "rgba",
                        "windows-scene-v1",
                        PlatformSerializationEndianness.LittleEndian))],
                outputRoot,
                sourceRoot,
                string.Empty,
                string.Empty,
                string.Empty,
                new Dictionary<string, string>(),
                new Dictionary<string, string>(),
                new Dictionary<string, string>(),
                generatedCoreRoot);

            RecordingNativeBuildExecutor nativeBuildExecutor = new();
            WindowsPlatformAssetBuilder builder = new(nativeBuildExecutor);
            RecordingProgressReporter progressReporter = new();
            RecordingDiagnosticReporter diagnosticReporter = new();

            PlatformBuildReport report = await builder.BuildAsync(request, progressReporter, diagnosticReporter, CancellationToken.None);

            Assert.True(report.Succeeded);
            Assert.Empty(diagnosticReporter.Diagnostics);
            Assert.True(nativeBuildExecutor.WasCalled);
            Assert.True(File.Exists(Path.Combine(outputRoot, "helengine_windows.exe")));
            Assert.True(File.Exists(Path.Combine(outputRoot, "cooked", "scenes", "main-menu.hasset")));
            Assert.True(File.Exists(Path.Combine(sourceRoot, "_builder", "windows-build-manifest.json")));
        } finally {
            try {
                Directory.SetCurrentDirectory(previousDirectory);
            } catch {
            }

            try {
                if (Directory.Exists(workingRoot)) {
                    Directory.Delete(workingRoot, recursive: true);
                }
            } catch {
            }
        }
    }

    /// <summary>
    /// Records the repository and build roots used by the native build hook and synthesizes a dummy executable.
    /// </summary>
    sealed class RecordingNativeBuildExecutor : IWindowsNativeBuildExecutor {
        /// <summary>
        /// Gets whether the build hook was invoked.
        /// </summary>
        public bool WasCalled { get; private set; }

        /// <summary>
        /// Gets the last repository root provided by the builder.
        /// </summary>
        public string RepositoryRoot { get; private set; }

        /// <summary>
        /// Gets the last native build root provided by the builder.
        /// </summary>
        public string BuildRoot { get; private set; }

        /// <summary>
        /// Gets the generated core root passed by the builder.
        /// </summary>
        public string GeneratedCoreCppRootPath { get; private set; }

        /// <summary>
        /// Runs the fake native build step and returns a synthetic executable path.
        /// </summary>
        /// <param name="repositoryRoot">Repository root provided by the builder.</param>
        /// <param name="buildRoot">Native build root provided by the builder.</param>
        /// <param name="generatedCoreCppRootPath">Generated core root provided by the builder.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>Absolute path to the fake native executable.</returns>
        public string Build(string repositoryRoot, string buildRoot, string generatedCoreCppRootPath, CancellationToken cancellationToken) {
            WasCalled = true;
            RepositoryRoot = repositoryRoot;
            BuildRoot = buildRoot;
            GeneratedCoreCppRootPath = generatedCoreCppRootPath;

            Directory.CreateDirectory(buildRoot);
            string executablePath = Path.Combine(buildRoot, "helengine_windows.exe");
            File.WriteAllText(executablePath, "fake executable");
            return executablePath;
        }
    }
}
