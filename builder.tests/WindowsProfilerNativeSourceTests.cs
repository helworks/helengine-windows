namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the native Windows profiler integration remains isolated to the Tracy-enabled player profile.
/// </summary>
public sealed class WindowsProfilerNativeSourceTests {
    /// <summary>
    /// Verifies the native host and DirectX11 draw paths use the guarded profiler boundary at their real frame stages.
    /// </summary>
    [Fact]
    public void Native_sources_route_cpu_gpu_zones_and_frame_plots_through_the_guarded_profiler_boundary() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string profilerHeaderSource = File.ReadAllText(Path.Combine(repositoryRootPath, "src", "platform", "windows", "runtime", "windows_tracy_profiler.hpp"));
        string profilerImplementationSource = File.ReadAllText(Path.Combine(repositoryRootPath, "src", "platform", "windows", "runtime", "windows_tracy_profiler.cpp"));
        string applicationSource = File.ReadAllText(Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_application.cpp"));
        string inputSource = File.ReadAllText(Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_input_bridge.cpp"));
        string renderBridgeSource = File.ReadAllText(Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp"));
        string presenterSource = File.ReadAllText(Path.Combine(repositoryRootPath, "src", "platform", "windows", "directx11", "directx11_presenter.cpp"));

        Assert.Contains("#if defined(TRACY_ENABLE)", profilerHeaderSource, StringComparison.Ordinal);
        Assert.Contains("<tracy/Tracy.hpp>", profilerHeaderSource, StringComparison.Ordinal);
        Assert.Contains("<tracy/TracyD3D11.hpp>", profilerHeaderSource, StringComparison.Ordinal);
        Assert.Contains("TracyD3D11Context", profilerImplementationSource, StringComparison.Ordinal);
        Assert.Contains("TracyD3D11Collect", profilerImplementationSource, StringComparison.Ordinal);
        Assert.Contains("TracyD3D11Destroy", profilerImplementationSource, StringComparison.Ordinal);
        Assert.Contains("FrameMarkStart", profilerImplementationSource, StringComparison.Ordinal);
        Assert.Contains("FrameMarkEnd", profilerImplementationSource, StringComparison.Ordinal);
        Assert.Contains("TracyPlot", profilerImplementationSource, StringComparison.Ordinal);
        Assert.DoesNotContain("TracyD3D11NewFrame", profilerImplementationSource, StringComparison.Ordinal);

        Assert.Contains("InitializeWindowsTracyProfiler", applicationSource, StringComparison.Ordinal);
        Assert.Contains("BeginWindowsTracyProfilerFrame", applicationSource, StringComparison.Ordinal);
        Assert.Contains("CollectWindowsTracyProfilerGpu", applicationSource, StringComparison.Ordinal);
        Assert.Contains("ShutdownWindowsTracyProfiler", applicationSource, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_TRACY_ZONE_N(\"Frame\")", applicationSource, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_TRACY_ZONE_N(\"Engine.Update\")", applicationSource, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_TRACY_PLOT", applicationSource, StringComparison.Ordinal);
        Assert.True(applicationSource.IndexOf("EngineCore->Draw();", StringComparison.Ordinal) < applicationSource.IndexOf("EmitWindowsTracyProfilerPlots", StringComparison.Ordinal));
        Assert.True(applicationSource.IndexOf("EmitWindowsTracyProfilerPlots", StringComparison.Ordinal) < applicationSource.IndexOf("Presenter->RenderFrame();", StringComparison.Ordinal));
        Assert.True(applicationSource.IndexOf("Presenter->RenderFrame();", StringComparison.Ordinal) < applicationSource.IndexOf("CollectWindowsTracyProfilerGpu", StringComparison.Ordinal));

        Assert.Contains("HELENGINE_TRACY_ZONE_N(\"Input.Capture\")", inputSource, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_TRACY_ZONE_N(\"Render.ExtractAndBuild\")", renderBridgeSource, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_TRACY_GPU_ZONE_N(\"D3D11.MainDraw\")", renderBridgeSource, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_TRACY_GPU_ZONE_N(\"D3D11.ShadowDraw\")", renderBridgeSource, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_TRACY_GPU_ZONE_N(\"D3D11.Draw2D\")", renderBridgeSource, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_TRACY_ZONE_N(\"D3D11.Present\")", presenterSource, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_TRACY_GPU_ZONE_N(\"D3D11.Present\")", presenterSource, StringComparison.Ordinal);
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
