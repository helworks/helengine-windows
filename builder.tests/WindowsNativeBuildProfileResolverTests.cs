using helengine.windows.builder;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Windows player profile resolver translates supported editor profile identifiers into typed native build settings.
/// </summary>
public sealed class WindowsNativeBuildProfileResolverTests {
    /// <summary>
    /// Verifies each supported player profile has the intended CMake configuration and profiler artifact requirements.
    /// </summary>
    [Theory]
    [InlineData("debug", WindowsNativeBuildProfile.Debug, "Debug", false, false)]
    [InlineData("release", WindowsNativeBuildProfile.Release, "Release", false, false)]
    [InlineData("profiler", WindowsNativeBuildProfile.Profiler, "RelWithDebInfo", true, true)]
    public void Resolve_maps_supported_player_profiles(string profileId, WindowsNativeBuildProfile expectedProfile, string expectedCmakeBuildType, bool expectedProfilerEnabled, bool expectedPdbRequired) {
        WindowsNativeBuildProfileResolution resolution = WindowsNativeBuildProfileResolver.Resolve(profileId);

        Assert.Equal(expectedProfile, resolution.Profile);
        Assert.Equal(expectedCmakeBuildType, resolution.CmakeBuildType);
        Assert.Equal(expectedProfilerEnabled, resolution.ProfilerEnabled);
        Assert.Equal(expectedPdbRequired, resolution.PdbRequired);
    }

    /// <summary>
    /// Verifies absent or unsupported editor profile identifiers cannot reach the native CMake invocation.
    /// </summary>
    [Theory]
    [InlineData("")]
    [InlineData("mobile")]
    public void Resolve_rejects_unknown_or_empty_player_profiles(string profileId) {
        Assert.Throws<ArgumentException>(() => WindowsNativeBuildProfileResolver.Resolve(profileId));
    }
}
