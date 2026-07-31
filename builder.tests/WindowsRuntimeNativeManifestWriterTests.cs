using System.Text.Json;
using helengine.baseplatform.Manifest;
using helengine.windows.builder;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Windows runtime manifest writer emits the native startup and scene-catalog sources required by the player bootstrap.
/// </summary>
public sealed class WindowsRuntimeNativeManifestWriterTests : IDisposable {
    /// <summary>
    /// Temporary generated-core root used by each manifest-writer test.
    /// </summary>
    readonly string GeneratedCoreRootPath;

    /// <summary>
    /// Initializes one isolated generated-core root for manifest-writer verification.
    /// </summary>
    public WindowsRuntimeNativeManifestWriterTests() {
        GeneratedCoreRootPath = WindowsTestOutputPathResolver.CreateUniqueRoot("runtime-manifest-writer");
        Directory.CreateDirectory(GeneratedCoreRootPath);
    }

    /// <summary>
    /// Deletes the generated-core root after each test completes.
    /// </summary>
    public void Dispose() {
        if (Directory.Exists(GeneratedCoreRootPath)) {
            Directory.Delete(GeneratedCoreRootPath, true);
        }
    }

    /// <summary>
    /// Ensures the writer emits the runtime scene catalog alongside the startup manifest so the Windows player can initialize the runtime scene manager.
    /// </summary>
    [Fact]
    public void Write_when_manifest_contains_scenes_emits_runtime_scene_catalog_manifest() {
        PlatformBuildManifest manifest = new(
            2,
            "project",
            "1.0.0",
            "1.0.0",
            "scenes/DemoDiscMainMenu.helen",
            [
                new PlatformBuildScene(
                    "scenes/DemoDiscMainMenu.helen",
                    "DemoDiscMainMenu",
                    "cooked/scenes/DemoDiscMainMenu.hasset",
                    [],
                    [new KeyValuePair<string, string>("cooked-relative-path", "cooked/scenes/DemoDiscMainMenu.hasset")]),
                new PlatformBuildScene(
                    "scenes/rendering/cube_test.helen",
                    "cube_test",
                    "cooked/scenes/rendering/cube_test.hasset",
                    [],
                    [new KeyValuePair<string, string>("cooked-relative-path", "cooked/scenes/rendering/cube_test.hasset")])
            ],
            [],
            [],
            [],
            [],
            new PlatformContainerWritePlan(string.Empty, []));
        Dictionary<string, string> graphicsOptionValues = new(StringComparer.OrdinalIgnoreCase) {
            ["default-width"] = "1280",
            ["default-height"] = "720"
        };

        WindowsRuntimeNativeManifestWriter writer = new();
        writer.Write(GeneratedCoreRootPath, manifest, graphicsOptionValues, new Dictionary<string, string>());

        string runtimeRootPath = Path.Combine(GeneratedCoreRootPath, "runtime");
        string sceneCatalogHeaderPath = Path.Combine(runtimeRootPath, "runtime_scene_catalog_manifest.hpp");
        string sceneCatalogSourcePath = Path.Combine(runtimeRootPath, "runtime_scene_catalog_manifest.cpp");

        Assert.True(File.Exists(sceneCatalogHeaderPath));
        Assert.True(File.Exists(sceneCatalogSourcePath));

        string sceneCatalogSource = File.ReadAllText(sceneCatalogSourcePath);
        Assert.Contains("he_runtime_scene_catalog_entries", sceneCatalogSource, StringComparison.Ordinal);
        Assert.Contains("scenes/DemoDiscMainMenu.helen", sceneCatalogSource, StringComparison.Ordinal);
        Assert.Contains("cooked/scenes/rendering/cube_test.hasset", sceneCatalogSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the writer emits the runtime player-settings manifest so the native host can honor the selected default window resolution.
    /// </summary>
    [Fact]
    public void Write_when_graphics_options_define_default_resolution_emits_runtime_player_settings_manifest() {
        PlatformBuildManifest manifest = new(
            2,
            "project",
            "1.0.0",
            "1.0.0",
            "DemoDiscMainMenu",
            [
                new PlatformBuildScene(
                    "DemoDiscMainMenu",
                    "DemoDiscMainMenu",
                    "cooked/scenes/DemoDiscMainMenu.hasset",
                    [],
                    [new KeyValuePair<string, string>("cooked-relative-path", "cooked/scenes/DemoDiscMainMenu.hasset")])
            ],
            [],
            [],
            [],
            [],
            new PlatformContainerWritePlan(string.Empty, []));
        Dictionary<string, string> graphicsOptionValues = new(StringComparer.OrdinalIgnoreCase) {
            ["default-width"] = "640",
            ["default-height"] = "480"
        };

        WindowsRuntimeNativeManifestWriter writer = new();
        writer.Write(GeneratedCoreRootPath, manifest, graphicsOptionValues, new Dictionary<string, string>());

        string runtimeRootPath = Path.Combine(GeneratedCoreRootPath, "runtime");
        string settingsHeaderPath = Path.Combine(runtimeRootPath, "runtime_player_settings_manifest.hpp");
        string settingsSourcePath = Path.Combine(runtimeRootPath, "player_settings.cpp");

        Assert.True(File.Exists(settingsHeaderPath));
        Assert.True(File.Exists(settingsSourcePath));

        string settingsSource = File.ReadAllText(settingsSourcePath);
        Assert.Contains("he_get_runtime_default_window_width", settingsSource, StringComparison.Ordinal);
        Assert.Contains("640", settingsSource, StringComparison.Ordinal);
        Assert.Contains("480", settingsSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures runtime player profile seeding keeps using the generated deployment defaults.
    /// </summary>
    [Fact]
    public void Write_when_graphics_options_define_default_resolution_emits_runtime_profile_seed_values() {
        PlatformBuildManifest manifest = new(
            2,
            "project",
            "1.0.0",
            "1.0.0",
            "DemoDiscMainMenu",
            [
                new PlatformBuildScene(
                    "DemoDiscMainMenu",
                    "DemoDiscMainMenu",
                    "cooked/scenes/DemoDiscMainMenu.hasset",
                    [],
                    [new KeyValuePair<string, string>("cooked-relative-path", "cooked/scenes/DemoDiscMainMenu.hasset")])
            ],
            [],
            [],
            [],
            [],
            new PlatformContainerWritePlan(string.Empty, []));
        Dictionary<string, string> graphicsOptionValues = new(StringComparer.OrdinalIgnoreCase) {
            ["default-width"] = "640",
            ["default-height"] = "480"
        };

        WindowsRuntimeNativeManifestWriter writer = new();
        writer.Write(GeneratedCoreRootPath, manifest, graphicsOptionValues, new Dictionary<string, string>());

        string runtimeRootPath = Path.Combine(GeneratedCoreRootPath, "runtime");
        string settingsSourcePath = Path.Combine(runtimeRootPath, "player_settings.cpp");
        string settingsSource = File.ReadAllText(settingsSourcePath);

        Assert.Contains("kRuntimeDefaultWindowWidth = 640", settingsSource, StringComparison.Ordinal);
        Assert.Contains("kRuntimeDefaultWindowHeight = 480", settingsSource, StringComparison.Ordinal);
        Assert.Contains("he_get_runtime_default_window_width", settingsSource, StringComparison.Ordinal);
        Assert.Contains("he_get_runtime_default_window_height", settingsSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures HelPhysics emits one exclusive native selection and matching packaged JSON values.
    /// </summary>
    [Fact]
    public void Write_when_helphysics_is_selected_emits_matching_native_and_json_manifests() {
        PlatformBuildManifest manifest = CreateMinimalManifest();
        Dictionary<string, string> graphicsOptionValues = CreateGraphicsOptions();
        Dictionary<string, string> codegenOptionValues = new Dictionary<string, string>(StringComparer.Ordinal) {
            ["physics-runtime-backend"] = "helphysics",
            ["physics-fixed-step-hertz"] = "20",
            ["physics-velocity-iterations"] = "4",
            ["physics-penetration-correction-passes"] = "1"
        };

        WindowsRuntimeNativeManifestWriter writer = new();
        WindowsPhysicsRuntimeConfiguration configuration = writer.Write(
            GeneratedCoreRootPath,
            manifest,
            graphicsOptionValues,
            codegenOptionValues);

        string runtimeRootPath = Path.Combine(GeneratedCoreRootPath, "runtime");
        string header = File.ReadAllText(Path.Combine(runtimeRootPath, "runtime_physics_backend_manifest.hpp"));
        Assert.True(configuration.UsesHelPhysics);
        Assert.Contains("HELENGINE_RUNTIME_PHYSICS_BACKEND_HELPHYSICS", header, StringComparison.Ordinal);
        Assert.DoesNotContain("HELENGINE_RUNTIME_PHYSICS_BACKEND_BEPU", header, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_RUNTIME_PHYSICS_BACKEND_ID \"helphysics\"", header, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_RUNTIME_PHYSICS_FIXED_STEP_HERTZ 20", header, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_RUNTIME_PHYSICS_VELOCITY_ITERATIONS 4", header, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_RUNTIME_PHYSICS_PENETRATION_CORRECTION_PASSES 1", header, StringComparison.Ordinal);

        using JsonDocument json = JsonDocument.Parse(File.ReadAllText(Path.Combine(runtimeRootPath, "runtime-physics-backend.json")));
        Assert.Equal("helphysics", json.RootElement.GetProperty("backend").GetString());
        Assert.Equal(20, json.RootElement.GetProperty("fixedStepHertz").GetInt32());
        Assert.Equal(4, json.RootElement.GetProperty("velocityIterations").GetInt32());
        Assert.Equal(1, json.RootElement.GetProperty("penetrationCorrectionPasses").GetInt32());
    }

    /// <summary>
    /// Ensures default BEPU output contains only its own native selection and backend identity.
    /// </summary>
    [Fact]
    public void Write_when_backend_is_absent_emits_bepu_only_manifests() {
        WindowsRuntimeNativeManifestWriter writer = new();

        WindowsPhysicsRuntimeConfiguration configuration = writer.Write(
            GeneratedCoreRootPath,
            CreateMinimalManifest(),
            CreateGraphicsOptions(),
            new Dictionary<string, string>());

        string runtimeRootPath = Path.Combine(GeneratedCoreRootPath, "runtime");
        string header = File.ReadAllText(Path.Combine(runtimeRootPath, "runtime_physics_backend_manifest.hpp"));
        Assert.False(configuration.UsesHelPhysics);
        Assert.Contains("HELENGINE_RUNTIME_PHYSICS_BACKEND_BEPU", header, StringComparison.Ordinal);
        Assert.DoesNotContain("HELENGINE_RUNTIME_PHYSICS_BACKEND_HELPHYSICS", header, StringComparison.Ordinal);
        Assert.DoesNotContain("HELENGINE_RUNTIME_PHYSICS_FIXED_STEP_HERTZ", header, StringComparison.Ordinal);
        Assert.DoesNotContain("HELENGINE_RUNTIME_PHYSICS_VELOCITY_ITERATIONS", header, StringComparison.Ordinal);
        Assert.DoesNotContain("HELENGINE_RUNTIME_PHYSICS_PENETRATION_CORRECTION_PASSES", header, StringComparison.Ordinal);

        using JsonDocument json = JsonDocument.Parse(File.ReadAllText(Path.Combine(runtimeRootPath, "runtime-physics-backend.json")));
        JsonProperty backendProperty = Assert.Single(json.RootElement.EnumerateObject());
        Assert.Equal("backend", backendProperty.Name);
        Assert.Equal("bepu", backendProperty.Value.GetString());
    }

    /// <summary>
    /// Creates the smallest valid scene manifest required by native runtime manifest generation.
    /// </summary>
    /// <returns>Manifest containing one startup scene and no optional payloads.</returns>
    static PlatformBuildManifest CreateMinimalManifest() {
        return new PlatformBuildManifest(
            2,
            "project",
            "1.0.0",
            "1.0.0",
            "startup",
            [
                new PlatformBuildScene(
                    "startup",
                    "Startup",
                    "cooked/scenes/startup.hasset",
                    [],
                    [new KeyValuePair<string, string>("cooked-relative-path", "cooked/scenes/startup.hasset")])
            ],
            [],
            [],
            [],
            [],
            new PlatformContainerWritePlan(string.Empty, []));
    }

    /// <summary>
    /// Creates valid default Windows graphics settings required by the writer.
    /// </summary>
    /// <returns>Default width and height values.</returns>
    static Dictionary<string, string> CreateGraphicsOptions() {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
            ["default-width"] = "1280",
            ["default-height"] = "720"
        };
    }
}
