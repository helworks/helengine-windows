using helengine;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.windows.builder;

/// <summary>
/// Implements the Windows platform asset builder contract.
/// </summary>
public sealed class WindowsPlatformAssetBuilder : IPlatformAssetBuilder {
    /// <summary>
    /// Stable material field identifier used for the authored base color.
    /// </summary>
    const string BaseColorFieldId = "base-color";

    /// <summary>
    /// Constant-buffer name used for the authored base color payload.
    /// </summary>
    const string BaseColorBufferName = "BaseColorBuffer";

    /// <summary>
    /// Native build executor used to invoke the Windows CMake build.
    /// </summary>
    readonly IWindowsNativeBuildExecutor NativeBuildExecutor;

    /// <summary>
    /// Initializes one Windows builder instance with the current platform metadata.
    /// </summary>
    public WindowsPlatformAssetBuilder() {
        NativeBuildExecutor = WindowsNativeBuildExecutor.Instance;
        Descriptor = CreateDescriptor();
        Definition = WindowsPlatformDefinitionFactory.Create();
    }

    /// <summary>
    /// Initializes one Windows builder instance with the current platform metadata and a custom native build executor.
    /// </summary>
    /// <param name="nativeBuildExecutor">Custom native build executor used by tests.</param>
    internal WindowsPlatformAssetBuilder(IWindowsNativeBuildExecutor nativeBuildExecutor) {
        NativeBuildExecutor = nativeBuildExecutor ?? WindowsNativeBuildExecutor.Instance;
        Descriptor = CreateDescriptor();
        Definition = WindowsPlatformDefinitionFactory.Create();
    }

    /// <summary>
    /// Gets the explicit builder descriptor for the Windows builder assembly.
    /// </summary>
    public PlatformBuilderDescriptor Descriptor { get; }

    /// <summary>
    /// Gets the typed Windows platform definition exposed to the editor.
    /// </summary>
    public PlatformDefinition Definition { get; }

