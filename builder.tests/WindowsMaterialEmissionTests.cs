using helengine.baseplatform.Requests;

namespace helengine.windows.builder.tests;

/// <summary>Verifies emission survives the actual Windows binary material cook, including non-emissive defaults.</summary>
public sealed class WindowsMaterialEmissionTests {
    /// <summary>Checks the serialized GPU constant and texture dependency rather than only authored field values.</summary>
    [Theory]
    [InlineData(null, "", 0, 0, 0, 0)]
    [InlineData("#FF9445FF", "", 255, 148, 69, 255)]
    [InlineData("#FFE5C080", "Textures/LampGlow.png", 255, 229, 192, 128)]
    public void Cook_preserves_emission(string color, string texture, int red, int green, int blue, int alpha) {
        Dictionary<string, string> fields = new() {
            ["shader-asset-id"] = "ForwardStandardShader", ["vertex-program"] = "ForwardStandardShader.vs",
            ["pixel-program"] = "ForwardStandardShader.ps", ["variant"] = "Mesh",
            ["emissive-texture-id"] = texture
        };
        if (color != null) { fields.Add("emissive-color", color); }
        WindowsPlatformAssetBuilder builder = new();
        var schema = builder.Definition.MaterialSchemas[0];
        Assert.Contains(schema.Fields, field => field.FieldId == "emissive-color" && field.FieldKind == helengine.baseplatform.Definitions.PlatformMaterialFieldKind.Color);
        Assert.Contains(schema.Fields, field => field.FieldId == "emissive-texture-id" && field.FieldKind == helengine.baseplatform.Definitions.PlatformMaterialFieldKind.AssetReference);
        // The editor retains only declared fields while selecting this builder schema.
        Dictionary<string, string> normalized = schema.Fields.ToDictionary(field => field.FieldId,
            field => fields.TryGetValue(field.FieldId, out string value) ? value : field.DefaultValue);
        normalized["variant"] = "Mesh"; // The scene packager supplies the resolved shader variant after schema selection.
        var result = builder.CookMaterial(new PlatformMaterialCookRequest(
            "Materials/Lamp.helmat", "Materials/Lamp.helmat", "windows", "debug", "directx11", "standard-shader", normalized,
            numericPolicy: NumericPrecisionPolicy.Default));
        using ShaderMaterialAsset material = Assert.IsType<ShaderMaterialAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        MaterialConstantBufferAsset buffer = Assert.Single(material.ConstantBuffers, value => value.Name == "EmissiveColorBuffer");
        Assert.Equal(16, buffer.Data.Length);
        Assert.Equal(red / 255f, BitConverter.ToSingle(buffer.Data, 0));
        Assert.Equal(green / 255f, BitConverter.ToSingle(buffer.Data, 4));
        Assert.Equal(blue / 255f, BitConverter.ToSingle(buffer.Data, 8));
        Assert.Equal(alpha / 255f, BitConverter.ToSingle(buffer.Data, 12));
        Assert.Equal(texture, material.EmissiveTextureAssetId);
    }
}
