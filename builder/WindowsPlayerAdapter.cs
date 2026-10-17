using System.Buffers.Binary;
using helengine.baseplatform.Player;

namespace helengine.windows.builder;

/// <summary>
/// Loads Windows-cooked content in the shared desktop Play host. Textures and models retain their runtime format;
/// shader materials are translated to the shared player's standard material representation.
/// </summary>
public sealed class WindowsPlayerAdapter : IPlatformPlayerAdapter {
    /// <summary>Absolute staging root bound by the player before settings or content are accessed.</summary>
    string ContentRootPath;

    /// <summary>Validates and binds the existing Windows assets-only staging tree.</summary>
    /// <param name="contentRootPath">Staging directory produced by the Windows builder.</param>
    public void Initialize(string contentRootPath) {
        if (string.IsNullOrWhiteSpace(contentRootPath)) {
            throw new ArgumentException("Windows content root must be provided.", nameof(contentRootPath));
        } else if (ContentRootPath != null) {
            throw new InvalidOperationException("The Windows player adapter is already initialized.");
        }

        string fullPath = Path.GetFullPath(contentRootPath);
        if (!Directory.Exists(fullPath)) {
            throw new DirectoryNotFoundException($"Windows Play content root '{fullPath}' does not exist.");
        }

        ContentRootPath = fullPath;
    }

    /// <summary>Applies the Windows-specific render width and height selected in Play Settings.</summary>
    /// <param name="playSettings">Resolved settings with explicit Windows dimensions.</param>
    /// <returns>Square-pixel framebuffer size, independent of window magnification.</returns>
    public PlayerRenderSize ApplyPlaySettings(PlayerPlaySettings playSettings) {
        EnsureInitialized();
        return WindowsPlayerSettings.Resolve(playSettings);
    }

    /// <summary>Registers the existing runtime readers for Windows texture and model payloads.</summary>
    /// <param name="contentManager">Content manager configured by the player before shared registration.</param>
    public void RegisterContentProcessors(ContentManager contentManager) {
        EnsureInitialized();
        if (contentManager == null) {
            throw new ArgumentNullException(nameof(contentManager));
        }

        contentManager.RegisterProcessor<TextureAsset>(RuntimeContentProcessorIds.TextureAsset, new AssetContentProcessor<TextureAsset>());
        contentManager.RegisterProcessor<ModelAsset>(RuntimeContentProcessorIds.ModelAsset, new AssetContentProcessor<ModelAsset>());
    }

    /// <summary>
    /// Reads a Windows shader material and translates its base color, diffuse texture, blending and culling to the
    /// existing player contract. Custom shader programs and PBR parameters are not carried by that contract.
    /// </summary>
    /// <param name="cookedMaterial">Readable stream at the start of a cooked Windows material; seeking is unnecessary.</param>
    /// <param name="materialRelativePath">Content-relative path included in failures.</param>
    /// <returns>The material description consumed by the shared player renderer.</returns>
    public PlayerMaterialDescription DecodeMaterial(Stream cookedMaterial, string materialRelativePath) {
        EnsureInitialized();
        if (cookedMaterial == null) {
            throw new ArgumentNullException(nameof(cookedMaterial));
        } else if (string.IsNullOrWhiteSpace(materialRelativePath)) {
            throw new ArgumentException("Material path must be provided.", nameof(materialRelativePath));
        }

        ShaderMaterialAsset material = ShaderMaterialAssetBinarySerializer.Deserialize(cookedMaterial);
        try {
            MaterialConstantBufferAsset color = FindBaseColor(material, materialRelativePath);
            if (material.RenderState == null) {
                throw new InvalidOperationException($"Windows material '{materialRelativePath}' has no render state.");
            } else if (material.RenderState.CullMode != MaterialCullMode.Back && material.RenderState.CullMode != MaterialCullMode.None) {
                throw new NotSupportedException($"Windows material '{materialRelativePath}' uses culling '{material.RenderState.CullMode}', which the shared Play material contract cannot represent.");
            }

            PlayerMaterialRenderClass renderClass = material.RenderState.BlendMode switch {
                MaterialBlendMode.Opaque => PlayerMaterialRenderClass.Opaque,
                MaterialBlendMode.AlphaBlend => PlayerMaterialRenderClass.Transparent,
                _ => throw new NotSupportedException($"Windows material '{materialRelativePath}' uses unsupported blending '{material.RenderState.BlendMode}'.")
            };
            return new PlayerMaterialDescription(
                ReadChannel(color.Data, 0, materialRelativePath), ReadChannel(color.Data, 4, materialRelativePath),
                ReadChannel(color.Data, 8, materialRelativePath), ReadChannel(color.Data, 12, materialRelativePath),
                ResolveDiffuseTexture(material.DiffuseTextureAssetId, materialRelativePath), renderClass,
                material.RenderState.CullMode == MaterialCullMode.None);
        } finally {
            material.Dispose();
        }
    }

