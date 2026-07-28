using helengine.windows.builder;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies Windows player profile selection cannot silently diverge from generated C++ function profiling configuration.
/// </summary>
public sealed class WindowsBuildWorkspaceProfileValidationTests {
    /// <summary>
    /// Verifies profiler builds reject an explicitly disabled generated function profiling setting.
    /// </summary>
    [Fact]
    public void ValidateGeneratedFunctionProfilingConfiguration_rejects_disabled_profiler_instrumentation() {
        Assert.Throws<InvalidOperationException>(() => WindowsBuildWorkspace.ValidateGeneratedFunctionProfilingConfiguration(
            WindowsNativeBuildProfileResolver.Resolve("profiler"),
            new Dictionary<string, string>(StringComparer.Ordinal) {
                ["codegen-generated-function-profiling"] = "false"
            }));
    }

    /// <summary>
    /// Verifies profiler builds reject an absent generated function profiling setting.
    /// </summary>
    [Fact]
    public void ValidateGeneratedFunctionProfilingConfiguration_rejects_missing_profiler_instrumentation() {
        Assert.Throws<InvalidOperationException>(() => WindowsBuildWorkspace.ValidateGeneratedFunctionProfilingConfiguration(
            WindowsNativeBuildProfileResolver.Resolve("profiler"),
            new Dictionary<string, string>(StringComparer.Ordinal)));
    }

    /// <summary>
    /// Verifies debug builds reject generated function profiling because they do not link the native profiler client.
    /// </summary>
    [Fact]
    public void ValidateGeneratedFunctionProfilingConfiguration_rejects_debug_instrumentation() {
        Assert.Throws<InvalidOperationException>(() => WindowsBuildWorkspace.ValidateGeneratedFunctionProfilingConfiguration(
            WindowsNativeBuildProfileResolver.Resolve("debug"),
            new Dictionary<string, string>(StringComparer.Ordinal) {
                ["codegen-generated-function-profiling"] = "true"
            }));
    }

    /// <summary>
    /// Verifies release builds reject generated function profiling because they must contain no profiler payload.
    /// </summary>
    [Fact]
    public void ValidateGeneratedFunctionProfilingConfiguration_rejects_release_instrumentation() {
        Assert.Throws<InvalidOperationException>(() => WindowsBuildWorkspace.ValidateGeneratedFunctionProfilingConfiguration(
            WindowsNativeBuildProfileResolver.Resolve("release"),
            new Dictionary<string, string>(StringComparer.Ordinal) {
                ["codegen-generated-function-profiling"] = "true"
            }));
    }
}
