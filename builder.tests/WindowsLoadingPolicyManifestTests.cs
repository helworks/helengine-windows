using System.Globalization;
using helengine.baseplatform.Manifest;
using helengine.windows.builder;

namespace helengine.windows.builder.tests;

/// <summary>Verifies Windows packaging preserves the final shared loading policy when rewriting its native manifests.</summary>
public sealed class WindowsLoadingPolicyManifestTests {
    /// <summary>Checks effective policy remains invariant and the emitted getters retain the packaged operational loading scene ID.</summary>
    [Fact]
    public void Write_preserves_loading_policy_when_replacing_startup_sources() {
        string root = FixtureRoot(nameof(Write_preserves_loading_policy_when_replacing_startup_sources));
        Directory.CreateDirectory(Path.Combine(root, "runtime"));
        File.WriteAllText(Path.Combine(root, "runtime", "runtime_startup_manifest.hpp"), "old startup header");
        PlatformBuildManifest manifest = CreateManifest();
        manifest.SceneLoadingBudgetMilliseconds = 2.25d;
        manifest.LoadingScreenSceneId = "SceneLoadingScreen";
        CultureInfo previous = CultureInfo.CurrentCulture;
        try {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("pt-BR");
            new WindowsRuntimeNativeManifestWriter().Write(root, manifest, GraphicsOptions());
        } finally { CultureInfo.CurrentCulture = previous; }
        string header = File.ReadAllText(Path.Combine(root, "runtime", "runtime_startup_manifest.hpp"));
        string source = File.ReadAllText(Path.Combine(root, "runtime", "runtime_startup_manifest.cpp"));
        Assert.Contains("double he_get_runtime_scene_loading_budget_milliseconds();", header);
        Assert.Contains("const char* he_get_runtime_loading_screen_scene_id();", header);
        Assert.Contains("kRuntimeSceneLoadingBudgetMilliseconds = 2.25;", source);
        Assert.Contains("kRuntimeLoadingScreenSceneId[] = \"SceneLoadingScreen\";", source);
    }

    /// <summary>Rejects malformed budgets rather than emitting invalid C++ or treating zero as an uncapped native load.</summary>
    [Theory]
    [InlineData(0d)]
    [InlineData(-1d)]
    [InlineData(double.NaN)]
    [InlineData(double.PositiveInfinity)]
    public void Write_rejects_invalid_effective_budget(double value) {
        PlatformBuildManifest manifest = CreateManifest();
        manifest.SceneLoadingBudgetMilliseconds = value;
        Assert.Throws<InvalidOperationException>(() => new WindowsRuntimeNativeManifestWriter().Write(
            FixtureRoot(nameof(Write_rejects_invalid_effective_budget)), manifest, GraphicsOptions()));
    }

    /// <summary>Checks a loading-screen ID cannot survive packaging if it does not name a real catalog entry.</summary>
    [Fact]
    public void Write_rejects_missing_loading_screen() {
        PlatformBuildManifest manifest = CreateManifest();
        manifest.LoadingScreenSceneId = "MissingScreen";
        Assert.Throws<InvalidOperationException>(() => new WindowsRuntimeNativeManifestWriter().Write(
            FixtureRoot(nameof(Write_rejects_missing_loading_screen)), manifest, GraphicsOptions()));
    }

    /// <summary>Uses fixed visible output roots, overwriting only this focused test's generated startup fixtures.</summary>
    static string FixtureRoot(string name) {
        return Path.Combine("C:/dev/helworks/builds/demodisc/windows/loading-policy-tests", name);
    }

    /// <summary>Creates a real operational two-scene manifest like the Windows bootstrap consumes.</summary>
    static PlatformBuildManifest CreateManifest() {
        return new PlatformBuildManifest(2, "project", "1", "1", "windows", "1", "DemoDiscMainMenu", [
            new PlatformBuildScene("DemoDiscMainMenu", "Menu", "cooked/scenes/menu.hasset", [], [new KeyValuePair<string, string>("cooked-relative-path", "cooked/scenes/menu.hasset")]),
            new PlatformBuildScene("SceneLoadingScreen", "Loading", "cooked/scenes/loading.hasset", [], [new KeyValuePair<string, string>("cooked-relative-path", "cooked/scenes/loading.hasset")])
        ], [], [], [], [], new PlatformContainerWritePlan(string.Empty, []), [], PlatformBuildRuntimeFeatureManifest.Empty);
    }

    /// <summary>Supplies existing graphics fields required by the production Windows manifest writer.</summary>
    static Dictionary<string, string> GraphicsOptions() {
        return new Dictionary<string, string> { ["default-width"] = "1280", ["default-height"] = "720" };
    }
}
