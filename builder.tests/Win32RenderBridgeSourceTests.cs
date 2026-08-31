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

        Assert.Contains("RuntimeMaterial* BuildMaterialFromRawAsset(ContentManager* assetContentManager, std::string materialAssetPath) override;", headerSource, StringComparison.Ordinal);
        Assert.Contains("RuntimeMaterial* Win32RenderManager3D::BuildMaterialFromRawAsset(ContentManager* assetContentManager, std::string materialAssetPath)", implementationSource, StringComparison.Ordinal);
        Assert.Contains("ShaderRuntimeMaterialLoader::BuildMaterialFromRawAsset(this, assetContentManager, materialAssetPath);", implementationSource, StringComparison.Ordinal);
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
        Assert.Contains("if (!ShouldMaterialCastShadows(runtimeMaterial) || !UsesTriangleTopology(submesh)) {", implementationSource, StringComparison.Ordinal);
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
    /// Verifies raw texture uploads reject incompatible formats and malformed payload lengths before calling the graphics driver.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_validates_rgba32_texture_payload_before_native_upload() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);
        int colorFormatValidationIndex = implementationSource.IndexOf(
            "data->ColorFormat != TextureAssetColorFormat::Rgba32",
            StringComparison.Ordinal);
        int payloadLengthValidationIndex = implementationSource.IndexOf(
            "static_cast<std::uint64_t>(data->Colors->Length) != expectedColorByteCount",
            StringComparison.Ordinal);
        int textureUploadIndex = implementationSource.IndexOf(
            "Bootstrap.GetDevice()->CreateTexture2D(&textureDescription, &textureData, textureResource.Texture.GetAddressOf())",
            StringComparison.Ordinal);

        Assert.True(colorFormatValidationIndex >= 0);
        Assert.True(payloadLengthValidationIndex >= 0);
        Assert.True(textureUploadIndex >= 0);
        Assert.True(colorFormatValidationIndex < textureUploadIndex);
        Assert.True(payloadLengthValidationIndex < textureUploadIndex);
    }

    /// <summary>
    /// Verifies the packaged bridge declares the generated native texture-region hook with the generated array ABI.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_declares_generated_texture_region_override_with_native_array_parameter() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string headerPath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.hpp");

        string headerSource = File.ReadAllText(headerPath);

        Assert.Contains("void UpdateTextureRegionCore(", headerSource, StringComparison.Ordinal);
        Assert.Contains("::RuntimeTexture* texture", headerSource, StringComparison.Ordinal);
        Assert.Contains("Array<uint8_t>* rgba8", headerSource, StringComparison.Ordinal);
        Assert.Contains("int32_t sourceRowPitch", headerSource, StringComparison.Ordinal);
        Assert.Contains("override;", headerSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the native hook resolves an exact runtime-texture owner and rejects disposed or missing resources.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_texture_region_hook_validates_runtime_texture_ownership() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);

        Assert.Contains("RuntimeTextureResourceOwners[runtimeTexture]", implementationSource, StringComparison.Ordinal);
        Assert.Contains("RuntimeTextureResourceOwners.find(texture)", implementationSource, StringComparison.Ordinal);
        Assert.Contains("RuntimeTextureResourceOwners.end()", implementationSource, StringComparison.Ordinal);
        Assert.Contains("texture->get_IsDisposed()", implementationSource, StringComparison.Ordinal);
        Assert.Contains("Win32TextureResource* ownedResource = owner->second;", implementationSource, StringComparison.Ordinal);
        Assert.Contains("ownedResource->Texture", implementationSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the native hook uploads only the requested box with the generated array's data and source row pitch.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_texture_region_hook_updates_exact_d3d11_box_and_pitches() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);
        string hookSource = ExtractTextureRegionHook(implementationSource);

        Assert.Contains("D3D11_BOX region {}", hookSource, StringComparison.Ordinal);
        Assert.Contains("region.left = static_cast<UINT>(x);", hookSource, StringComparison.Ordinal);
        Assert.Contains("region.top = static_cast<UINT>(y);", hookSource, StringComparison.Ordinal);
        Assert.Contains("region.front = 0;", hookSource, StringComparison.Ordinal);
        Assert.Contains("region.right = static_cast<UINT>(x + width);", hookSource, StringComparison.Ordinal);
        Assert.Contains("region.bottom = static_cast<UINT>(y + height);", hookSource, StringComparison.Ordinal);
        Assert.Contains("region.back = 1;", hookSource, StringComparison.Ordinal);
        Assert.Contains("rgba8->Data", hookSource, StringComparison.Ordinal);
        Assert.Contains("static_cast<UINT>(sourceRowPitch)", hookSource, StringComparison.Ordinal);
        Assert.Contains("UpdateSubresource", hookSource, StringComparison.Ordinal);
        Assert.Contains("            0);", hookSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies a region update reuses the existing texture resource and does not recreate a texture or view.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_texture_region_hook_preserves_existing_texture_and_view_identity() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);
        string hookSource = ExtractTextureRegionHook(implementationSource);

        Assert.DoesNotContain("CreateTexture2D", hookSource, StringComparison.Ordinal);
        Assert.DoesNotContain("CreateShaderResourceView", hookSource, StringComparison.Ordinal);
        Assert.DoesNotContain("new Array<uint8_t>", hookSource, StringComparison.Ordinal);
        Assert.Contains("RuntimeTextureResourceOwners", hookSource, StringComparison.Ordinal);
        Assert.Contains("Texture.Get()", hookSource, StringComparison.Ordinal);
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

    /// <summary>
    /// Extracts the packaged bridge's texture-region hook for focused source-contract assertions.
    /// </summary>
    /// <param name="implementationSource">Complete native bridge source.</param>
    /// <returns>Texture-region hook source through the next native texture release method.</returns>
    static string ExtractTextureRegionHook(string implementationSource) {
        int hookIndex = implementationSource.IndexOf(
            "void Win32RenderManager2D::UpdateTextureRegionCore(",
            StringComparison.Ordinal);
        Assert.True(hookIndex >= 0);

        int nextMethodIndex = implementationSource.IndexOf(
            "/// Releases Windows renderer-owned 2D resources.",
            hookIndex,
            StringComparison.Ordinal);

        Assert.True(nextMethodIndex > hookIndex);
        return implementationSource.Substring(hookIndex, nextMethodIndex - hookIndex);
    }
}
