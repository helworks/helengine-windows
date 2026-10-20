using helengine;
using helengine.baseplatform.Player;
using helengine.baseplatform.Requests;
using helengine.windows.builder;

namespace helengine.windows.builder.tests;

/// <summary>Exercises the Windows Play adapter against real cooked assets and the runtime content manager.</summary>
public sealed class WindowsPlayerAdapterTests : IDisposable {
    /// <summary>Scratch content tree owned by this fixture, always under the user-designated temporary directory.</summary>
    readonly string Root = Path.Combine(@"C:\temp\helengine-windows\tests", Guid.NewGuid().ToString("N"));

    /// <summary>Creates an empty content root for the adapter.</summary>
    public WindowsPlayerAdapterTests() {
        Directory.CreateDirectory(Root);
    }

    /// <summary>Deletes only this fixture's scratch content.</summary>
    public void Dispose() {
        Directory.Delete(Root, true);
    }

    /// <summary>The advertised adapter can be constructed through the same type name used by the player loader.</summary>
    [Fact]
    public void Capability_ResolvesConcreteAdapterAndRuntimeIdentity() {
        WindowsPlatformAssetBuilder builder = new();
        var capability = builder.Definition.PlayerCapability;
        Type adapterType = typeof(WindowsPlatformAssetBuilder).Assembly.GetType(capability.AdapterTypeName, true);

        Assert.IsAssignableFrom<IPlatformPlayerAdapter>(Activator.CreateInstance(adapterType));
        Assert.True(capability.SupportsPlay);
        Assert.Equal("windows-staging", capability.ContentRootRelativePath);
        Assert.Equal(builder.Descriptor.TargetPlatformId, capability.RuntimePlatformName);
        Assert.Equal(WindowsRuntimePlatformIdentity.Version, capability.RuntimePlatformVersion);
        Assert.Equal(60d, capability.TargetFrameRate);
        Assert.Equal(helengine.baseplatform.Definitions.PlayerInputDevices.Keyboard
            | helengine.baseplatform.Definitions.PlayerInputDevices.Mouse
            | helengine.baseplatform.Definitions.PlayerInputDevices.Gamepad, capability.InputDevices);
    }

    /// <summary>The Windows cooker and player agree on the base-color channel order and imported texture path.</summary>
    [Fact]
    public void Material_RoundTripsWindowsCooker() {
        WriteAsset("cooked/imported/checker", new TextureAsset {
            Id = "checker", Width = 1, Height = 1, Colors = [20, 40, 60, 255]
        });
        byte[] bytes = new WindowsPlatformAssetBuilder().CookMaterial(new PlatformMaterialCookRequest(
            "test", "materials/test.helmat", "windows", "debug", "directx11", "standard-shader",
            new Dictionary<string, string> {
                ["shader-asset-id"] = "ForwardStandardShader", ["vertex-program"] = "VSMain",
                ["pixel-program"] = "PSMain", ["variant"] = "Standard", ["base-color"] = "#33669980",
                ["texture-id"] = "checker"
            }, numericPolicy: global::helengine.NumericPrecisionPolicy.Default)).CookedMaterialBytes;
        WindowsPlayerAdapter adapter = CreateAdapter();

        PlayerMaterialDescription result = adapter.DecodeMaterial(new MemoryStream(bytes), "cooked/materials/test.hasset");

        Assert.Equal((byte)0x33, result.BaseColorR);
        Assert.Equal((byte)0x66, result.BaseColorG);
        Assert.Equal((byte)0x99, result.BaseColorB);
        Assert.Equal((byte)0x80, result.BaseColorA);
        Assert.Equal("cooked/imported/checker", result.DiffuseTextureRelativePath);
        Assert.Equal(PlayerMaterialRenderClass.Opaque, result.RenderClass);
        Assert.False(result.DoubleSided);
    }

