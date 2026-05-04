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
            @"C:\vs\VsDevCmd.bat");

        Assert.Contains("call \"C:\\vs\\VsDevCmd.bat\" -arch=amd64 -host_arch=amd64", arguments, StringComparison.Ordinal);
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
