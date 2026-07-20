using helengine;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.baseplatform.Targets;
using helengine.windows.builder;
using helengine.windows.builder.tests.Builders;
using Xunit;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Windows builder metadata and payload staging behavior.
/// </summary>
public class WindowsPlatformAssetBuilderTests {
    /// <summary>
    /// Verifies the builder exposes the expected Windows metadata.
    /// </summary>
    [Fact]
    public void Descriptor_and_definition_expose_windows_metadata() {
        WindowsPlatformAssetBuilder builder = new();

        Assert.Equal("helengine.windows.builder", builder.Descriptor.BuilderId);
        Assert.Equal("windows", builder.Descriptor.TargetPlatformId);
        Assert.Equal("windows", builder.Definition.PlatformId);
        Assert.Contains(builder.Definition.BuildProfiles, profile => profile.ProfileId == "debug");
        Assert.Contains(builder.Definition.BuildProfiles, profile => profile.ProfileId == "release");
        Assert.Contains(builder.Definition.GraphicsProfiles, profile => profile.ProfileId == "directx11");
        Assert.Contains(builder.Definition.StorageProfiles, profile =>
            profile.ProfileId == "loose-files" &&
            profile.RuntimeSpecializationId == "windows-loose-files");
        Assert.Contains(builder.Definition.ComponentSupportRules, supportRule =>
            supportRule.ComponentTypeId == "helengine.fpscomponent" &&
            supportRule.SupportKind == PlatformComponentSupportKind.Transform);
        Assert.Contains(builder.Definition.ComponentSupportRules, supportRule =>
            supportRule.ComponentTypeId == "helengine.meshcomponent" &&
            supportRule.SupportKind == PlatformComponentSupportKind.Transform);
    }

    /// <summary>
    /// Verifies the Windows material schema publishes the authored standard material fields.
    /// </summary>
    [Fact]
    public void Descriptor_and_definition_expose_standard_material_fields() {
        WindowsPlatformAssetBuilder builder = new();

        PlatformMaterialSchemaDefinition schema = Assert.Single(builder.Definition.MaterialSchemas, materialSchema => materialSchema.SchemaId == "standard-shader");

        Assert.Collection(schema.Fields,
            field => {
                Assert.Equal("use-custom-shader", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.Boolean, field.FieldKind);
                Assert.Equal("false", field.DefaultValue);
                Assert.True(field.Required);
            },
            field => {
                Assert.Equal("shader-asset-id", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.AssetReference, field.FieldKind);
                Assert.Equal(string.Empty, field.DefaultValue);
                Assert.True(field.Required);
            },
            field => {
                Assert.Equal("vertex-program", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.Text, field.FieldKind);
                Assert.Equal(string.Empty, field.DefaultValue);
                Assert.True(field.Required);
            },
            field => {
                Assert.Equal("pixel-program", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.Text, field.FieldKind);
                Assert.Equal(string.Empty, field.DefaultValue);
                Assert.True(field.Required);
            },
            field => {
                Assert.Equal("base-color", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.Color, field.FieldKind);
                Assert.Equal("#ffffff", field.DefaultValue);
                Assert.False(field.Required);
            },
            field => {
                Assert.Equal("texture-id", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.AssetReference, field.FieldKind);
                Assert.Equal(string.Empty, field.DefaultValue);
                Assert.False(field.Required);
            },
            field => {
                Assert.Equal("roughness", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.Text, field.FieldKind);
                Assert.Equal("1.0", field.DefaultValue);
                Assert.False(field.Required);
            },
            field => {
                Assert.Equal("roughness-texture-id", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.AssetReference, field.FieldKind);
                Assert.Equal(string.Empty, field.DefaultValue);
                Assert.False(field.Required);
            },
            field => {
                Assert.Equal("casts-shadow", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.Boolean, field.FieldKind);
                Assert.Equal("true", field.DefaultValue);
                Assert.False(field.Required);
            },
            field => {
                Assert.Equal("receives-shadow", field.FieldId);
                Assert.Equal(PlatformMaterialFieldKind.Boolean, field.FieldKind);
                Assert.Equal("true", field.DefaultValue);
                Assert.False(field.Required);
            });
    }

