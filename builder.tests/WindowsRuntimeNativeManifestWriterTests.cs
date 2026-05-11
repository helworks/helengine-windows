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

        WindowsRuntimeNativeManifestWriter writer = new();
        writer.Write(GeneratedCoreRootPath, manifest);

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
}
