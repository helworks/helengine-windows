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
    /// Verifies the Windows bridge uses the current two-argument standard-material default API and current renderer-owned fallback texture property.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_standard_material_defaults_use_current_generated_apis() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);

        string materialBuildSource = ExtractMethodBody(
            implementationSource,
            "RuntimeMaterial* Win32RenderManager3D::BuildMaterialFromRaw(");
        string textureBindingSource = ExtractMethodBody(
            implementationSource,
            "void Win32RenderManager3D::BindMaterialTextures(");

        Assert.Contains(
            "StandardMaterialTextureBindingDefaults::Apply(shaderRuntimeMaterial, renderManager2D);",
            materialBuildSource,
            StringComparison.Ordinal);
        Assert.Contains(
            "RenderManager2D* renderManager2D = OwnerCore != nullptr",
            materialBuildSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain(
            "StandardMaterialTextureBindingDefaults::Apply(shaderRuntimeMaterial);",
            materialBuildSource,
            StringComparison.Ordinal);
        Assert.DoesNotContain("TextureUtils::get_PixelTexture()", textureBindingSource, StringComparison.Ordinal);
        Assert.Contains("renderManager2D->get_PixelTexture()", textureBindingSource, StringComparison.Ordinal);
        Assert.Contains("context->PSSetShaderResources(0, ClearedMaterialTextureSlotCount, clearedShaderResources);", textureBindingSource, StringComparison.Ordinal);
        Assert.Contains("context->PSSetSamplers(0, ClearedMaterialTextureSlotCount, clearedSamplers);", textureBindingSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the Windows native build consumes the current generated-core handoff translation unit and optional runtime manifest sources.
    /// </summary>
    [Fact]
    public void Win32NativeBuild_consumes_current_generated_core_handoff_and_runtime_manifest_sources() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string cmakePath = Path.Combine(repositoryRootPath, "CMakeLists.txt");

        string cmakeSource = File.ReadAllText(cmakePath);

        Assert.Contains("generated_windows_handoff.cmake", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("CPP_GENERATED_UNITY_SOURCE", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("runtime/runtime_startup_manifest.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("runtime/runtime_scene_catalog_manifest.cpp", cmakeSource, StringComparison.Ordinal);
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
        string hookSource = ExtractMethodBody(
            implementationSource,
            "void Win32RenderManager2D::UpdateTextureRegionCore(");

        Assert.Contains("RuntimeTextureResourceOwners.find(texture)", hookSource, StringComparison.Ordinal);
        Assert.Contains("RuntimeTextureResourceOwners.end()", hookSource, StringComparison.Ordinal);
        Assert.Contains("texture->get_IsDisposed()", hookSource, StringComparison.Ordinal);
        Assert.Contains("Win32TextureResource* ownedResource = owner->second;", hookSource, StringComparison.Ordinal);
        Assert.Contains("ownedResource->Texture", hookSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies repeated builds for one id share one native resource while retaining one owner entry per runtime object.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_duplicate_texture_id_shares_resource_and_increments_owner_count() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);
        string buildSource = ExtractMethodBody(
            implementationSource,
            "RuntimeTexture* Win32RenderManager2D::BuildTextureFromRaw(");

        Assert.Contains("auto existingResource = TextureResources.find(textureId);", buildSource, StringComparison.Ordinal);
        Assert.Contains("existingResource->second->OwnerCount++;", buildSource, StringComparison.Ordinal);
        Assert.Contains("sharedResource->EngineOwnedOwnerCount++;", buildSource, StringComparison.Ordinal);
        Assert.Contains("RuntimeTextureResourceOwners.emplace(runtimeTexture, sharedResource);", buildSource, StringComparison.Ordinal);
        Assert.Contains("textureResource.OwnerCount = 1;", buildSource, StringComparison.Ordinal);
        Assert.Contains("TextureResources.emplace(", buildSource, StringComparison.Ordinal);
        Assert.DoesNotContain("RuntimeTextureResourceOwners.erase", buildSource, StringComparison.Ordinal);
        Assert.DoesNotContain("TextureResources[textureId] =", buildSource, StringComparison.Ordinal);
        Assert.True(
            buildSource.IndexOf("auto existingResource = TextureResources.find(textureId);", StringComparison.Ordinal)
                < buildSource.IndexOf("CreateTexture2D", StringComparison.Ordinal));
    }

    /// <summary>
    /// Verifies releasing one duplicate-id owner deletes only that runtime object and releases the shared native resource last.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_release_texture_drains_owner_before_last_resource_release() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);
        string releaseSource = ExtractMethodBody(
            implementationSource,
            "void Win32RenderManager2D::ReleaseTexture(");

        Assert.Contains("RuntimeTextureResourceOwners.erase(owner);", releaseSource, StringComparison.Ordinal);
        Assert.Contains("--ownedResource->OwnerCount;", releaseSource, StringComparison.Ordinal);
        Assert.Contains("const bool isEngineOwned = texture->get_IsEngineOwned();", releaseSource, StringComparison.Ordinal);
        Assert.Contains("--ownedResource->EngineOwnedOwnerCount;", releaseSource, StringComparison.Ordinal);
        Assert.Contains("if (ownedResource->OwnerCount == 0)", releaseSource, StringComparison.Ordinal);
        Assert.Contains("TextureResources.erase(resource);", releaseSource, StringComparison.Ordinal);
        Assert.Contains("RenderManager2D::ReleaseTexture(texture);", releaseSource, StringComparison.Ordinal);
        Assert.True(
            releaseSource.IndexOf("TextureResources.erase(resource);", StringComparison.Ordinal)
                < releaseSource.IndexOf("RenderManager2D::ReleaseTexture(texture);", StringComparison.Ordinal));
    }

    /// <summary>
    /// Verifies renderer disposal calls the generated base lifecycle and drains every remaining runtime owner.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_dispose_drains_runtime_texture_owners_and_native_resources() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);
        string disposeSource = ExtractMethodBody(
            implementationSource,
            "void Win32RenderManager2D::Dispose(");

        Assert.Contains("RenderManager2D::Dispose();", disposeSource, StringComparison.Ordinal);
        Assert.Contains("while (!RuntimeTextureResourceOwners.empty())", disposeSource, StringComparison.Ordinal);
        Assert.Contains("RenderManager2D::ReleaseTexture(runtimeTexture);", disposeSource, StringComparison.Ordinal);
        Assert.Contains("TextureResources.clear();", disposeSource, StringComparison.Ordinal);
        Assert.DoesNotContain("RuntimeTextureResourceOwners.clear()", disposeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the native hook uploads only the requested box with the generated array's data and source row pitch.
    /// </summary>
    [Fact]
    public void Win32RenderBridge_texture_region_hook_updates_exact_d3d11_box_and_pitches() {
        string repositoryRootPath = ResolveWindowsRepositoryRootPath();
        string sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        string implementationSource = File.ReadAllText(sourcePath);
        string hookSource = ExtractMethodBody(
            implementationSource,
            "void Win32RenderManager2D::UpdateTextureRegionCore(");

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
        string hookSource = ExtractMethodBody(
            implementationSource,
            "void Win32RenderManager2D::UpdateTextureRegionCore(");

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
    /// Extracts one native method body for focused source-contract assertions.
    /// </summary>
    /// <param name="implementationSource">Complete native bridge source.</param>
    /// <param name="methodSignature">Unique method signature prefix.</param>
    /// <returns>Complete native method source through its matching closing brace.</returns>
    static string ExtractMethodBody(string implementationSource, string methodSignature) {
        int methodIndex = implementationSource.IndexOf(methodSignature, StringComparison.Ordinal);
        Assert.True(methodIndex >= 0);

        int openingBraceIndex = implementationSource.IndexOf('{', methodIndex);
        Assert.True(openingBraceIndex > methodIndex);

        int braceDepth = 0;
        for (int index = openingBraceIndex; index < implementationSource.Length; index++) {
            if (implementationSource[index] == '{') {
                braceDepth++;
            } else if (implementationSource[index] == '}') {
                braceDepth--;
                if (braceDepth == 0) {
                    return implementationSource.Substring(methodIndex, index - methodIndex + 1);
                }
            }
        }

        Assert.Fail($"Could not find the closing brace for '{methodSignature}'.");
        return string.Empty;
    }
}
