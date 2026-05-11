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
        GeneratedCoreRootPath = Path.Combine(Path.GetTempPath(), "helengine-windows-runtime-manifest-tests", Guid.NewGuid().ToString("N"));
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
        writer.Write(GeneratedCoreRootPath, manifest, graphicsOptionValues);

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
        writer.Write(GeneratedCoreRootPath, manifest, graphicsOptionValues);

        string runtimeRootPath = Path.Combine(GeneratedCoreRootPath, "runtime");
        string settingsHeaderPath = Path.Combine(runtimeRootPath, "runtime_player_settings_manifest.hpp");
        string settingsSourcePath = Path.Combine(runtimeRootPath, "runtime_player_settings_manifest.cpp");

        Assert.True(File.Exists(settingsHeaderPath));
        Assert.True(File.Exists(settingsSourcePath));

        string settingsSource = File.ReadAllText(settingsSourcePath);
        Assert.Contains("he_get_runtime_default_window_width", settingsSource, StringComparison.Ordinal);
        Assert.Contains("640", settingsSource, StringComparison.Ordinal);
        Assert.Contains("480", settingsSource, StringComparison.Ordinal);
    }
}
