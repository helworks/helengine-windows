using System.Text.Json;
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
/// Verifies profiler-only native artifacts are packaged with the Windows player and recorded for build traceability.
/// </summary>
[Collection(ProcessCurrentDirectoryCollection.Name)]
public sealed class WindowsProfilerArtifactPackagingTests {
    /// <summary>
    /// Verifies profiler packages include the generated scope manifest and native symbols and record both artifacts.
    /// </summary>
    [Fact]
    public async Task BuildAsync_profiler_copies_manifest_and_pdb_and_records_them() {
        using ProfilerArtifactPackagingFixture fixture = new("profiler", true, true);

        PlatformBuildReport report = await fixture.BuildAsync();

        string packagedManifestPath = Path.Combine(fixture.OutputRoot, "runtime", "generated_profiler_manifest.json");
        string packagedPdbPath = Path.Combine(fixture.OutputRoot, "helengine_windows.pdb");
        string buildManifestPath = Path.Combine(fixture.WorkingRoot, "windows-build-manifest.json");
        Assert.True(report.Succeeded);
        Assert.True(File.Exists(packagedManifestPath));
        Assert.True(File.Exists(packagedPdbPath));

        using JsonDocument buildManifest = JsonDocument.Parse(File.ReadAllText(buildManifestPath));
        JsonElement profilerArtifacts = buildManifest.RootElement.GetProperty("ProfilerArtifacts");
        Assert.Equal(2, profilerArtifacts.GetArrayLength());
        Assert.Contains(profilerArtifacts.EnumerateArray(), artifact => artifact.GetProperty("OutputPath").GetString() == packagedManifestPath);
        Assert.Contains(profilerArtifacts.EnumerateArray(), artifact => artifact.GetProperty("OutputPath").GetString() == packagedPdbPath);
    }

    /// <summary>
    /// Verifies non-profiler packages omit profiler-only artifacts even when a native executor reports a PDB.
    /// </summary>
    /// <param name="profileId">Non-profiler Windows profile whose package must remain artifact-free.</param>
    [Theory]
    [InlineData("debug")]
    [InlineData("release")]
    public async Task BuildAsync_non_profiler_omits_manifest_and_pdb(string profileId) {
        using ProfilerArtifactPackagingFixture fixture = new(profileId, true, true);

        PlatformBuildReport report = await fixture.BuildAsync();

        Assert.True(report.Succeeded);
        Assert.False(File.Exists(Path.Combine(fixture.OutputRoot, "runtime", "generated_profiler_manifest.json")));
        Assert.False(File.Exists(Path.Combine(fixture.OutputRoot, "helengine_windows.pdb")));
    }

    /// <summary>
    /// Verifies an ordinary release package includes and records the exact HelPhysics runtime backend manifest.
    /// </summary>
    [Fact]
    public async Task BuildAsync_release_helphysics_packages_and_records_runtime_manifest() {
        using ProfilerArtifactPackagingFixture fixture = new(
            "release",
            true,
            true,
            "helphysics",
            "valid");

        PlatformBuildReport report = await fixture.BuildAsync();

        string packagedManifestPath = Path.Combine(fixture.OutputRoot, "runtime", "runtime-physics-backend.json");
        Assert.True(report.Succeeded);
        using JsonDocument runtimeManifest = JsonDocument.Parse(File.ReadAllText(packagedManifestPath));
        Assert.Equal("helphysics", runtimeManifest.RootElement.GetProperty("backend").GetString());
        Assert.Equal(20, runtimeManifest.RootElement.GetProperty("fixedStepHertz").GetInt32());
        Assert.Equal(4, runtimeManifest.RootElement.GetProperty("velocityIterations").GetInt32());
        Assert.Equal(1, runtimeManifest.RootElement.GetProperty("penetrationCorrectionPasses").GetInt32());

        string buildManifestPath = Path.Combine(fixture.WorkingRoot, "windows-build-manifest.json");
        using JsonDocument buildManifest = JsonDocument.Parse(File.ReadAllText(buildManifestPath));
        JsonElement runtimeArtifacts = buildManifest.RootElement.GetProperty("RuntimeArtifacts");
        JsonElement runtimeArtifact = Assert.Single(runtimeArtifacts.EnumerateArray());
        Assert.Equal("runtime-physics-backend-manifest", runtimeArtifact.GetProperty("ItemId").GetString());
        Assert.Equal(packagedManifestPath, runtimeArtifact.GetProperty("OutputPath").GetString());
    }

