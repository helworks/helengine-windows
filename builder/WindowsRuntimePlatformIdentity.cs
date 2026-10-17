namespace helengine.windows.builder;

/// <summary>
/// Single source of truth for the <c>PlatformInfo</c> identity the native Windows runtime reports to the core.
/// The native host builds its <c>PlatformInfo</c> from <c>he_get_runtime_platform_name</c> and
/// <c>he_get_runtime_platform_version</c>, which <see cref="WindowsRuntimeNativeManifestWriter"/> generates from the
/// build manifest. The editor fills the manifest name from the builder descriptor's target platform id, which
/// <see cref="WindowsPlatformAssetBuilder"/> takes from <see cref="Name"/>, so the native runtime and the editor Play
/// capability both read this class.
/// </summary>
public static class WindowsRuntimePlatformIdentity {
    /// <summary>
    /// Platform name the native Windows runtime reports through <c>PlatformInfo.Name</c>; it is also the builder
    /// descriptor's target platform id.
    /// </summary>
    public const string Name = "windows";

    /// <summary>
    /// Platform version the native Windows runtime reports through <c>PlatformInfo.Version</c> when the project's Windows
    /// profile keeps the editor's default profile version. The editor stamps the project's per-platform profile version
    /// into the build manifest, so a project that edits that version ships a different value than Play reports.
    /// </summary>
    public const string Version = "1.0.0";
}
