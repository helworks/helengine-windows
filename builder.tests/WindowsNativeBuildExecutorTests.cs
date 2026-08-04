using helengine.windows.builder;
using System.Reflection;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the native Windows build executor emits an amd64 Visual Studio toolchain command line.
/// </summary>
public sealed class WindowsNativeBuildExecutorTests {
    /// <summary>
    /// Verifies the CMake configure command forces the amd64 developer environment before invoking CMake.
    /// </summary>
    [Fact]
    public void BuildConfigureArguments_forces_amd64_visual_studio_environment() {
        string arguments = InvokePrivateCommandBuilder(
            "BuildConfigureArguments",
            @"C:\repo",
            @"C:\build",
            @"C:\generated",
            @"C:\staged-code",
            @"C:\vs\VsDevCmd.bat",
            WindowsNativeBuildProfile.Debug);

        Assert.Contains("call \"C:\\vs\\VsDevCmd.bat\" -arch=amd64 -host_arch=amd64", arguments, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the CMake configure command forwards the staged generated code-module root to the native player build.
    /// </summary>
    [Fact]
    public void BuildConfigureArguments_forwards_staged_code_root() {
        string arguments = InvokePrivateCommandBuilder(
            "BuildConfigureArguments",
            @"C:\repo",
            @"C:\build",
            @"C:\generated",
            @"C:\staged-code",
            @"C:\vs\VsDevCmd.bat",
            WindowsNativeBuildProfile.Debug);

        Assert.Contains("-DHELENGINE_CODE_ROOT=\"C:\\staged-code\"", arguments, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the CMake build command re-enters the amd64 developer environment before compiling native sources.
    /// </summary>
    [Fact]
    public void BuildNativeBuildArguments_forces_amd64_visual_studio_environment() {
        string arguments = InvokePrivateCommandBuilder(
            "BuildNativeBuildArguments",
            @"C:\build",
            @"C:\vs\VsDevCmd.bat");

        Assert.Contains("call \"C:\\vs\\VsDevCmd.bat\" -arch=amd64 -host_arch=amd64", arguments, StringComparison.Ordinal);
        Assert.DoesNotContain("--config", arguments, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the profiler build configures Ninja with the optimized profiling configuration and explicit native profile.
    /// </summary>
    [Fact]
    public void BuildConfigureArguments_for_profiler_uses_rel_with_deb_info_and_forwards_profile() {
        string arguments = InvokePrivateCommandBuilder(
            "BuildConfigureArguments",
            @"C:\repo",
            @"C:\build",
            @"C:\generated",
            @"C:\staged-code",
            @"C:\vs\VsDevCmd.bat",
            WindowsNativeBuildProfile.Profiler);

        Assert.Contains("-DCMAKE_BUILD_TYPE=RelWithDebInfo", arguments, StringComparison.Ordinal);
        Assert.Contains("-DHELENGINE_WINDOWS_NATIVE_PROFILE=Profiler", arguments, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies a process-scoped physics override is forwarded to CMake without changing default builds.
    /// </summary>
    [Fact]
    public void BuildConfigureArguments_forwards_process_scoped_physics_overrides() {
        const string fixedStepHertzVariable = "HELENGINE_WINDOWS_PHYSICS_FIXED_STEP_HERTZ";
        const string velocityIterationsVariable = "HELENGINE_WINDOWS_PHYSICS_VELOCITY_ITERATIONS";
        const string substepsVariable = "HELENGINE_WINDOWS_PHYSICS_SUBSTEPS";
        string previousFixedStepHertz = Environment.GetEnvironmentVariable(fixedStepHertzVariable);
        string previousVelocityIterations = Environment.GetEnvironmentVariable(velocityIterationsVariable);
        string previousSubsteps = Environment.GetEnvironmentVariable(substepsVariable);

        try {
            Environment.SetEnvironmentVariable(fixedStepHertzVariable, "20");
            Environment.SetEnvironmentVariable(velocityIterationsVariable, "1");
            Environment.SetEnvironmentVariable(substepsVariable, "1");

            string arguments = InvokePrivateCommandBuilder(
                "BuildConfigureArguments",
                @"C:\repo",
                @"C:\build",
                @"C:\generated",
                @"C:\staged-code",
                @"C:\vs\VsDevCmd.bat",
                WindowsNativeBuildProfile.Profiler);

            Assert.Contains("-DHELENGINE_WINDOWS_PHYSICS_FIXED_STEP_HERTZ=20", arguments, StringComparison.Ordinal);
            Assert.Contains("-DHELENGINE_WINDOWS_PHYSICS_VELOCITY_ITERATIONS=1", arguments, StringComparison.Ordinal);
            Assert.Contains("-DHELENGINE_WINDOWS_PHYSICS_SUBSTEPS=1", arguments, StringComparison.Ordinal);
        } finally {
            Environment.SetEnvironmentVariable(fixedStepHertzVariable, previousFixedStepHertz);
            Environment.SetEnvironmentVariable(velocityIterationsVariable, previousVelocityIterations);
            Environment.SetEnvironmentVariable(substepsVariable, previousSubsteps);
        }
    }

    /// <summary>
    /// Verifies the release build configures Ninja with release optimization and explicit native profile.
    /// </summary>
    [Fact]
    public void BuildConfigureArguments_for_release_uses_release_and_forwards_profile() {
        string arguments = InvokePrivateCommandBuilder(
            "BuildConfigureArguments",
            @"C:\repo",
            @"C:\build",
            @"C:\generated",
            @"C:\staged-code",
            @"C:\vs\VsDevCmd.bat",
            WindowsNativeBuildProfile.Release);

        Assert.Contains("-DCMAKE_BUILD_TYPE=Release", arguments, StringComparison.Ordinal);
        Assert.Contains("-DHELENGINE_WINDOWS_NATIVE_PROFILE=Release", arguments, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the debug build configures Ninja with debug symbols and explicit native profile.
    /// </summary>
    [Fact]
    public void BuildConfigureArguments_for_debug_uses_debug_and_forwards_profile() {
        string arguments = InvokePrivateCommandBuilder(
            "BuildConfigureArguments",
            @"C:\repo",
            @"C:\build",
            @"C:\generated",
            @"C:\staged-code",
            @"C:\vs\VsDevCmd.bat",
            WindowsNativeBuildProfile.Debug);

        Assert.Contains("-DCMAKE_BUILD_TYPE=Debug", arguments, StringComparison.Ordinal);
        Assert.Contains("-DHELENGINE_WINDOWS_NATIVE_PROFILE=Debug", arguments, StringComparison.Ordinal);
    }

    /// <summary>
    /// Invokes one private static command-builder helper from the production executor.
    /// </summary>
    /// <param name="methodName">Private helper method name.</param>
    /// <param name="arguments">Ordered method arguments.</param>
    /// <returns>Built command-line arguments string.</returns>
    static string InvokePrivateCommandBuilder(string methodName, params object[] arguments) {
        MethodInfo method = typeof(WindowsNativeBuildExecutor).GetMethod(
            methodName,
            BindingFlags.Static | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException($"Unable to find private method '{methodName}'.");

        return (string)(method.Invoke(null, arguments)
            ?? throw new InvalidOperationException($"Private method '{methodName}' returned no value."));
    }
}