    /// <summary>Returns no aliases because Windows stages the same logical paths its scenes reference.</summary>
    /// <returns>An empty, ordinally compared path map.</returns>
    public IReadOnlyDictionary<string, string> BuildContentAliases() {
        EnsureInitialized();
        return new Dictionary<string, string>(StringComparer.Ordinal);
    }

    /// <summary>Finds the one standard float4 base-color buffer and rejects malformed or ambiguous payloads.</summary>
    /// <param name="material">Decoded Windows material.</param>
    /// <param name="path">Material path used in diagnostics.</param>
    /// <returns>The required sixteen-byte color buffer.</returns>
    static MaterialConstantBufferAsset FindBaseColor(ShaderMaterialAsset material, string path) {
        MaterialConstantBufferAsset result = null;
        foreach (MaterialConstantBufferAsset buffer in material.ConstantBuffers) {
            if (string.Equals(buffer.Name, "BaseColorBuffer", StringComparison.Ordinal)) {
                if (result != null) {
                    throw new InvalidOperationException($"Windows material '{path}' has duplicate BaseColorBuffer payloads.");
                }
                result = buffer;
            }
        }
        if (result == null || result.Data == null || result.Data.Length != 16) {
            throw new InvalidOperationException($"Windows material '{path}' needs a sixteen-byte BaseColorBuffer for the shared Play renderer.");
        }

        return result;
    }

    /// <summary>Quantizes one finite floating-point color channel to the shared player's 8-bit normalized range.</summary>
    /// <param name="data">Validated little-endian float4 buffer.</param>
    /// <param name="offset">Byte offset of the channel.</param>
    /// <param name="path">Material path used in diagnostics.</param>
    /// <returns>Rounded channel, with HDR values clamped to the existing player's normalized color range.</returns>
    static byte ReadChannel(byte[] data, int offset, string path) {
        double value = BinaryPrimitives.ReadSingleLittleEndian(data.AsSpan(offset, 4));
        if (!double.IsFinite(value)) {
            throw new InvalidOperationException($"Windows material '{path}' has a non-finite base-color channel.");
        }
        return (byte)Math.Round(Math.Clamp(value, 0d, 1d) * 255d, MidpointRounding.AwayFromZero);
    }

    /// <summary>Uses the same imported-texture location as the native shader material loader.</summary>
    /// <param name="textureId">Imported asset id, or empty for an untextured material.</param>
    /// <param name="materialPath">Material path used in missing-texture diagnostics.</param>
    /// <returns>Validated staged texture path, or an empty string.</returns>
    string ResolveDiffuseTexture(string textureId, string materialPath) {
        if (string.IsNullOrEmpty(textureId)) {
            return string.Empty;
        }

        string relativePath = CanonicalPackagedAssetPath.ValidateCanonical("cooked/imported/" + textureId);
        if (!File.Exists(Path.Combine(ContentRootPath, relativePath))) {
            throw new InvalidOperationException($"Windows material '{materialPath}' references texture '{relativePath}' missing from the Play staging tree.");
        }
        return relativePath;
    }

    /// <summary>Enforces the player's initialization-before-use contract.</summary>
    void EnsureInitialized() {
        if (ContentRootPath == null) {
            throw new InvalidOperationException("WindowsPlayerAdapter.Initialize must run before settings or content are accessed.");
        }
    }
}
