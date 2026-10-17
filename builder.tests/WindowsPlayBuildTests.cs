using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Targets;
using helengine.windows.builder;
using helengine.windows.builder.tests.Builders;

namespace helengine.windows.builder.tests;

/// <summary>Verifies Play stages cooked content without invoking the native build or overwriting packaged output.</summary>
[Collection(ProcessCurrentDirectoryCollection.Name)]
public sealed class WindowsPlayBuildTests {
    /// <summary>An assets-only request preserves the shipped player and generated C++ while replacing stale Play content.</summary>
    [Fact]
    public async Task AssetsOnly_StagesContentAndLeavesNativeOutputUntouched() {
        string root = Path.Combine(@"C:\temp\helengine-windows\tests", Guid.NewGuid().ToString("N"));
        string source = Path.Combine(root, "source");
        string working = Path.Combine(root, "working");
        string output = Path.Combine(root, "output");
        string generated = Path.Combine(root, "generated");
        string contentRoot = Path.Combine(working, WindowsPlayerContentLayout.ContentRootRelativePath);
        string previousDirectory = Directory.GetCurrentDirectory();
        try {
            Directory.CreateDirectory(Path.Combine(source, "cooked", "scenes"));
            Directory.CreateDirectory(output);
            Directory.CreateDirectory(generated);
            Directory.CreateDirectory(contentRoot);
            File.WriteAllText(Path.Combine(source, "cooked", "scenes", "menu.hasset"), "scene payload");
            File.WriteAllText(Path.Combine(output, "helengine_windows.exe"), "existing player");
            File.WriteAllText(Path.Combine(generated, "sentinel.cpp"), "existing generated source");
            File.WriteAllText(Path.Combine(contentRoot, "stale.hasset"), "obsolete content");
            Directory.SetCurrentDirectory(source);
            PlatformBuildManifest manifest = new(1, "project", "1.0.0", "1.0.0", "windows", "1.0.0", "menu",
                [new PlatformBuildScene("menu", "Menu", "cooked/scenes/menu.hasset", [], [])], [], [], [], [],
                new PlatformContainerWritePlan(string.Empty, []), [], PlatformBuildRuntimeFeatureManifest.Empty);
            PlatformBuildRequest request = new(manifest,
                [new PlatformBuildTargetVariant("windows", "windows", "windows", "debug")],
                [new PlatformCookProfile("debug", "Debug", new PlatformCookProfileCapabilities(
                    "windows", "raw", "rgba", "windows-scene-v1", PlatformSerializationEndianness.LittleEndian))],
                output, working, "debug", "directx11", "default", new Dictionary<string, string>(),
                new Dictionary<string, string>(), new Dictionary<string, string>(), generated,
                executionScope: PlatformBuildExecutionScope.AssetsOnly);
            RecordingDiagnosticReporter diagnostics = new();

            var report = await new WindowsPlatformAssetBuilder().BuildAsync(request, new RecordingProgressReporter(), diagnostics, CancellationToken.None);

            Assert.True(report.Succeeded);
            Assert.Empty(diagnostics.Diagnostics);
            Assert.Equal("scene payload", File.ReadAllText(Path.Combine(contentRoot, "cooked", "scenes", "menu.hasset")));
            Assert.False(File.Exists(Path.Combine(contentRoot, "stale.hasset")));
            Assert.Equal("existing player", File.ReadAllText(Path.Combine(output, "helengine_windows.exe")));
            Assert.Single(Directory.GetFiles(output));
            Assert.Equal("existing generated source", File.ReadAllText(Path.Combine(generated, "sentinel.cpp")));
            Assert.Single(Directory.GetFiles(generated));
            Assert.False(Directory.Exists(Path.Combine(working, "native")));
        } finally {
            Directory.SetCurrentDirectory(previousDirectory);
            Directory.Delete(root, true);
        }
    }
}
