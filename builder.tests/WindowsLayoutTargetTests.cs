using helengine.baseplatform.Builders;
using helengine.windows.builder;

namespace helengine.windows.builder.tests;

/// <summary>Checks that scene cooking uses the same dimensions consumed by Windows startup generation.</summary>
public sealed class WindowsLayoutTargetTests {
    /// <summary>A profile name cannot hide changes to its effective framebuffer dimensions.</summary>
    [Fact]
    public void SameProfileDifferentOptions_ResolveDifferentTargets() {
        IPlatformLayoutTargetProvider provider = Assert.IsAssignableFrom<IPlatformLayoutTargetProvider>(new WindowsPlatformAssetBuilder());
        PlatformLayoutTarget first = provider.ResolveLayoutTarget(new PlatformLayoutTargetRequest("profiler", "desktop",
            new Dictionary<string, string> { ["default-width"] = "1280", ["default-height"] = "720" }));
        PlatformLayoutTarget second = provider.ResolveLayoutTarget(new PlatformLayoutTargetRequest("profiler", "desktop",
            new Dictionary<string, string> { ["default-width"] = "640", ["default-height"] = "480" }));
        Assert.Equal(1280, first.Width);
        Assert.Equal(720, first.Height);
        Assert.Equal(640, second.Width);
        Assert.Equal(480, second.Height);
        Assert.False(first.HasDpi);
    }

    /// <summary>Missing required startup dimensions fail rather than becoming a resolution fallback.</summary>
    [Fact]
    public void MissingDimensions_FailsLikeNativeManifest() {
        IPlatformLayoutTargetProvider provider = Assert.IsAssignableFrom<IPlatformLayoutTargetProvider>(new WindowsPlatformAssetBuilder());
        Assert.Throws<InvalidOperationException>(() => provider.ResolveLayoutTarget(
            new PlatformLayoutTargetRequest("profiler", "desktop", new Dictionary<string, string>())));
    }
}
