namespace helengine.windows.builder;

/// <summary>
/// Describes the CMake and artifact requirements associated with one validated native Windows build profile.
/// </summary>
internal sealed class WindowsNativeBuildProfileResolution {
    /// <summary>
    /// Initializes the native configuration selected for a supported Windows player profile.
    /// </summary>
    /// <param name="profile">Validated typed native Windows profile.</param>
    /// <param name="cmakeBuildType">Single-configuration CMake build type passed to Ninja.</param>
    /// <param name="profilerEnabled">Whether the native Windows profiler integration is enabled.</param>
    /// <param name="pdbRequired">Whether the output executable must have an accompanying PDB.</param>
    public WindowsNativeBuildProfileResolution(WindowsNativeBuildProfile profile, string cmakeBuildType, bool profilerEnabled, bool pdbRequired) {
        if (string.IsNullOrWhiteSpace(cmakeBuildType)) {
            throw new ArgumentException("CMake build type must be provided.", nameof(cmakeBuildType));
        }

        Profile = profile;
        CmakeBuildType = cmakeBuildType;
        ProfilerEnabled = profilerEnabled;
        PdbRequired = pdbRequired;
    }

    /// <summary>
    /// Gets the validated typed native Windows build profile.
    /// </summary>
    public WindowsNativeBuildProfile Profile { get; }

    /// <summary>
    /// Gets the single-configuration CMake build type used by Ninja.
    /// </summary>
    public string CmakeBuildType { get; }

    /// <summary>
    /// Gets whether the native profiler client must be enabled.
    /// </summary>
    public bool ProfilerEnabled { get; }

    /// <summary>
    /// Gets whether a PDB must accompany the player executable.
    /// </summary>
    public bool PdbRequired { get; }
}