    /// <summary>
    /// Verifies packaging fails when native compilation does not leave the required runtime physics manifest available.
    /// </summary>
    [Fact]
    public async Task BuildAsync_reports_missing_runtime_physics_manifest() {
        using ProfilerArtifactPackagingFixture fixture = new(
            "release",
            true,
            true,
            "bepu",
            "missing");

        PlatformBuildReport report = await fixture.BuildAsync();

        PlatformBuildDiagnostic diagnostic = Assert.Single(report.Diagnostics);
        Assert.False(report.Succeeded);
        Assert.Equal("WINBUILD003", diagnostic.Code);
        Assert.Contains("runtime-physics-backend.json", diagnostic.Message, StringComparison.Ordinal);
        Assert.Contains("missing", diagnostic.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// Verifies packaging fails when generated runtime metadata contradicts the backend compiled into the native player.
    /// </summary>
    [Fact]
    public async Task BuildAsync_reports_conflicting_runtime_physics_manifest() {
        using ProfilerArtifactPackagingFixture fixture = new(
            "release",
            true,
            true,
            "helphysics",
            "conflicting");

        PlatformBuildReport report = await fixture.BuildAsync();

        PlatformBuildDiagnostic diagnostic = Assert.Single(report.Diagnostics);
        Assert.False(report.Succeeded);
        Assert.Equal("WINBUILD003", diagnostic.Code);
        Assert.Contains("runtime physics backend manifest", diagnostic.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// Verifies profiler packages report a clear failure when generated C++ did not produce its scope manifest.
    /// </summary>
    [Fact]
    public async Task BuildAsync_profiler_reports_missing_generated_manifest() {
        using ProfilerArtifactPackagingFixture fixture = new("profiler", false, true);

        PlatformBuildReport report = await fixture.BuildAsync();

        PlatformBuildDiagnostic diagnostic = Assert.Single(report.Diagnostics);
        Assert.False(report.Succeeded);
        Assert.Equal("WINBUILD003", diagnostic.Code);
        Assert.Contains("generated_profiler_manifest.json", diagnostic.Message, StringComparison.Ordinal);
        Assert.Contains("profiler", diagnostic.Message, StringComparison.OrdinalIgnoreCase);
        Assert.False(File.Exists(Path.Combine(fixture.OutputRoot, "runtime", "runtime-physics-backend.json")));
        string buildManifestPath = Path.Combine(fixture.WorkingRoot, "windows-build-manifest.json");
        using JsonDocument buildManifest = JsonDocument.Parse(File.ReadAllText(buildManifestPath));
        Assert.Empty(buildManifest.RootElement.GetProperty("RuntimeArtifacts").EnumerateArray());
    }

    /// <summary>
    /// Verifies profiler packages report a clear failure when the native build does not produce symbols.
    /// </summary>
    [Fact]
    public async Task BuildAsync_profiler_reports_missing_pdb() {
        using ProfilerArtifactPackagingFixture fixture = new("profiler", true, false);

        PlatformBuildReport report = await fixture.BuildAsync();

        PlatformBuildDiagnostic diagnostic = Assert.Single(report.Diagnostics);
        Assert.False(report.Succeeded);
        Assert.Equal("WINBUILD003", diagnostic.Code);
        Assert.Contains("PDB", diagnostic.Message, StringComparison.Ordinal);
        Assert.Contains("profiler", diagnostic.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// Creates a disposable build package with optional profiler artifacts for one profile-specific package test.
    /// </summary>
    sealed class ProfilerArtifactPackagingFixture : IDisposable {
        /// <summary>
        /// Initializes the build package and optional generated/native profiler artifacts.
        /// </summary>
        /// <param name="profileId">Windows player profile selected for the package.</param>
        /// <param name="createGeneratedManifest">Whether generated C++ output contains the profiling manifest.</param>
        /// <param name="createNativePdb">Whether the synthetic native build contains symbols.</param>
        public ProfilerArtifactPackagingFixture(string profileId, bool createGeneratedManifest, bool createNativePdb)
            : this(profileId, createGeneratedManifest, createNativePdb, "bepu", "valid") {
        }

        /// <summary>
        /// Initializes a build package with explicit physics backend and post-native manifest state.
        /// </summary>
        /// <param name="profileId">Windows player profile selected for the package.</param>
        /// <param name="createGeneratedManifest">Whether generated C++ output contains the profiling manifest.</param>
        /// <param name="createNativePdb">Whether the synthetic native build contains symbols.</param>
        /// <param name="physicsBackendId">Physics backend selected by code-generation settings.</param>
        /// <param name="runtimePhysicsManifestState">Synthetic post-native manifest state: valid, missing, or conflicting.</param>
        public ProfilerArtifactPackagingFixture(
            string profileId,
            bool createGeneratedManifest,
            bool createNativePdb,
            string physicsBackendId,
            string runtimePhysicsManifestState) {
            RootPath = WindowsTestOutputPathResolver.CreateUniqueRoot("runtime-artifact-packaging");
            OutputRoot = Path.Combine(RootPath, "out");
            WorkingRoot = Path.Combine(RootPath, "builder");
            ProjectRoot = Path.Combine(RootPath, "project");
            GeneratedCoreRoot = Path.Combine(RootPath, "generated-core");
            string scenePath = Path.Combine(ProjectRoot, "scenes", "main-menu.hasset");
            Directory.CreateDirectory(Path.GetDirectoryName(scenePath)!);
            Directory.CreateDirectory(GeneratedCoreRoot);
            File.WriteAllText(scenePath, "scene payload");

            if (createGeneratedManifest) {
                string generatedManifestPath = Path.Combine(GeneratedCoreRoot, "runtime", "generated_profiler_manifest.json");
                Directory.CreateDirectory(Path.GetDirectoryName(generatedManifestPath)!);
                File.WriteAllText(generatedManifestPath, "{ \"scopes\": [] }");
            }

            PhysicsBackendId = physicsBackendId;
            NativeBuildExecutor = new PackagingNativeBuildExecutor(createNativePdb, runtimePhysicsManifestState);
            Request = CreateRequest(profileId);
        }

        /// <summary>
        /// Gets the working root that contains builder-owned traceability output.
        /// </summary>
        public string WorkingRoot { get; }

        /// <summary>
        /// Gets the final player package output root.
        /// </summary>
        public string OutputRoot { get; }

        /// <summary>
        /// Gets the project root used as the staged package current directory.
        /// </summary>
        public string ProjectRoot { get; }

        /// <summary>
        /// Gets the generated C++ root consumed by the native player build.
        /// </summary>
        public string GeneratedCoreRoot { get; }

        /// <summary>
        /// Gets the synthetic native builder used by the package flow.
        /// </summary>
        PackagingNativeBuildExecutor NativeBuildExecutor { get; }

        /// <summary>
        /// Gets the physics backend serialized into this fixture's code-generation settings.
        /// </summary>
        string PhysicsBackendId { get; }

        /// <summary>
        /// Gets the resolved platform request used by this fixture.
        /// </summary>
        PlatformBuildRequest Request { get; }

        /// <summary>
        /// Gets the temporary root deleted when the fixture is disposed.
        /// </summary>
        string RootPath { get; }

        /// <summary>
        /// Runs the builder while the staged project directory is active.
        /// </summary>
        /// <returns>The final package report.</returns>
        public async Task<PlatformBuildReport> BuildAsync() {
            string previousDirectory = Directory.GetCurrentDirectory();
            try {
                Directory.SetCurrentDirectory(ProjectRoot);
                WindowsPlatformAssetBuilder builder = new(NativeBuildExecutor);
                return await builder.BuildAsync(Request, new RecordingProgressReporter(), new RecordingDiagnosticReporter(), CancellationToken.None);
            } finally {
                Directory.SetCurrentDirectory(previousDirectory);
            }
        }

        /// <summary>
        /// Deletes the temporary package fixture after its test completes.
        /// </summary>
        public void Dispose() {
            if (Directory.Exists(RootPath)) {
                Directory.Delete(RootPath, true);
            }
        }

        /// <summary>
        /// Creates the smallest valid platform build request for the selected profile.
        /// </summary>
        /// <param name="profileId">Selected Windows profile identifier.</param>
        /// <returns>A package request with one scene payload.</returns>
        PlatformBuildRequest CreateRequest(string profileId) {
            PlatformBuildManifest manifest = new(
                1,
                "project",
                "1.0.0",
                "1.0.0",
                [new PlatformBuildScene("startup", "Startup", "scenes/main-menu.hasset", [], [new KeyValuePair<string, string>("cooked-relative-path", "scenes/main-menu.hasset")])],
                []);
            Dictionary<string, string> codegenOptions = new(StringComparer.Ordinal) {
                ["codegen-generated-function-profiling"] = profileId == "profiler" ? "true" : "false",
                ["physics-runtime-backend"] = PhysicsBackendId
            };
            if (string.Equals(PhysicsBackendId, "helphysics", StringComparison.OrdinalIgnoreCase)) {
                codegenOptions["physics-fixed-step-hertz"] = "20";
                codegenOptions["physics-velocity-iterations"] = "4";
                codegenOptions["physics-penetration-correction-passes"] = "1";
            }

            return new PlatformBuildRequest(
                manifest,
                [new PlatformBuildTargetVariant("windows-default", "windows", "windows", profileId)],
                [new PlatformCookProfile(profileId, profileId, new PlatformCookProfileCapabilities("windows", "raw", "rgba", "windows-scene-v1", PlatformSerializationEndianness.LittleEndian))],
                OutputRoot,
                WorkingRoot,
                profileId,
                "directx11",
                string.Empty,
                new Dictionary<string, string>(),
                new Dictionary<string, string> {
                    ["default-width"] = "1280",
                    ["default-height"] = "720"
                },
                codegenOptions,
                GeneratedCoreRoot);
        }
    }

    /// <summary>
    /// Produces predictable executable and optional PDB artifacts without invoking CMake.
    /// </summary>
    sealed class PackagingNativeBuildExecutor : IWindowsNativeBuildExecutor {
        /// <summary>
        /// Initializes a synthetic native build with optional symbols.
        /// </summary>
        /// <param name="createPdb">Whether the build should produce a PDB beside its executable.</param>
        /// <param name="runtimePhysicsManifestState">Post-native runtime physics manifest state to synthesize.</param>
        public PackagingNativeBuildExecutor(bool createPdb, string runtimePhysicsManifestState) {
            if (runtimePhysicsManifestState != "valid" &&
                runtimePhysicsManifestState != "missing" &&
                runtimePhysicsManifestState != "conflicting") {
                throw new ArgumentOutOfRangeException(
                    nameof(runtimePhysicsManifestState),
                    runtimePhysicsManifestState,
                    "Unknown synthetic runtime physics manifest state.");
            }

            CreatePdb = createPdb;
            RuntimePhysicsManifestState = runtimePhysicsManifestState;
        }

        /// <summary>
        /// Gets whether synthetic builds emit a PDB.
        /// </summary>
        bool CreatePdb { get; }

        /// <summary>
        /// Gets the post-native runtime physics manifest mutation applied by this executor.
        /// </summary>
        string RuntimePhysicsManifestState { get; }

        /// <summary>
        /// Creates the requested synthetic native player artifacts.
        /// </summary>
        /// <param name="repositoryRoot">Unused repository root supplied by the builder.</param>
        /// <param name="buildRoot">Native build output root supplied by the builder.</param>
        /// <param name="generatedCoreCppRootPath">Unused generated C++ root supplied by the builder.</param>
        /// <param name="stagedCodeRootPath">Unused staged code root supplied by the builder.</param>
        /// <param name="profile">Unused typed native profile supplied by the builder.</param>
        /// <param name="cancellationToken">Unused cancellation token supplied by the builder.</param>
        /// <returns>The executable and optional PDB paths produced by this synthetic build.</returns>
        public WindowsNativeBuildResult Build(string repositoryRoot, string buildRoot, string generatedCoreCppRootPath, string stagedCodeRootPath, WindowsNativeBuildProfile profile, CancellationToken cancellationToken) {
            Directory.CreateDirectory(buildRoot);
            string executablePath = Path.Combine(buildRoot, "helengine_windows.exe");
            string pdbPath = Path.ChangeExtension(executablePath, ".pdb");
            File.WriteAllText(executablePath, "synthetic executable");
            if (CreatePdb) {
                File.WriteAllText(pdbPath, "synthetic symbols");
            }

            string runtimePhysicsManifestPath = Path.Combine(
                generatedCoreCppRootPath,
                "runtime",
                "runtime-physics-backend.json");
            if (RuntimePhysicsManifestState == "missing") {
                File.Delete(runtimePhysicsManifestPath);
            } else if (RuntimePhysicsManifestState == "conflicting") {
                File.WriteAllText(runtimePhysicsManifestPath, "{ \"backend\": \"bepu\" }");
            }

            return new WindowsNativeBuildResult(executablePath, CreatePdb ? pdbPath : string.Empty);
        }
    }

}
