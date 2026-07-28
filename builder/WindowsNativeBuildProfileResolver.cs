namespace helengine.windows.builder;

/// <summary>
/// Converts editor-selected Windows player profile identifiers into validated native CMake settings.
/// </summary>
internal static class WindowsNativeBuildProfileResolver {
    /// <summary>
    /// Resolves an editor-selected Windows player profile identifier.
    /// </summary>
    /// <param name="playerProfileId">Stable editor player profile identifier.</param>
    /// <returns>Validated native build configuration for the requested player profile.</returns>
    public static WindowsNativeBuildProfileResolution Resolve(string playerProfileId) {
        if (string.IsNullOrWhiteSpace(playerProfileId)) {
            throw new ArgumentException("Windows player build profile id must be provided.", nameof(playerProfileId));
        }

        if (string.Equals(playerProfileId, "debug", StringComparison.Ordinal)) {
            return Resolve(WindowsNativeBuildProfile.Debug);
        } else if (string.Equals(playerProfileId, "release", StringComparison.Ordinal)) {
            return Resolve(WindowsNativeBuildProfile.Release);
        } else if (string.Equals(playerProfileId, "profiler", StringComparison.Ordinal)) {
            return Resolve(WindowsNativeBuildProfile.Profiler);
        }

        throw new ArgumentException($"Unsupported Windows player build profile '{playerProfileId}'.", nameof(playerProfileId));
    }

    /// <summary>
    /// Resolves the CMake and artifact settings for an already validated typed Windows player profile.
    /// </summary>
    /// <param name="profile">Typed Windows player profile.</param>
    /// <returns>Native build configuration for the profile.</returns>
    public static WindowsNativeBuildProfileResolution Resolve(WindowsNativeBuildProfile profile) {
        return profile switch {
            WindowsNativeBuildProfile.Debug => new WindowsNativeBuildProfileResolution(profile, "Debug", false, false),
            WindowsNativeBuildProfile.Release => new WindowsNativeBuildProfileResolution(profile, "Release", false, false),
            WindowsNativeBuildProfile.Profiler => new WindowsNativeBuildProfileResolution(profile, "RelWithDebInfo", true, true),
            _ => throw new ArgumentOutOfRangeException(nameof(profile), profile, "Unsupported Windows native build profile.")
        };
    }
}