    /// <summary>
    /// Verifies the Windows material cook path preserves the diffuse texture and shadow authoring values.
    /// </summary>
    [Fact]
    public void CookMaterial_preserves_diffuse_texture_and_shadow_fields() {
        WindowsPlatformAssetBuilder builder = new();

        PlatformMaterialCookResult result = builder.CookMaterial(new PlatformMaterialCookRequest(
            "Materials/Test.helmat",
            "Materials/Test.helmat",
            "windows",
            "debug",
            "directx11",
            "standard-shader",
            new Dictionary<string, string> {
                ["use-custom-shader"] = "false",
                ["shader-asset-id"] = "ForwardStandardShader",
                ["vertex-program"] = "ForwardStandardShader.vs",
                ["pixel-program"] = "ForwardStandardShader.ps",
                ["variant"] = "Mesh",
                ["base-color"] = "#336699",
                ["texture-id"] = "Textures/Checker",
                ["casts-shadow"] = "false",
                ["receives-shadow"] = "true"
            }));

        ShaderMaterialAsset materialAsset = Assert.IsType<ShaderMaterialAsset>(global::helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        Assert.Equal("ForwardStandardShader", materialAsset.ShaderAssetId);
        Assert.Equal("Textures/Checker", materialAsset.DiffuseTextureAssetId);
        Assert.False(materialAsset.CastsShadows);
        Assert.True(materialAsset.ReceivesShadows);
        Assert.Single(materialAsset.ConstantBuffers);
        Assert.Equal("BaseColorBuffer", materialAsset.ConstantBuffers[0].Name);
        Assert.Equal(16, materialAsset.ConstantBuffers[0].Data.Length);
        Assert.Equal(new[] { "ForwardStandardShader" }, result.ReferencedShaderAssetIds);
    }

    /// <summary>
    /// Verifies the Windows material cook path preserves the authored roughness scalar and texture fields.
    /// </summary>
    [Fact]
    public void CookMaterial_preserves_roughness_scalar_and_texture_fields() {
        WindowsPlatformAssetBuilder builder = new();

        PlatformMaterialCookResult result = builder.CookMaterial(new PlatformMaterialCookRequest(
            "Materials/Test.helmat",
            "Materials/Test.helmat",
            "windows",
            "debug",
            "directx11",
            "standard-shader",
            new Dictionary<string, string> {
                ["use-custom-shader"] = "false",
                ["shader-asset-id"] = "ForwardStandardShader",
                ["vertex-program"] = "ForwardStandardShader.vs",
                ["pixel-program"] = "ForwardStandardShader.ps",
                ["variant"] = "Mesh",
                ["roughness"] = "0.35",
                ["roughness-texture-id"] = "Textures/MarbleRoughness"
            }));

        ShaderMaterialAsset materialAsset = Assert.IsType<ShaderMaterialAsset>(global::helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        MaterialConstantBufferAsset roughnessBuffer = Assert.Single(materialAsset.ConstantBuffers, constantBuffer => constantBuffer.Name == "RoughnessBuffer");
        float[] roughnessChannels = ReadFloat4(roughnessBuffer.Data);

        Assert.Equal("Textures/MarbleRoughness", ReadStringField(materialAsset, "RoughnessTextureAssetId"));
        Assert.Equal(0.35f, roughnessChannels[0], 5);
        Assert.Equal(0.35f, roughnessChannels[1], 5);
        Assert.Equal(0.35f, roughnessChannels[2], 5);
        Assert.Equal(0.35f, roughnessChannels[3], 5);
    }

    /// <summary>
    /// Verifies the Windows material schema publishes metallic and specular authored standard-material fields.
    /// </summary>
    [Fact]
    public void Descriptor_and_definition_expose_standard_material_metallic_and_specular_fields() {
        WindowsPlatformAssetBuilder builder = new();

        PlatformMaterialSchemaDefinition schema = Assert.Single(builder.Definition.MaterialSchemas, materialSchema => materialSchema.SchemaId == "standard-shader");

        Assert.Contains(schema.Fields, field => field.FieldId == "metallic" && field.FieldKind == PlatformMaterialFieldKind.Text && field.DefaultValue == "0.0" && !field.Required);
        Assert.Contains(schema.Fields, field => field.FieldId == "specular" && field.FieldKind == PlatformMaterialFieldKind.Text && field.DefaultValue == "0.5" && !field.Required);
    }

    /// <summary>
    /// Verifies the Windows material cook path preserves authored metallic and specular scalar values.
    /// </summary>
    [Fact]
    public void CookMaterial_preserves_metallic_and_specular_scalar_fields() {
        WindowsPlatformAssetBuilder builder = new();

        PlatformMaterialCookResult result = builder.CookMaterial(new PlatformMaterialCookRequest(
            "Materials/Test.helmat",
            "Materials/Test.helmat",
            "windows",
            "debug",
            "directx11",
            "standard-shader",
            new Dictionary<string, string> {
                ["use-custom-shader"] = "false",
                ["shader-asset-id"] = "ForwardStandardShader",
                ["vertex-program"] = "ForwardStandardShader.vs",
                ["pixel-program"] = "ForwardStandardShader.ps",
                ["variant"] = "Mesh",
                ["metallic"] = "0.25",
                ["specular"] = "0.75"
            }));

        ShaderMaterialAsset materialAsset = Assert.IsType<ShaderMaterialAsset>(global::helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        MaterialConstantBufferAsset metallicBuffer = Assert.Single(
            materialAsset.ConstantBuffers,
            constantBuffer => constantBuffer.Name == StandardMaterialMetallicDefaults.MetallicBufferName);
        MaterialConstantBufferAsset specularBuffer = Assert.Single(
            materialAsset.ConstantBuffers,
            constantBuffer => constantBuffer.Name == StandardMaterialSpecularDefaults.SpecularBufferName);

        Assert.Equal(StandardMaterialMetallicDefaults.CreateConstantBufferData(0.25f), metallicBuffer.Data);
        Assert.Equal(StandardMaterialSpecularDefaults.CreateConstantBufferData(0.75f), specularBuffer.Data);
    }

    /// <summary>
    /// Verifies eight-digit authored base colors use the shared engine <c>#RRGGBBAA</c> contract before the payload is cooked for the player.
    /// </summary>
    [Fact]
    public void CookMaterial_when_base_color_includes_alpha_preserves_rgba_channel_order() {
        WindowsPlatformAssetBuilder builder = new();

        PlatformMaterialCookResult result = builder.CookMaterial(new PlatformMaterialCookRequest(
            "Materials/Test.helmat",
            "Materials/Test.helmat",
            "windows",
            "debug",
            "directx11",
            "standard-shader",
            new Dictionary<string, string> {
                ["use-custom-shader"] = "false",
                ["shader-asset-id"] = "ForwardStandardShader",
                ["vertex-program"] = "ForwardStandardShader.vs",
                ["pixel-program"] = "ForwardStandardShader.ps",
                ["variant"] = "Mesh",
                ["base-color"] = "#FF4040FF"
            }));

        ShaderMaterialAsset materialAsset = Assert.IsType<ShaderMaterialAsset>(global::helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        MaterialConstantBufferAsset baseColorBuffer = Assert.Single(materialAsset.ConstantBuffers);
        float[] channels = ReadFloat4(baseColorBuffer.Data);

        Assert.Equal(1f, channels[0]);
        Assert.Equal(64f / 255f, channels[1], 5);
        Assert.Equal(64f / 255f, channels[2], 5);
        Assert.Equal(1f, channels[3]);
    }

    /// <summary>
    /// Verifies the builder copies staged payloads into the output root.
    /// </summary>
    [Fact]
    public async Task BuildAsync_copies_payloads_into_the_output_root() {
        string workingRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        string outputRoot = Path.Combine(workingRoot, "out");
        string sourceRoot = Path.Combine(workingRoot, "project");
        string generatedCoreRoot = Path.Combine(workingRoot, "generated-core");
        string sceneSourcePath = Path.Combine(sourceRoot, "scenes", "main-menu.hasset");
        string textureSourcePath = Path.Combine(sourceRoot, "assets", "textures", "checker.png");

        Directory.CreateDirectory(Path.GetDirectoryName(sceneSourcePath)!);
        Directory.CreateDirectory(Path.GetDirectoryName(textureSourcePath)!);
            Directory.CreateDirectory(generatedCoreRoot);
            File.WriteAllText(sceneSourcePath, "scene payload");
            File.WriteAllText(textureSourcePath, "texture payload");

        string previousDirectory = Directory.GetCurrentDirectory();
        try {
            Directory.SetCurrentDirectory(sourceRoot);

            PlatformBuildManifest manifest = new(
                1,
                "project",
                "1.0.0",
                "1.0.0",
                [
                    new PlatformBuildScene(
                        "startup",
                        "Startup",
                        "scenes/main-menu.hasset",
                        [],
                        [new KeyValuePair<string, string>("cooked-relative-path", "scenes/main-menu.hasset")])
                ],
                [
                    new PlatformBuildAsset(
                        "checker",
                        "Checker Texture",
                        "assets/textures/checker.png",
                        new PlatformBuildPayloadReference("checker-payload", "assets/textures/checker.png"),
                        [])
                ]);

            PlatformBuildRequest request = new(
                manifest,
                [new PlatformBuildTargetVariant("windows-default", "windows", "windows", "debug")],
                [new PlatformCookProfile(
                    "debug",
                    "Debug",
                    new PlatformCookProfileCapabilities(
                        "windows",
                        "raw",
                        "rgba",
                        "windows-scene-v1",
                        PlatformSerializationEndianness.LittleEndian))],
                outputRoot,
                Path.Combine(workingRoot, "tmp"),
                "debug",
                "directx11",
                string.Empty,
                new Dictionary<string, string>(),
                new Dictionary<string, string> {
                    ["default-width"] = "1280",
                    ["default-height"] = "720"
                },
                new Dictionary<string, string>(),
                generatedCoreRoot);

            RecordingNativeBuildExecutor nativeBuildExecutor = new();
            WindowsPlatformAssetBuilder builder = new(nativeBuildExecutor);
            RecordingProgressReporter progressReporter = new();
            RecordingDiagnosticReporter diagnosticReporter = new();

            PlatformBuildReport report = await builder.BuildAsync(request, progressReporter, diagnosticReporter, CancellationToken.None);

            Assert.True(report.Succeeded);
            Assert.Empty(diagnosticReporter.Diagnostics);
            Assert.True(nativeBuildExecutor.WasCalled);
            Assert.True(progressReporter.Updates.Count >= 3);
            Assert.True(File.Exists(Path.Combine(outputRoot, "cooked", "scenes", "main-menu.hasset")));
            Assert.True(File.Exists(Path.Combine(outputRoot, "cooked", "assets", "textures", "checker.png")));
            Assert.True(File.Exists(Path.Combine(outputRoot, "helengine_windows.exe")));
            Assert.True(File.Exists(Path.Combine(workingRoot, "tmp", "windows-build-manifest.json")));
            Assert.False(File.Exists(Path.Combine(outputRoot, "windows-build-manifest.json")));
        } finally {
            try {
                Directory.SetCurrentDirectory(previousDirectory);
            } catch {
            }

            try {
                if (Directory.Exists(workingRoot)) {
                    Directory.Delete(workingRoot, recursive: true);
                }
            } catch {
            }
        }
    }

    /// <summary>
    /// Verifies the builder stages builder-owned cook outputs like externalized font atlases into the final package.
    /// </summary>
    [Fact]
    public async Task BuildAsync_copies_platform_cook_work_item_outputs_into_the_output_root() {
        string workingRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        string outputRoot = Path.Combine(workingRoot, "out");
        string sourceRoot = Path.Combine(workingRoot, "project");
        string generatedCoreRoot = Path.Combine(workingRoot, "generated-core");
        string sceneSourcePath = Path.Combine(sourceRoot, "scenes", "main-menu.hasset");
        string fontAtlasSourcePath = Path.Combine(sourceRoot, "generated", "packaged-font-atlases", "fredoka.hasset");

        Directory.CreateDirectory(Path.GetDirectoryName(sceneSourcePath)!);
        Directory.CreateDirectory(Path.GetDirectoryName(fontAtlasSourcePath)!);
        Directory.CreateDirectory(generatedCoreRoot);
        File.WriteAllText(sceneSourcePath, "scene payload");
        File.WriteAllText(fontAtlasSourcePath, "font atlas payload");

        string previousDirectory = Directory.GetCurrentDirectory();
        try {
            Directory.SetCurrentDirectory(sourceRoot);

            PlatformBuildManifest manifest = new(
                1,
                "project",
                "1.0.0",
                "1.0.0",
                "windows",
                "1.0.0",
                "startup",
                [
                    new PlatformBuildScene(
                        "startup",
                        "Startup",
                        "scenes/main-menu.hasset",
                        [],
                        [new KeyValuePair<string, string>("cooked-relative-path", "scenes/main-menu.hasset")])
                ],
                [],
                [],
                [],
                [],
                new PlatformContainerWritePlan(string.Empty, []),
                [
                    new PlatformCookWorkItem(
                        "fredoka-font-atlas",
                        "generated/packaged-font-atlases/fredoka.hasset",
                        "font-atlas-texture",
                        "windows",
                        "texture",
                        "cooked/fonts/fredoka.hetex",
                        "fredoka-atlas",
                        "hash-source",
                        "hash-settings",
                        "{}",
                        [])
                ]);

            PlatformBuildRequest request = new(
                manifest,
                [new PlatformBuildTargetVariant("windows-default", "windows", "windows", "debug")],
                [new PlatformCookProfile(
                    "debug",
                    "Debug",
                    new PlatformCookProfileCapabilities(
                        "windows",
                        "raw",
                        "rgba",
                        "windows-scene-v1",
                        PlatformSerializationEndianness.LittleEndian))],
                outputRoot,
                Path.Combine(workingRoot, "tmp"),
                "debug",
                "directx11",
                string.Empty,
                new Dictionary<string, string>(),
                new Dictionary<string, string> {
                    ["default-width"] = "1280",
                    ["default-height"] = "720"
                },
                new Dictionary<string, string>(),
                generatedCoreRoot);

            RecordingNativeBuildExecutor nativeBuildExecutor = new();
            WindowsPlatformAssetBuilder builder = new(nativeBuildExecutor);
            RecordingProgressReporter progressReporter = new();
            RecordingDiagnosticReporter diagnosticReporter = new();

            PlatformBuildReport report = await builder.BuildAsync(request, progressReporter, diagnosticReporter, CancellationToken.None);

            Assert.True(report.Succeeded);
            Assert.Empty(diagnosticReporter.Diagnostics);
            Assert.True(File.Exists(Path.Combine(outputRoot, "cooked", "fonts", "fredoka.hetex")));
            Assert.Equal("font atlas payload", File.ReadAllText(Path.Combine(outputRoot, "cooked", "fonts", "fredoka.hetex")));
        } finally {
            try {
                Directory.SetCurrentDirectory(previousDirectory);
            } catch {
            }

            try {
                if (Directory.Exists(workingRoot)) {
                    Directory.Delete(workingRoot, recursive: true);
                }
            } catch {
            }
        }
    }

    /// <summary>
    /// Verifies the builder runs the native Windows build step and copies the produced executable into the output root.
    /// </summary>
    [Fact]
    public async Task BuildAsync_runs_the_native_build_step_and_copies_the_executable() {
        string workingRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        string outputRoot = Path.Combine(workingRoot, "out");
        string sourceRoot = Path.Combine(workingRoot, "project");
        string generatedCoreRoot = Path.Combine(workingRoot, "generated-core");
        string sceneSourcePath = Path.Combine(sourceRoot, "scenes", "main-menu.hasset");

        Directory.CreateDirectory(Path.GetDirectoryName(sceneSourcePath)!);
        Directory.CreateDirectory(generatedCoreRoot);
        File.WriteAllText(sceneSourcePath, "scene payload");

        string previousDirectory = Directory.GetCurrentDirectory();
        try {
            Directory.SetCurrentDirectory(sourceRoot);

            PlatformBuildManifest manifest = new(
                1,
                "project",
                "1.0.0",
                "1.0.0",
                [
                    new PlatformBuildScene(
                        "startup",
                        "Startup",
                        "scenes/main-menu.hasset",
                        [],
                        [new KeyValuePair<string, string>("cooked-relative-path", "scenes/main-menu.hasset")])
                ],
                []);

            PlatformBuildRequest request = new(
                manifest,
                [new PlatformBuildTargetVariant("windows-default", "windows", "windows", "debug")],
                [new PlatformCookProfile(
                    "debug",
                    "Debug",
                    new PlatformCookProfileCapabilities(
                        "windows",
                        "raw",
                        "rgba",
                        "windows-scene-v1",
                        PlatformSerializationEndianness.LittleEndian))],
                outputRoot,
                Path.Combine(workingRoot, "tmp"),
                "debug",
                "directx11",
                string.Empty,
                new Dictionary<string, string>(),
                new Dictionary<string, string> {
                    ["default-width"] = "1280",
                    ["default-height"] = "720"
                },
                new Dictionary<string, string>(),
                generatedCoreRoot);

            RecordingNativeBuildExecutor nativeBuildExecutor = new();
            WindowsPlatformAssetBuilder builder = new(nativeBuildExecutor);
            RecordingProgressReporter progressReporter = new();
            RecordingDiagnosticReporter diagnosticReporter = new();

            PlatformBuildReport report = await builder.BuildAsync(request, progressReporter, diagnosticReporter, CancellationToken.None);

            Assert.True(report.Succeeded);
            Assert.Empty(diagnosticReporter.Diagnostics);
            Assert.True(progressReporter.Updates.Count >= 3);
            Assert.True(nativeBuildExecutor.WasCalled);
            Assert.Equal(Path.Combine(sourceRoot, "code"), nativeBuildExecutor.StagedCodeRootPath);
            Assert.True(File.Exists(Path.Combine(outputRoot, "helengine_windows.exe")));
            Assert.True(File.Exists(Path.Combine(outputRoot, "cooked", "scenes", "main-menu.hasset")));
            Assert.True(File.Exists(Path.Combine(workingRoot, "tmp", "windows-build-manifest.json")));
            Assert.False(File.Exists(Path.Combine(outputRoot, "windows-build-manifest.json")));
        } finally {
            try {
                Directory.SetCurrentDirectory(previousDirectory);
            } catch {
            }

            try {
                if (Directory.Exists(workingRoot)) {
                    Directory.Delete(workingRoot, recursive: true);
                }
            } catch {
            }
        }
    }

    /// <summary>
    /// Verifies the builder succeeds when the requested working root matches the staged package current directory.
    /// </summary>
    [Fact]
    public async Task BuildAsync_succeeds_when_working_root_matches_staging_root() {
        string workingRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        string outputRoot = Path.Combine(workingRoot, "out");
        string sourceRoot = Path.Combine(workingRoot, "package");
        string generatedCoreRoot = Path.Combine(workingRoot, "generated-core");
        string sceneSourcePath = Path.Combine(sourceRoot, "scenes", "main-menu.hasset");

        Directory.CreateDirectory(Path.GetDirectoryName(sceneSourcePath)!);
        Directory.CreateDirectory(generatedCoreRoot);
        File.WriteAllText(sceneSourcePath, "scene payload");

        string previousDirectory = Directory.GetCurrentDirectory();
        try {
            Directory.SetCurrentDirectory(sourceRoot);

            PlatformBuildManifest manifest = new(
                1,
                "project",
                "1.0.0",
                "1.0.0",
                [
                    new PlatformBuildScene(
                        "startup",
                        "Startup",
                        "scenes/main-menu.hasset",
                        [],
                        [new KeyValuePair<string, string>("cooked-relative-path", "scenes/main-menu.hasset")])
                ],
                []);

            PlatformBuildRequest request = new(
                manifest,
                [new PlatformBuildTargetVariant("windows-default", "windows", "windows", "debug")],
                [new PlatformCookProfile(
                    "debug",
                    "Debug",
                    new PlatformCookProfileCapabilities(
                        "windows",
                        "raw",
                        "rgba",
                        "windows-scene-v1",
                        PlatformSerializationEndianness.LittleEndian))],
                outputRoot,
                sourceRoot,
                "debug",
                "directx11",
                string.Empty,
                new Dictionary<string, string>(),
                new Dictionary<string, string> {
                    ["default-width"] = "1280",
                    ["default-height"] = "720"
                },
                new Dictionary<string, string>(),
                generatedCoreRoot);

            RecordingNativeBuildExecutor nativeBuildExecutor = new();
            WindowsPlatformAssetBuilder builder = new(nativeBuildExecutor);
            RecordingProgressReporter progressReporter = new();
            RecordingDiagnosticReporter diagnosticReporter = new();

            PlatformBuildReport report = await builder.BuildAsync(request, progressReporter, diagnosticReporter, CancellationToken.None);

            Assert.True(report.Succeeded);
            Assert.Empty(diagnosticReporter.Diagnostics);
            Assert.True(nativeBuildExecutor.WasCalled);
            Assert.True(File.Exists(Path.Combine(outputRoot, "helengine_windows.exe")));
            Assert.True(File.Exists(Path.Combine(outputRoot, "cooked", "scenes", "main-menu.hasset")));
            Assert.True(File.Exists(Path.Combine(sourceRoot, "_builder", "windows-build-manifest.json")));
        } finally {
            try {
                Directory.SetCurrentDirectory(previousDirectory);
            } catch {
            }

            try {
                if (Directory.Exists(workingRoot)) {
                    Directory.Delete(workingRoot, recursive: true);
                }
            } catch {
            }
        }
    }

    /// <summary>
    /// Reads one packed four-float constant-buffer payload into scalar channels for assertions.
    /// </summary>
    /// <param name="data">Packed constant-buffer payload.</param>
    /// <returns>Decoded scalar channels in RGBA order.</returns>
    static float[] ReadFloat4(byte[] data) {
        if (data == null) {
            throw new ArgumentNullException(nameof(data));
        }

        using MemoryStream stream = new(data);
        using EngineBinaryReader reader = EngineBinaryReader.Create(stream, EngineBinaryEndianness.LittleEndian);
        return [
            reader.ReadSingle(),
            reader.ReadSingle(),
            reader.ReadSingle(),
            reader.ReadSingle()
        ];
    }

    /// <summary>
    /// Reads one public instance string field via reflection so tests can fail cleanly before the field is implemented.
    /// </summary>
    /// <param name="instance">Object instance to inspect.</param>
    /// <param name="fieldName">Public instance field name.</param>
    /// <returns>Current string value.</returns>
    static string ReadStringField(object instance, string fieldName) {
        if (instance == null) {
            throw new ArgumentNullException(nameof(instance));
        } else if (string.IsNullOrWhiteSpace(fieldName)) {
            throw new ArgumentException("Field name must be provided.", nameof(fieldName));
        }

        System.Reflection.FieldInfo field = instance.GetType().GetField(fieldName, System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public);
        Assert.NotNull(field);
        return Assert.IsType<string>(field.GetValue(instance));
    }

    /// <summary>
    /// Records the repository and build roots used by the native build hook and synthesizes a dummy executable.
    /// </summary>
    sealed class RecordingNativeBuildExecutor : IWindowsNativeBuildExecutor {
        /// <summary>
        /// Gets whether the build hook was invoked.
        /// </summary>
        public bool WasCalled { get; private set; }

        /// <summary>
        /// Gets the last repository root provided by the builder.
        /// </summary>
        public string RepositoryRoot { get; private set; }

        /// <summary>
        /// Gets the last native build root provided by the builder.
        /// </summary>
        public string BuildRoot { get; private set; }

        /// <summary>
        /// Gets the generated core root passed by the builder.
        /// </summary>
        public string GeneratedCoreCppRootPath { get; private set; }

        /// <summary>
        /// Gets the staged generated code-module root passed by the builder.
        /// </summary>
        public string StagedCodeRootPath { get; private set; }

        /// <summary>
        /// Runs the fake native build step and returns a synthetic executable path.
        /// </summary>
        /// <param name="repositoryRoot">Repository root provided by the builder.</param>
        /// <param name="buildRoot">Native build root provided by the builder.</param>
        /// <param name="generatedCoreCppRootPath">Generated core root provided by the builder.</param>
        /// <param name="stagedCodeRootPath">Staged generated code-module root provided by the builder.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        /// <returns>Absolute path to the fake native executable.</returns>
        public string Build(string repositoryRoot, string buildRoot, string generatedCoreCppRootPath, string stagedCodeRootPath, CancellationToken cancellationToken) {
            WasCalled = true;
            RepositoryRoot = repositoryRoot;
            BuildRoot = buildRoot;
            GeneratedCoreCppRootPath = generatedCoreCppRootPath;
            StagedCodeRootPath = stagedCodeRootPath;

            Directory.CreateDirectory(buildRoot);
            string executablePath = Path.Combine(buildRoot, "helengine_windows.exe");
            File.WriteAllText(executablePath, "fake executable");
            return executablePath;
        }
    }
}