    /// <summary>Authored transparency and two-sided geometry survive the player's neutral material description.</summary>
    [Fact]
    public void Material_PreservesBlendAndCullState() {
        ShaderMaterialAsset source = CreateMaterial();
        source.RenderState.BlendMode = MaterialBlendMode.AlphaBlend;
        source.RenderState.CullMode = MaterialCullMode.None;

        PlayerMaterialDescription result = CreateAdapter().DecodeMaterial(
            new MemoryStream(helengine.files.AssetSerializer.SerializeToBytes(source)), "cooked/material.hasset");

        Assert.Equal(PlayerMaterialRenderClass.Transparent, result.RenderClass);
        Assert.True(result.DoubleSided);
        Assert.Equal(string.Empty, result.DiffuseTextureRelativePath);
    }

    /// <summary>A missing staged texture is reported with the material and texture paths instead of losing the binding.</summary>
    [Fact]
    public void Material_MissingTextureFailsWithPaths() {
        ShaderMaterialAsset source = CreateMaterial();
        source.DiffuseTextureAssetId = "missing";

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() => CreateAdapter().DecodeMaterial(
            new MemoryStream(helengine.files.AssetSerializer.SerializeToBytes(source)), "cooked/material.hasset"));

        Assert.Contains("cooked/material.hasset", error.Message);
        Assert.Contains("cooked/imported/missing", error.Message);
    }

    /// <summary>A malformed color buffer is rejected rather than replaced with a white material.</summary>
    [Fact]
    public void Material_MalformedColorFailsWithMaterialPath() {
        ShaderMaterialAsset source = CreateMaterial();
        source.ConstantBuffers[0].Data = [0, 1, 2];

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() => CreateAdapter().DecodeMaterial(
            new MemoryStream(helengine.files.AssetSerializer.SerializeToBytes(source)), "cooked/broken.hasset"));

        Assert.Contains("cooked/broken.hasset", error.Message);
    }

    /// <summary>The decoder also works with streaming content sources without seeking or buffering the whole payload.</summary>
    [Fact]
    public void Material_ReadsNonSeekableStream() {
        using MemoryStream compressed = new();
        using (System.IO.Compression.GZipStream writer = new(compressed, System.IO.Compression.CompressionMode.Compress, true)) {
            writer.Write(helengine.files.AssetSerializer.SerializeToBytes(CreateMaterial()));
        }
        compressed.Position = 0;
        using System.IO.Compression.GZipStream stream = new(compressed, System.IO.Compression.CompressionMode.Decompress);

        PlayerMaterialDescription result = CreateAdapter().DecodeMaterial(stream, "cooked/streamed.hasset");

        Assert.False(stream.CanSeek);
        Assert.Equal((byte)255, result.BaseColorR);
        Assert.Equal((byte)255, result.BaseColorA);
    }

    /// <summary>The color conversion rejects NaN rather than producing a different visible color.</summary>
    [Fact]
    public void Material_RejectsNonFiniteColor() {
        ShaderMaterialAsset source = CreateMaterial();
        BitConverter.GetBytes(float.NaN).CopyTo(source.ConstantBuffers[0].Data, 0);

        Assert.Throws<InvalidOperationException>(() => CreateAdapter().DecodeMaterial(
            new MemoryStream(helengine.files.AssetSerializer.SerializeToBytes(source)), "cooked/invalid.hasset"));
    }

    /// <summary>Standard runtime readers retain Windows texture bytes and model arrays without platform conversion.</summary>
    [Fact]
    public void Content_UsesSharedTextureAndModelReadersWithoutAliases() {
        TextureAsset sourceTexture = new() {
            Id = "texture", Width = 1, Height = 1,
            ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8,
            Colors = [20, 40, 60, 127]
        };
        ModelAsset sourceModel = ModelUtils.GenerateCubeMesh(new float3(0, 0, 0), new float3(1, 1, 1));
        WriteAsset("cooked/texture.hasset", sourceTexture);
        WriteAsset("cooked/cube.hasset", sourceModel);
        WindowsPlayerAdapter adapter = CreateAdapter();
        using ContentManager content = new(new HostFileSystemContentStreamSource(Root));
        adapter.RegisterContentProcessors(content);
        RuntimeContentManagerConfiguration.ConfigureSharedAssetContentManager(content, null);

        TextureAsset texture = content.Load<TextureAsset>("cooked/texture.hasset", RuntimeContentProcessorIds.TextureAsset);
        ModelAsset model = content.Load<ModelAsset>("cooked/cube.hasset", RuntimeContentProcessorIds.ModelAsset);

        Assert.Equal(sourceTexture.Colors, texture.Colors);
        Assert.Equal(sourceModel.Positions, model.Positions);
        Assert.Equal(sourceModel.Indices16, model.Indices16);
        Assert.Empty(adapter.BuildContentAliases());
    }

    /// <summary>Content-dependent operations fail clearly when the player has not bound a staging root.</summary>
    [Fact]
    public void Adapter_RejectsUninitializedUseAndMissingRoot() {
        Assert.Throws<InvalidOperationException>(() => new WindowsPlayerAdapter().BuildContentAliases());
        Assert.Throws<DirectoryNotFoundException>(() => new WindowsPlayerAdapter().Initialize(Path.Combine(Root, "missing")));
    }

    /// <summary>Play settings declare their defaults in metadata and the adapter uses the resolved dimensions.</summary>
    [Fact]
    public void PlaySettings_DeclareDefaultsAndApplySelectedResolution() {
        var capability = WindowsPlatformDefinitionFactory.Create().PlayerCapability;
        Assert.Equal("1280", capability.FindPlaySetting("render-width").DefaultValue);
        Assert.Equal("720", capability.FindPlaySetting("render-height").DefaultValue);

        PlayerRenderSize size = CreateAdapter().ApplyPlaySettings(CreatePlaySettings("640", "480"));

        Assert.Equal(640, size.Width);
        Assert.Equal(480, size.Height);
        Assert.Equal(1d, size.PixelAspect);
    }

    /// <summary>Absent or invalid dimensions cannot silently change to a default or overflow the render target.</summary>
    [Theory]
    [InlineData("", "720")]
    [InlineData("0", "720")]
    [InlineData("-1", "720")]
    [InlineData("8193", "720")]
    [InlineData("NaN", "720")]
    [InlineData("1280", "1.5")]
    public void PlaySettings_RejectInvalidDimensions(string width, string height) {
        Assert.Throws<InvalidOperationException>(() => CreateAdapter().ApplyPlaySettings(CreatePlaySettings(width, height)));
    }

    /// <summary>The default resolution belongs to metadata; the adapter never manufactures missing resolved values.</summary>
    [Fact]
    public void PlaySettings_RejectMissingResolvedValues() {
        PlayerPlaySettings settings = new(PlayerWindowScale.One, PlayerWindowMonitor.Primary, 1, false, true, false, 60,
            new Dictionary<string, string>(), null);

        Assert.Throws<InvalidOperationException>(() => CreateAdapter().ApplyPlaySettings(settings));
    }

    /// <summary>Creates resolved Play settings with explicit Windows render dimensions.</summary>
    static PlayerPlaySettings CreatePlaySettings(string width, string height) {
        return new PlayerPlaySettings(PlayerWindowScale.Fit, PlayerWindowMonitor.Primary, 1, false, true, false, 60d,
            new Dictionary<string, string> { ["render-width"] = width, ["render-height"] = height }, null);
    }

    /// <summary>Creates an adapter bound to this fixture's existing content tree.</summary>
    WindowsPlayerAdapter CreateAdapter() {
        WindowsPlayerAdapter adapter = new();
        adapter.Initialize(Root);
        return adapter;
    }

    /// <summary>Creates a standard opaque white material as produced by the Windows cooker.</summary>
    static ShaderMaterialAsset CreateMaterial() {
        return new ShaderMaterialAsset {
            Id = "test", ShaderAssetId = "ForwardStandardShader", VertexProgram = "VSMain", PixelProgram = "PSMain",
            Variant = "Standard", ConstantBuffers = [new MaterialConstantBufferAsset {
                Name = "BaseColorBuffer", Data = BitConverter.GetBytes(1f).Concat(BitConverter.GetBytes(1f))
                    .Concat(BitConverter.GetBytes(1f)).Concat(BitConverter.GetBytes(1f)).ToArray()
            }]
        };
    }

    /// <summary>Serializes a real runtime asset to the fixture's cooked tree.</summary>
    void WriteAsset(string relativePath, Asset asset) {
        string path = Path.Combine(Root, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        File.WriteAllBytes(path, helengine.files.AssetSerializer.SerializeToBytes(asset));
    }
}
