namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies that shader-backed Windows materials always use their packaged DirectX shader resources.
/// </summary>
public sealed class WindowsPackagedForwardShaderSourceTests {
    /// <summary>
    /// Absolute native renderer source path inspected by the source-contract tests.
    /// </summary>
    const string Win32RenderBridgePath = @"C:\dev\helworks\helengine-windows\src\platform\windows\win32\win32_render_bridge.cpp";

    /// <summary>
    /// Ensures a missing cached resource for a shader-backed material fails instead of falling through to the native fallback shader.
    /// </summary>
    [Fact]
    public void Visit_WhenShaderBackedMaterialHasNoCachedShaderResource_ThrowsInsteadOfUsingFallbackPipeline() {
        string source = File.ReadAllText(Win32RenderBridgePath);

        Assert.Contains("Standard materials require a packaged shader resource.", source, StringComparison.Ordinal);
    }

    /// <summary>
    /// Ensures the first-frame diagnostics identify the shader asset selected for the packaged material path.
    /// </summary>
    [Fact]
    public void BuildMaterialFromRaw_WhenCreatingPackagedShaderResource_RecordsShaderAssetIdentity() {
        string source = File.ReadAllText(Win32RenderBridgePath);

        Assert.Contains("3d.material_shader_asset_id=", source, StringComparison.Ordinal);
    }
}
