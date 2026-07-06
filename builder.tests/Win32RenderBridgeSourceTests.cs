namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the native Windows render bridge exposes shader-backed raw-material loading for packaged scene startup.
/// </summary>
public sealed class Win32RenderBridgeSourceTests {
    /// <summary>
    /// Verifies the bridge declares and implements the raw-material build override using the shared shader runtime loader.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_defines_shader_backed_raw_material_build_override() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string headerPath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.hpp");
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string headerSource = File.ReadAllText(headerPath);
        string implementationSource = File.ReadAllText(sourcePath);

        Assert.Contains("RuntimeMaterial* BuildMaterialFromRawAsset(ContentManager* assetContentManager, std::string contentRootPath, std::string materialAssetPath) override;", headerSource, StringComparison.Ordinal);
        Assert.Contains("RuntimeMaterial* Win32RenderManager3D::BuildMaterialFromRawAsset(ContentManager* assetContentManager, std::string contentRootPath, std::string materialAssetPath)", implementationSource, StringComparison.Ordinal);
        Assert.Contains("ShaderRuntimeMaterialLoader::BuildMaterialFromRawAsset(this, assetContentManager, contentRootPath, materialAssetPath);", implementationSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the built-in Windows forward shader consumes the authored standard-material base-color constant buffer.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_builtin_forward_shader_uses_standard_material_base_color_buffer() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);

        Assert.Contains("cbuffer BaseColorBuffer : register(b3)", implementationSource, StringComparison.Ordinal);
        Assert.Contains("float4 baseColor;", implementationSource, StringComparison.Ordinal);
        Assert.Contains("float4 sampledBaseColor = baseColor;", implementationSource, StringComparison.Ordinal);
        Assert.DoesNotContain("float3 surfaceColor = float3(0.78f, 0.80f, 0.84f);", implementationSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the Windows bridge directional shadow pass honors runtime material shadow-cast flags.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_directional_shadow_pass_honors_runtime_material_shadow_cast_flags() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string headerPath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.hpp");
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string headerSource = File.ReadAllText(headerPath);
        string implementationSource = File.ReadAllText(sourcePath);

        Assert.Contains("bool ShouldMaterialCastShadows(RuntimeMaterial* material) const;", headerSource, StringComparison.Ordinal);
        Assert.Contains("bool Win32RenderManager3D::ShouldMaterialCastShadows(RuntimeMaterial* material) const", implementationSource, StringComparison.Ordinal);
        Assert.Contains("Array<RuntimeMaterial*>* runtimeMaterials = drawable->get_Materials();", implementationSource, StringComparison.Ordinal);
        Assert.Contains("if (!ShouldMaterialCastShadows(runtimeMaterial)) {", implementationSource, StringComparison.Ordinal);
        Assert.Contains("return rootMaterial->get_CastsShadows();", implementationSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the Windows bridge falls back to the standard white diffuse texture and clears texture slot zero when materials expose no texture bindings.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_standard_material_texture_binding_path_uses_pixel_texture_fallback_and_clears_empty_slots() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);

        Assert.Contains("texture = TextureUtils::get_PixelTexture();", implementationSource, StringComparison.Ordinal);
        Assert.Contains("context->PSSetShaderResources(0, 1, &nullResourceView);", implementationSource, StringComparison.Ordinal);
        Assert.Contains("context->PSSetSamplers(0, 1, &nullSampler);", implementationSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the Windows native player includes the current generated engine header names for shared math and input value types.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_and_input_bridge_use_current_generated_engine_header_names() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string renderHeaderPath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.hpp");
        string inputHeaderPath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_input_bridge.hpp");
        string renderSourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");
        string inputSourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_input_bridge.cpp");

        string renderHeaderSource = File.ReadAllText(renderHeaderPath);
        string inputHeaderSource = File.ReadAllText(inputHeaderPath);
        string renderSource = File.ReadAllText(renderSourcePath);
        string inputSource = File.ReadAllText(inputSourcePath);

        Assert.Contains("#include \"float4x4.hpp\"", renderHeaderSource, StringComparison.Ordinal);
        Assert.DoesNotContain("#include \"helengine_float4x4.hpp\"", renderHeaderSource, StringComparison.Ordinal);
        Assert.Contains("#include \"int2.hpp\"", inputHeaderSource, StringComparison.Ordinal);
        Assert.DoesNotContain("#include \"helengine_int2.hpp\"", inputHeaderSource, StringComparison.Ordinal);
        Assert.DoesNotContain("#include \"helengine_helengine_int2.hpp\"", inputHeaderSource, StringComparison.Ordinal);
        Assert.DoesNotContain("helengine_int2(", inputSource, StringComparison.Ordinal);
        Assert.Contains("drawable->get_Materials()", renderSource, StringComparison.Ordinal);
        Assert.DoesNotContain("drawable->get_Material()", renderSource, StringComparison.Ordinal);
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
