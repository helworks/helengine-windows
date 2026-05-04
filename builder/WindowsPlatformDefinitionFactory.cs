using helengine.baseplatform.Definitions;
using helengine.baseplatform.Profiles;

namespace helengine.windows.builder;

/// <summary>
/// Creates the typed Windows builder metadata consumed by the editor.
/// </summary>
public static class WindowsPlatformDefinitionFactory {
    /// <summary>
    /// Creates the current Windows DirectX platform definition.
    /// </summary>
    /// <returns>The Windows platform definition.</returns>
    public static PlatformDefinition Create() {
        return new PlatformDefinition(
            "windows",
            "Windows DirectX",
            [
                new PlatformBuildProfileDefinition(
                    "debug",
                    "Debug",
                    "Debug Windows player build",
                    "directx11",
                    "default",
                    [
                        new PlatformSettingDefinition(
                            "texture-scale-percent",
                            "Texture Scale Percent",
                            PlatformSettingKind.Text,
                            "100",
                            true,
                            []),
                        new PlatformSettingDefinition(
                            "shader-variant-pruning",
                            "Shader Variant Pruning",
                            PlatformSettingKind.Boolean,
                            "true",
                            true,
                            [])
                    ]),
                new PlatformBuildProfileDefinition(
                    "release",
                    "Release",
                    "Release Windows player build",
                    "directx11",
                    "default",
                    [
                        new PlatformSettingDefinition(
                            "texture-scale-percent",
                            "Texture Scale Percent",
                            PlatformSettingKind.Text,
                            "100",
                            true,
                            []),
                        new PlatformSettingDefinition(
                            "shader-variant-pruning",
                            "Shader Variant Pruning",
                            PlatformSettingKind.Boolean,
                            "true",
                            true,
                            [])
                    ])
            ],
            [
                new PlatformGraphicsProfileDefinition(
                    "directx11",
                    "DirectX 11",
                    "Current Windows rendering backend",
                    [
                        new PlatformSettingDefinition(
                            "default-width",
                            "Default Width",
                            PlatformSettingKind.Text,
                            "1280",
                            true,
                            []),
                        new PlatformSettingDefinition(
                            "default-height",
                            "Default Height",
                            PlatformSettingKind.Text,
                            "720",
                            true,
                            []),
                        new PlatformSettingDefinition(
                            "vsync-enabled",
                            "VSync Enabled",
                            PlatformSettingKind.Boolean,
                            "true",
                            true,
                            []),
                        new PlatformSettingDefinition(
                            "fullscreen-enabled",
                            "Fullscreen Enabled",
                            PlatformSettingKind.Boolean,
                            "false",
                            true,
                            [])
                    ])
            ],
            [
                new PlatformAssetRequirementDefinition(
                    "scene",
                    "Scene",
                    true,
                    ["helen"]),
                new PlatformAssetRequirementDefinition(
                    "texture",
                    "Texture",
                    true,
                    ["png", "tga", "jpg"])
            ],
            [
                new PlatformComponentCompatibilityDefinition(
                    "helengine.meshcomponent",
                    PlatformComponentCompatibilityKind.Transform,
                    "Mesh components are normalized during packaging.",
                    string.Empty),
                new PlatformComponentCompatibilityDefinition(
                    "helengine.cameracomponent",
                    PlatformComponentCompatibilityKind.Transform,
                    "Camera components are normalized during packaging.",
                    string.Empty),
                new PlatformComponentCompatibilityDefinition(
                    "helengine.fpscomponent",
                    PlatformComponentCompatibilityKind.Transform,
                    "Font references are rewritten during packaging.",
                    string.Empty),
                new PlatformComponentCompatibilityDefinition(
                    "helengine.textcomponent",
                    PlatformComponentCompatibilityKind.Transform,
                    "Font references are rewritten during packaging.",
                    string.Empty),
                new PlatformComponentCompatibilityDefinition(
                    "helengine.directionallightcomponent",
                    PlatformComponentCompatibilityKind.PassThrough,
                    "Directional light payloads are emitted unchanged for runtime light extraction.",
                    string.Empty),
                new PlatformComponentCompatibilityDefinition(
                    "helengine.pointlightcomponent",
                    PlatformComponentCompatibilityKind.PassThrough,
                    "Point light payloads are emitted unchanged for runtime light extraction.",
                    string.Empty),
                new PlatformComponentCompatibilityDefinition(
                    "helengine.spotlightcomponent",
                    PlatformComponentCompatibilityKind.PassThrough,
                    "Spot light payloads are emitted unchanged for runtime light extraction.",
                    string.Empty)
            ],
            [
                new PlatformCodegenProfileDefinition(
                    "default",
                    "Default",
                    "Windows C# to C++ codegen profile",
                    PlatformCodegenLanguage.Cpp,
                    PlatformSerializationEndianness.LittleEndian,
                    [
                        new PlatformSettingDefinition(
                            "write-conversion-report",
                            "Write Conversion Report",
                            PlatformSettingKind.Boolean,
                            "true",
                            true,
                            []),
                        new PlatformSettingDefinition(
                            "include-project-defined-preprocessor-symbols",
                            "Include Project Symbols",
                            PlatformSettingKind.Boolean,
                            "false",
                            true,
                            []),
                        new PlatformSettingDefinition(
                            "load-native-runtime-metadata",
                            "Load Native Runtime Metadata",
                            PlatformSettingKind.Boolean,
                            "true",
                            true,
                            [])
                    ])
            ],
            [
                new PlatformStorageProfileDefinition(
                    "loose-files",
                    "Loose Files",
                    PlatformStorageProfileKind.LooseFiles,
                    "windows-loose-files",
                    allowContainerSegmentation: false)
            ],
            [
                new PlatformMediaProfileDefinition(
                    "windows-install-tree",
                    "Windows Install Tree",
                    PlatformMediaLayoutKind.InstallTree,
                    allowPhysicalDuplication: false,
                    preferLocalityOverDeduplication: false)
            ]);
    }
}

