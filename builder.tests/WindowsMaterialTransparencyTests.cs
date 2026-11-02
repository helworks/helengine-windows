using helengine.baseplatform.Requests;

namespace helengine.windows.builder.tests;

/// <summary>Checks transparency and culling survive schema normalization and the actual cooked material binary.</summary>
public sealed class WindowsMaterialTransparencyTests {
    /// <summary>Exercises both glass opacities and opaque materials through the same schema filtering used by the editor.</summary>
    [Theory]
    [InlineData("alpha-blend", false, "#DEEEFF05", MaterialBlendMode.AlphaBlend, MaterialCullMode.Back, 5)]
    [InlineData("alpha-blend", true, "#F5FAFF20", MaterialBlendMode.AlphaBlend, MaterialCullMode.None, 32)]
    [InlineData("opaque", false, "#FFFFFFFF", MaterialBlendMode.Opaque, MaterialCullMode.Back, 255)]
    [InlineData("opaque", true, "#FFFFFFFF", MaterialBlendMode.Opaque, MaterialCullMode.None, 255)]
    public void Cook_preserves_authored_render_state(string alphaMode, bool doubleSided, string color,
        MaterialBlendMode blend, MaterialCullMode cull, int alpha) {
        WindowsPlatformAssetBuilder builder = new();
        Dictionary<string, string> fields = new() {
            ["shader-asset-id"] = "ForwardStandardShader", ["vertex-program"] = "ForwardStandardShader.vs",
            ["pixel-program"] = "ForwardStandardShader.ps", ["base-color"] = color,
            ["alpha-mode"] = alphaMode, ["double-sided"] = doubleSided ? "true" : "false"
        };
        var schema = builder.Definition.MaterialSchemas[0];
        Dictionary<string, string> normalized = schema.Fields.ToDictionary(field => field.FieldId,
            field => fields.TryGetValue(field.FieldId, out string value) ? value : field.DefaultValue);
        normalized["variant"] = "default";
        var result = builder.CookMaterial(new PlatformMaterialCookRequest("Materials/Glass.helmat", "Materials/Glass.helmat",
            "windows", "debug", "directx11", "standard-shader", normalized, numericPolicy: NumericPrecisionPolicy.Default));
        using ShaderMaterialAsset material = Assert.IsType<ShaderMaterialAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        Assert.Equal(blend, material.RenderState.BlendMode);
        Assert.Equal(cull, material.RenderState.CullMode);
        Assert.True(material.RenderState.DepthTestEnabled);
        Assert.Equal(alpha / 255f, BitConverter.ToSingle(Assert.Single(material.ConstantBuffers, item => item.Name == "BaseColorBuffer").Data, 12));
    }
}
