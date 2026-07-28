namespace helengine.windows.builder;

/// <summary>
/// Identifies the supported native Windows player build configurations after editor profile validation.
/// </summary>
public enum WindowsNativeBuildProfile {
    /// <summary>
    /// Builds a debuggable native player without generated C++ profiling.
    /// </summary>
    Debug,

    /// <summary>
    /// Builds an optimized native player without generated C++ profiling.
    /// </summary>
    Release,

    /// <summary>
    /// Builds an optimized native player with symbols and generated C++ profiling enabled.
    /// </summary>
    Profiler
}