    /// <summary>
    /// Returns the builder-owned cooked material payload for one Windows material schema request.
    /// </summary>
    /// <param name="request">Material translation request to process.</param>
    /// <returns>Cooked material payload and shader dependencies for the request.</returns>
    public PlatformMaterialCookResult CookMaterial(PlatformMaterialCookRequest request) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        }

        string shaderAssetId = ReadRequiredField(request.FieldValues, "shader-asset-id");
        string vertexProgram = ReadRequiredField(request.FieldValues, "vertex-program");
        string pixelProgram = ReadRequiredField(request.FieldValues, "pixel-program");
        string variant = ReadRequiredField(request.FieldValues, "variant");
        string baseColor = request.FieldValues.TryGetValue(BaseColorFieldId, out string authoredBaseColor) ? authoredBaseColor : "#ffffff";
        string diffuseTextureAssetId = request.FieldValues.TryGetValue("texture-id", out string authoredTextureAssetId) && !string.IsNullOrWhiteSpace(authoredTextureAssetId)
            ? authoredTextureAssetId
            : string.Empty;
        bool castsShadows = ReadOptionalBooleanField(request.FieldValues, "casts-shadow", true);
        bool receivesShadows = ReadOptionalBooleanField(request.FieldValues, "receives-shadow", true);

        MaterialAsset materialAsset = new MaterialAsset {
            Id = request.MaterialAssetId,
            ShaderAssetId = shaderAssetId,
            VertexProgram = vertexProgram,
            PixelProgram = pixelProgram,
            Variant = variant,
            DiffuseTextureAssetId = diffuseTextureAssetId,
            CastsShadows = castsShadows,
            ReceivesShadows = receivesShadows,
            RenderState = new MaterialRenderState(),
            ConstantBuffers = [
                new MaterialConstantBufferAsset {
                    Name = BaseColorBufferName,
                    Data = CreateFloat4ConstantBufferData(ParseBaseColor(baseColor))
                }
            ]
        };

        return new PlatformMaterialCookResult(global::helengine.files.AssetSerializer.SerializeToBytes(materialAsset), [shaderAssetId]);
    }

    /// <summary>
    /// Executes one Windows build request through the staged payload workspace.
    /// </summary>
    /// <param name="request">The resolved build request.</param>
    /// <param name="progressReporter">The progress reporter.</param>
    /// <param name="diagnosticReporter">The diagnostic reporter.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The final build report.</returns>
    public Task<PlatformBuildReport> BuildAsync(
        PlatformBuildRequest request,
        IPlatformBuildProgressReporter progressReporter,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        CancellationToken cancellationToken) {
        return WindowsBuildWorkspace.BuildAsync(request, progressReporter, diagnosticReporter, NativeBuildExecutor, cancellationToken);
    }

    /// <summary>
    /// Creates the standard builder descriptor used by both constructors.
    /// </summary>
    /// <returns>Builder descriptor for the Windows plugin.</returns>
    static PlatformBuilderDescriptor CreateDescriptor() {
        return new PlatformBuilderDescriptor(
            "helengine.windows.builder",
            "1.0.0",
            "windows",
            new EngineCompatibilityRange("1.0.0", "999.0.0"),
            new ManifestCompatibilityRange(1, 1),
            ["windows"],
            ["debug", "release"]);
    }

    /// <summary>
    /// Reads one required material field from the builder-owned field map.
    /// </summary>
    /// <param name="fieldValues">Serialized material field values keyed by field id.</param>
    /// <param name="fieldId">Field identifier to read.</param>
    /// <returns>Resolved field value.</returns>
    static string ReadRequiredField(IReadOnlyDictionary<string, string> fieldValues, string fieldId) {
        if (fieldValues == null) {
            throw new ArgumentNullException(nameof(fieldValues));
        } else if (string.IsNullOrWhiteSpace(fieldId)) {
            throw new ArgumentException("Field id must be provided.", nameof(fieldId));
        }

        string value;
        if (!fieldValues.TryGetValue(fieldId, out value) || string.IsNullOrWhiteSpace(value)) {
            throw new InvalidOperationException($"Missing required material field '{fieldId}'.");
        }

        return value;
    }

    /// <summary>
    /// Reads an optional boolean material field from the builder-owned field map.
    /// </summary>
    /// <param name="fieldValues">Serialized material field values keyed by field id.</param>
    /// <param name="fieldId">Field identifier to read.</param>
    /// <param name="defaultValue">Value returned when the field is missing or blank.</param>
    /// <returns>Resolved boolean value.</returns>
    static bool ReadOptionalBooleanField(IReadOnlyDictionary<string, string> fieldValues, string fieldId, bool defaultValue) {
        if (fieldValues == null) {
            throw new ArgumentNullException(nameof(fieldValues));
        } else if (string.IsNullOrWhiteSpace(fieldId)) {
            throw new ArgumentException("Field id must be provided.", nameof(fieldId));
        }

        string value;
        if (!fieldValues.TryGetValue(fieldId, out value) || string.IsNullOrWhiteSpace(value)) {
            return defaultValue;
        }

        if (!bool.TryParse(value, out bool parsedValue)) {
            throw new InvalidOperationException($"Material field '{fieldId}' must be a boolean value.");
        }

        return parsedValue;
    }

    /// <summary>
    /// Parses one serialized base-color string into a normalized floating-point color.
    /// </summary>
    /// <param name="serializedColor">Serialized color string in <c>#RRGGBB</c> or <c>#RRGGBBAA</c> form.</param>
    /// <returns>Normalized color value.</returns>
    static float4 ParseBaseColor(string serializedColor) {
        if (string.IsNullOrWhiteSpace(serializedColor)) {
            return new float4(1f, 1f, 1f, 1f);
        }

        string normalized = serializedColor.Trim();
        if (normalized.StartsWith("#", StringComparison.Ordinal)) {
            normalized = normalized.Substring(1);
        }

        if (normalized.Length != 6 && normalized.Length != 8) {
            throw new InvalidOperationException("Base color must use #RRGGBB or #RRGGBBAA.");
        }

        try {
            byte red = Convert.ToByte(normalized.Substring(0, 2), 16);
            byte green = Convert.ToByte(normalized.Substring(2, 2), 16);
            byte blue = Convert.ToByte(normalized.Substring(4, 2), 16);
            byte alpha = normalized.Length == 8
                ? Convert.ToByte(normalized.Substring(6, 2), 16)
                : (byte)255;

            return new float4(
                red / 255f,
                green / 255f,
                blue / 255f,
                alpha / 255f);
        } catch (FormatException ex) {
            throw new InvalidOperationException("Base color must use #RRGGBB or #RRGGBBAA.", ex);
        }
    }

    /// <summary>
    /// Packs one floating-point color into a 16-byte constant-buffer payload.
    /// </summary>
    /// <param name="value">Normalized color value to encode.</param>
    /// <returns>Packed constant-buffer bytes.</returns>
    static byte[] CreateFloat4ConstantBufferData(float4 value) {
        using MemoryStream stream = new MemoryStream();
        using EngineBinaryWriter writer = EngineBinaryWriter.Create(stream, EngineBinaryEndianness.LittleEndian);
        writer.WriteSingle(value.X);
        writer.WriteSingle(value.Y);
        writer.WriteSingle(value.Z);
        writer.WriteSingle(value.W);
        return stream.ToArray();
    }
}



