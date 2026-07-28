namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the native CMake project isolates the Tracy client to the profiler player profile.
/// </summary>
public sealed class WindowsProfilerCMakeIntegrationTests {
    /// <summary>
    /// Verifies the CMake project selects Tracy only when the explicit profiler profile is requested.
    /// </summary>
    [Fact]
    public void CMakeLists_gates_tracy_client_on_explicit_profiler_profile() {
        string cmakeSource = File.ReadAllText(Path.Combine(ResolveWindowsRepositoryRootPath(), "CMakeLists.txt"));

        Assert.Contains("set(HELENGINE_WINDOWS_NATIVE_PROFILE \"Debug\" CACHE STRING", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("set_property(CACHE HELENGINE_WINDOWS_NATIVE_PROFILE PROPERTY STRINGS Debug Release Profiler)", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("if(HELENGINE_WINDOWS_NATIVE_PROFILE STREQUAL \"Profiler\")", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("add_subdirectory(\"${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy\"", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("target_compile_definitions(helengine_windows PRIVATE TRACY_ENABLE)", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("target_link_libraries(helengine_windows PRIVATE TracyClient)", cmakeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the Windows native build command forwards the typed player profile instead of a profiler-only boolean.
    /// </summary>
    [Fact]
    public void BuildConfigureArguments_forwards_native_profile_to_cmake() {
        string profilerArguments = InvokePrivateCommandBuilder(WindowsNativeBuildProfile.Profiler);
        string releaseArguments = InvokePrivateCommandBuilder(WindowsNativeBuildProfile.Release);

        Assert.Contains("-DHELENGINE_WINDOWS_NATIVE_PROFILE=Profiler", profilerArguments, StringComparison.Ordinal);
        Assert.Contains("-DHELENGINE_WINDOWS_NATIVE_PROFILE=Release", releaseArguments, StringComparison.Ordinal);
        Assert.DoesNotContain("-DHELENGINE_WINDOWS_PROFILER=", profilerArguments, StringComparison.Ordinal);
        Assert.DoesNotContain("-DHELENGINE_WINDOWS_PROFILER=", releaseArguments, StringComparison.Ordinal);
    }

    /// <summary>
    /// Invokes the native build executor's private configure command builder for a selected profile.
    /// </summary>
    /// <param name="profile">Typed native profile to forward to CMake.</param>
    /// <returns>Built command-line arguments string.</returns>
    static string InvokePrivateCommandBuilder(WindowsNativeBuildProfile profile) {
        System.Reflection.MethodInfo method = typeof(WindowsNativeBuildExecutor).GetMethod(
            "BuildConfigureArguments",
            System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic)
            ?? throw new InvalidOperationException("Unable to find the native configure command builder.");

        return (string)(method.Invoke(null, [
            @"C:\repo",
            @"C:\build",
            @"C:\generated",
            @"C:\staged-code",
            @"C:\vs\VsDevCmd.bat",
            profile
        ]) ?? throw new InvalidOperationException("The native configure command builder returned no arguments."));
    }

    /// <summary>
    /// Resolves the Windows native-player repository root from the current test assembly location.
    /// </summary>
    /// <returns>Absolute repository root path.</returns>
    static string ResolveWindowsRepositoryRootPath() {
        string assemblyDirectoryPath = AppContext.BaseDirectory;
        string repositoryRootPath = Path.GetFullPath(Path.Combine(assemblyDirectoryPath, "..", "..", "..", ".."));
        if (!Directory.Exists(repositoryRootPath)) {
            throw new InvalidOperationException($"Could not resolve the Windows repository root from '{assemblyDirectoryPath}'.");
        }

        return repositoryRootPath;
    }
}
