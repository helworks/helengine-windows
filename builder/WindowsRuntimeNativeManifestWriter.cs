#nullable enable
using System.Globalization;
using System.Text;
using helengine.baseplatform.Manifest;

namespace helengine.windows.builder;

/// <summary>
/// Writes generated C++ source fragments that embed runtime startup-scene, scene-catalog, and code-module residency data for the Windows player.
/// </summary>
public sealed class WindowsRuntimeNativeManifestWriter {
    /// <summary>
    /// Writes the generated runtime manifest source files into the generated-core runtime folder.
    /// </summary>
    /// <param name="generatedCoreRootPath">Absolute path to the generated core output root.</param>
    /// <param name="manifest">Final build manifest whose runtime data should be embedded into native source.</param>
    /// <param name="selectedGraphicsOptionValues">Resolved graphics-profile options selected for the build.</param>
    public void Write(
        string generatedCoreRootPath,
        PlatformBuildManifest manifest,
        IReadOnlyDictionary<string, string> selectedGraphicsOptionValues) {
        if (string.IsNullOrWhiteSpace(generatedCoreRootPath)) {
            throw new ArgumentException("Generated core root path must be provided.", nameof(generatedCoreRootPath));
        }
        if (manifest == null) {
            throw new ArgumentNullException(nameof(manifest));
        }
        if (selectedGraphicsOptionValues == null) {
            throw new ArgumentNullException(nameof(selectedGraphicsOptionValues));
        }

        string runtimeRootPath = Path.Combine(generatedCoreRootPath, "runtime");
        Directory.CreateDirectory(runtimeRootPath);

        WriteStartupManifestSource(runtimeRootPath, manifest);
        WriteSceneCatalogManifestSource(runtimeRootPath, manifest);
        WriteCodeModuleManifestSource(runtimeRootPath, manifest);
        WritePlayerSettingsManifestSource(runtimeRootPath, selectedGraphicsOptionValues);
    }

    /// <summary>
    /// Writes the generated startup-scene manifest header and implementation.
    /// </summary>
    /// <param name="runtimeRootPath">Runtime source folder inside the generated core tree.</param>
    /// <param name="manifest">Final build manifest whose startup scene should be embedded.</param>
    void WriteStartupManifestSource(string runtimeRootPath, PlatformBuildManifest manifest) {
        string startupSceneRelativePath = ResolveStartupSceneRelativePath(manifest);
        string headerPath = Path.Combine(runtimeRootPath, "runtime_startup_manifest.hpp");
        string sourcePath = Path.Combine(runtimeRootPath, "runtime_startup_manifest.cpp");

        File.WriteAllText(headerPath, BuildStartupManifestHeaderContents());
        File.WriteAllText(sourcePath, BuildStartupManifestSourceContents(manifest, startupSceneRelativePath));
    }

    /// <summary>
    /// Writes the generated runtime scene-catalog manifest header and implementation.
    /// </summary>
    /// <param name="runtimeRootPath">Runtime source folder inside the generated core tree.</param>
    /// <param name="manifest">Final build manifest whose built scene layout should be embedded.</param>
    void WriteSceneCatalogManifestSource(string runtimeRootPath, PlatformBuildManifest manifest) {
        string headerPath = Path.Combine(runtimeRootPath, "runtime_scene_catalog_manifest.hpp");
        string sourcePath = Path.Combine(runtimeRootPath, "runtime_scene_catalog_manifest.cpp");

        File.WriteAllText(headerPath, BuildSceneCatalogManifestHeaderContents());
        File.WriteAllText(sourcePath, BuildSceneCatalogManifestSourceContents(manifest));
    }

    /// <summary>
    /// Writes the generated code-module residency manifest header and implementation.
    /// </summary>
    /// <param name="runtimeRootPath">Runtime source folder inside the generated core tree.</param>
    /// <param name="manifest">Final build manifest whose code modules should be embedded.</param>
    void WriteCodeModuleManifestSource(string runtimeRootPath, PlatformBuildManifest manifest) {
        string headerPath = Path.Combine(runtimeRootPath, "runtime_code_module_manifest.hpp");
        string sourcePath = Path.Combine(runtimeRootPath, "runtime_code_module_manifest.cpp");

        File.WriteAllText(headerPath, BuildCodeModuleManifestHeaderContents());
        File.WriteAllText(sourcePath, BuildCodeModuleManifestSourceContents(manifest.CodeModules));
    }

    /// <summary>
    /// Writes the generated player-settings manifest header and implementation.
    /// </summary>
    /// <param name="runtimeRootPath">Runtime source folder inside the generated core tree.</param>
    /// <param name="selectedGraphicsOptionValues">Resolved graphics-profile options selected for the build.</param>
    void WritePlayerSettingsManifestSource(string runtimeRootPath, IReadOnlyDictionary<string, string> selectedGraphicsOptionValues) {
        int defaultWindowWidth = ResolveRequiredPositiveIntegerOption(selectedGraphicsOptionValues, "default-width");
        int defaultWindowHeight = ResolveRequiredPositiveIntegerOption(selectedGraphicsOptionValues, "default-height");
        string headerPath = Path.Combine(runtimeRootPath, "runtime_player_settings_manifest.hpp");
        string sourcePath = Path.Combine(runtimeRootPath, "player_settings.cpp");

        File.WriteAllText(headerPath, BuildPlayerSettingsManifestHeaderContents());
        File.WriteAllText(sourcePath, BuildPlayerSettingsManifestSourceContents(defaultWindowWidth, defaultWindowHeight));
    }

    /// <summary>
    /// Builds the generated runtime startup manifest header.
    /// </summary>
    /// <returns>Generated C++ header text.</returns>
    static string BuildStartupManifestHeaderContents() {
        StringBuilder builder = new();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine("const char* he_get_runtime_startup_scene_relative_path();");
        builder.AppendLine("const char* he_get_runtime_platform_name();");
        builder.AppendLine("const char* he_get_runtime_platform_version();");
        return builder.ToString();
    }

    /// <summary>
    /// Builds the generated runtime scene-catalog manifest header.
    /// </summary>
    /// <returns>Generated C++ header text.</returns>
    static string BuildSceneCatalogManifestHeaderContents() {
        StringBuilder builder = new();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine("#include <cstddef>");
        builder.AppendLine();
        builder.AppendLine("struct HERuntimeSceneCatalogEntry {");
        builder.AppendLine("    const char* SceneId;");
        builder.AppendLine("    const char* CookedRelativePath;");
        builder.AppendLine("};");
        builder.AppendLine();
        builder.AppendLine("const HERuntimeSceneCatalogEntry* he_runtime_scene_catalog_entries(std::size_t* count);");
        builder.AppendLine("const char* he_runtime_scene_cooked_relative_path(const char* sceneId);");
        return builder.ToString();
    }

    /// <summary>
    /// Builds the generated runtime startup manifest implementation.
    /// </summary>
    /// <param name="manifest">Build manifest whose platform metadata should be embedded into native source.</param>
    /// <param name="startupSceneRelativePath">Cooked runtime scene path embedded into the native player.</param>
    /// <returns>Generated C++ implementation text.</returns>
    static string BuildStartupManifestSourceContents(PlatformBuildManifest manifest, string startupSceneRelativePath) {
        if (manifest == null) {
            throw new ArgumentNullException(nameof(manifest));
        }

        StringBuilder builder = new();
        builder.AppendLine("#include \"runtime/runtime_startup_manifest.hpp\"");
        builder.AppendLine();
        builder.AppendLine("static const char kRuntimeStartupSceneRelativePath[] = \"" + EscapeCppStringLiteral(startupSceneRelativePath) + "\";");
        builder.AppendLine("static const char kRuntimePlatformName[] = \"" + EscapeCppStringLiteral(manifest.PlatformName) + "\";");
        builder.AppendLine("static const char kRuntimePlatformVersion[] = \"" + EscapeCppStringLiteral(manifest.PlatformVersion) + "\";");
        builder.AppendLine();
        builder.AppendLine("const char* he_get_runtime_startup_scene_relative_path() {");
        builder.AppendLine("    return kRuntimeStartupSceneRelativePath;");
        builder.AppendLine("}");
        builder.AppendLine();
        builder.AppendLine("const char* he_get_runtime_platform_name() {");
        builder.AppendLine("    return kRuntimePlatformName;");
        builder.AppendLine("}");
        builder.AppendLine();
        builder.AppendLine("const char* he_get_runtime_platform_version() {");
        builder.AppendLine("    return kRuntimePlatformVersion;");
        builder.AppendLine("}");
        return builder.ToString();
    }

    /// <summary>
    /// Builds the generated runtime scene-catalog manifest implementation.
    /// </summary>
    /// <param name="manifest">Build manifest whose scene layout should be embedded into native source.</param>
    /// <returns>Generated C++ implementation text.</returns>
    static string BuildSceneCatalogManifestSourceContents(PlatformBuildManifest manifest) {
        if (manifest == null) {
            throw new ArgumentNullException(nameof(manifest));
        }
        if (manifest.Scenes == null || manifest.Scenes.Length == 0) {
            throw new InvalidOperationException("Build manifest did not define any scenes.");
        }

        StringBuilder builder = new();
        builder.AppendLine("#include \"runtime/runtime_scene_catalog_manifest.hpp\"");
        builder.AppendLine();
        builder.AppendLine("#include <cstring>");
        builder.AppendLine("#include <stdexcept>");
        builder.AppendLine();
        builder.AppendLine("static const HERuntimeSceneCatalogEntry kRuntimeSceneCatalogEntries[] = {");
        for (int index = 0; index < manifest.Scenes.Length; index++) {
            PlatformBuildScene scene = manifest.Scenes[index];
            string cookedRelativePath = ResolveCookedRelativePath(scene);
            builder.Append("    { \"");
            builder.Append(EscapeCppStringLiteral(scene.SceneId));
            builder.Append("\", \"");
            builder.Append(EscapeCppStringLiteral(cookedRelativePath));
            builder.AppendLine("\" },");
        }

        builder.AppendLine("};");
        builder.AppendLine("static const std::size_t kRuntimeSceneCatalogEntryCount = sizeof(kRuntimeSceneCatalogEntries) / sizeof(kRuntimeSceneCatalogEntries[0]);");
        builder.AppendLine();
        builder.AppendLine("const HERuntimeSceneCatalogEntry* he_runtime_scene_catalog_entries(std::size_t* count) {");
        builder.AppendLine("    if (count != nullptr) {");
        builder.AppendLine("        *count = kRuntimeSceneCatalogEntryCount;");
        builder.AppendLine("    }");
        builder.AppendLine();
        builder.AppendLine("    return kRuntimeSceneCatalogEntries;");
        builder.AppendLine("}");
        builder.AppendLine();
        builder.AppendLine("const char* he_runtime_scene_cooked_relative_path(const char* sceneId) {");
        builder.AppendLine("    if (sceneId == nullptr || sceneId[0] == '\\0') {");
        builder.AppendLine("        throw std::invalid_argument(\"Runtime scene id is required.\");");
        builder.AppendLine("    }");
        builder.AppendLine();
        builder.AppendLine("    for (std::size_t index = 0; index < kRuntimeSceneCatalogEntryCount; index++) {");
        builder.AppendLine("        const HERuntimeSceneCatalogEntry& entry = kRuntimeSceneCatalogEntries[index];");
        builder.AppendLine("        if (std::strcmp(entry.SceneId, sceneId) == 0) {");
        builder.AppendLine("            return entry.CookedRelativePath;");
        builder.AppendLine("        }");
        builder.AppendLine("    }");
        builder.AppendLine();
        builder.AppendLine("    throw std::runtime_error(\"Runtime scene id was not found in the scene catalog manifest.\");");
        builder.AppendLine("}");
        return builder.ToString();
    }

    /// <summary>
    /// Builds the generated runtime code-module manifest header.
    /// </summary>
    /// <returns>Generated C++ header text.</returns>
    static string BuildCodeModuleManifestHeaderContents() {
        StringBuilder builder = new();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine("#include <cstddef>");
        builder.AppendLine();
        builder.AppendLine("enum class HERuntimeCodeModuleLoadState {");
        builder.AppendLine("    ResidentAtStartup = 0,");
        builder.AppendLine("    SceneResident = 1,");
        builder.AppendLine("    Unloadable = 2");
        builder.AppendLine("};");
        builder.AppendLine();
        builder.AppendLine("struct HERuntimeCodeModuleEntry {");
        builder.AppendLine("    const char* ModuleId;");
        builder.AppendLine("    const char* RuntimeSpecializationId;");
        builder.AppendLine("    HERuntimeCodeModuleLoadState LoadState;");
        builder.AppendLine("    const char* const* DependencyModuleIds;");
        builder.AppendLine("    std::size_t DependencyModuleCount;");
        builder.AppendLine("};");
        builder.AppendLine();
        builder.AppendLine("const HERuntimeCodeModuleEntry* he_runtime_code_module_entries(std::size_t* count);");
        builder.AppendLine("HERuntimeCodeModuleLoadState he_runtime_code_module_load_state(const char* moduleId);");
        builder.AppendLine("bool he_runtime_code_module_can_unload(const char* moduleId);");
        return builder.ToString();
    }

    /// <summary>
    /// Builds the generated runtime player-settings manifest header.
    /// </summary>
    /// <returns>Generated C++ header text.</returns>
    static string BuildPlayerSettingsManifestHeaderContents() {
        StringBuilder builder = new();
        builder.AppendLine("#pragma once");
        builder.AppendLine();
        builder.AppendLine("int he_get_runtime_default_window_width();");
        builder.AppendLine("int he_get_runtime_default_window_height();");
        return builder.ToString();
    }

    /// <summary>
    /// Builds the generated runtime code-module manifest implementation.
    /// </summary>
    /// <param name="codeModules">Cooked code-module records to embed into native source.</param>
    /// <returns>Generated C++ implementation text.</returns>
    static string BuildCodeModuleManifestSourceContents(PlatformBuildCodeModule[] codeModules) {
        StringBuilder builder = new();
        builder.AppendLine("#include \"runtime/runtime_code_module_manifest.hpp\"");
        builder.AppendLine();
        builder.AppendLine("#include <cstring>");
        builder.AppendLine("#include <stdexcept>");
        builder.AppendLine();

        if (codeModules != null) {
            for (int index = 0; index < codeModules.Length; index++) {
                PlatformBuildCodeModule codeModule = codeModules[index];
                if (codeModule.DependencyModuleIds.Length == 0) {
                    continue;
                }

                builder.Append("static const char* const kRuntimeCodeModuleDependencies_");
                builder.Append(index.ToString(System.Globalization.CultureInfo.InvariantCulture));
                builder.AppendLine("[] = {");
                for (int dependencyIndex = 0; dependencyIndex < codeModule.DependencyModuleIds.Length; dependencyIndex++) {
                    builder.Append("    \"");
                    builder.Append(EscapeCppStringLiteral(codeModule.DependencyModuleIds[dependencyIndex]));
                    builder.AppendLine("\",");
                }

                builder.AppendLine("};");
                builder.AppendLine();
            }
        }

        if (codeModules == null || codeModules.Length == 0) {
            builder.AppendLine("static const HERuntimeCodeModuleEntry* kRuntimeCodeModuleEntries = nullptr;");
            builder.AppendLine("static const std::size_t kRuntimeCodeModuleEntryCount = 0;");
        } else {
            builder.AppendLine("static const HERuntimeCodeModuleEntry kRuntimeCodeModuleEntries[] = {");
            for (int index = 0; index < codeModules.Length; index++) {
                PlatformBuildCodeModule codeModule = codeModules[index];
                string loadStateExpression = ResolveCodeModuleLoadStateExpression(codeModule.LoadScopes);
                builder.Append("    { \"");
                builder.Append(EscapeCppStringLiteral(codeModule.ModuleId));
                builder.Append("\", \"");
                builder.Append(EscapeCppStringLiteral(codeModule.RuntimeSpecializationId));
                builder.Append("\", ");
                builder.Append(loadStateExpression);
                builder.Append(", ");
                if (codeModule.DependencyModuleIds.Length == 0) {
                    builder.Append("nullptr, 0");
                } else {
                    builder.Append("kRuntimeCodeModuleDependencies_");
                    builder.Append(index.ToString(System.Globalization.CultureInfo.InvariantCulture));
                    builder.Append(", ");
                    builder.Append(codeModule.DependencyModuleIds.Length.ToString(System.Globalization.CultureInfo.InvariantCulture));
                }

                builder.AppendLine(" },");
            }

            builder.AppendLine("};");
            builder.Append("static const std::size_t kRuntimeCodeModuleEntryCount = sizeof(kRuntimeCodeModuleEntries) / sizeof(kRuntimeCodeModuleEntries[0]);");
            builder.AppendLine();
        }

        builder.AppendLine();
        builder.AppendLine("const HERuntimeCodeModuleEntry* he_runtime_code_module_entries(std::size_t* count) {");
        builder.AppendLine("    if (count != nullptr) {");
        builder.AppendLine("        *count = kRuntimeCodeModuleEntryCount;");
        builder.AppendLine("    }");
        builder.AppendLine();
        builder.AppendLine("    return kRuntimeCodeModuleEntries;");
        builder.AppendLine("}");
        builder.AppendLine();
        builder.AppendLine("HERuntimeCodeModuleLoadState he_runtime_code_module_load_state(const char* moduleId) {");
        builder.AppendLine("    if (moduleId == nullptr || moduleId[0] == '\\0') {");
        builder.AppendLine("        throw std::invalid_argument(\"Runtime code module id is required.\");");
        builder.AppendLine("    }");
        builder.AppendLine();
        builder.AppendLine("    for (std::size_t index = 0; index < kRuntimeCodeModuleEntryCount; index++) {");
        builder.AppendLine("        const HERuntimeCodeModuleEntry& entry = kRuntimeCodeModuleEntries[index];");
        builder.AppendLine("        if (std::strcmp(entry.ModuleId, moduleId) == 0) {");
        builder.AppendLine("            return entry.LoadState;");
        builder.AppendLine("        }");
        builder.AppendLine("    }");
        builder.AppendLine();
        builder.AppendLine("    throw std::runtime_error(\"Runtime code module was not found in the residency manifest.\");");
        builder.AppendLine("}");
        builder.AppendLine();
        builder.AppendLine("bool he_runtime_code_module_can_unload(const char* moduleId) {");
        builder.AppendLine("    return he_runtime_code_module_load_state(moduleId) != HERuntimeCodeModuleLoadState::ResidentAtStartup;");
        builder.AppendLine("}");
        return builder.ToString();
    }

    /// <summary>
    /// Builds the generated runtime player-settings manifest implementation.
    /// </summary>
    /// <param name="defaultWindowWidth">Default player-window width resolved from the selected graphics settings.</param>
    /// <param name="defaultWindowHeight">Default player-window height resolved from the selected graphics settings.</param>
    /// <returns>Generated C++ implementation text.</returns>
    static string BuildPlayerSettingsManifestSourceContents(int defaultWindowWidth, int defaultWindowHeight) {
        StringBuilder builder = new();
        builder.AppendLine("#include \"runtime/runtime_player_settings_manifest.hpp\"");
        builder.AppendLine();
        builder.AppendLine("static const int kRuntimeDefaultWindowWidth = " + defaultWindowWidth.ToString(CultureInfo.InvariantCulture) + ";");
        builder.AppendLine("static const int kRuntimeDefaultWindowHeight = " + defaultWindowHeight.ToString(CultureInfo.InvariantCulture) + ";");
        builder.AppendLine();
        builder.AppendLine("int he_get_runtime_default_window_width() {");
        builder.AppendLine("    return kRuntimeDefaultWindowWidth;");
        builder.AppendLine("}");
        builder.AppendLine();
        builder.AppendLine("int he_get_runtime_default_window_height() {");
        builder.AppendLine("    return kRuntimeDefaultWindowHeight;");
        builder.AppendLine("}");
        return builder.ToString();
    }

    /// <summary>
    /// Resolves the cooked runtime scene path embedded into the generated startup source.
    /// </summary>
    /// <param name="manifest">Final build manifest that carries the startup scene metadata.</param>
    /// <returns>Runtime-relative cooked scene path.</returns>
    static string ResolveStartupSceneRelativePath(PlatformBuildManifest manifest) {
        if (manifest.Scenes == null || manifest.Scenes.Length == 0) {
            throw new InvalidOperationException("Build manifest did not define any scenes.");
        }

        string startupSceneId = manifest.StartupSceneId;
        if (string.IsNullOrWhiteSpace(startupSceneId)) {
            startupSceneId = manifest.Scenes[0].SceneId;
        }

        for (int index = 0; index < manifest.Scenes.Length; index++) {
            PlatformBuildScene scene = manifest.Scenes[index];
            if (!string.Equals(scene.SceneId, startupSceneId, StringComparison.OrdinalIgnoreCase)) {
                continue;
            }

            if (scene.ResolvedMetadata != null) {
                for (int metadataIndex = 0; metadataIndex < scene.ResolvedMetadata.Length; metadataIndex++) {
                    KeyValuePair<string, string> entry = scene.ResolvedMetadata[metadataIndex];
                    if (string.Equals(entry.Key, "cooked-relative-path", StringComparison.OrdinalIgnoreCase)
                        && !string.IsNullOrWhiteSpace(entry.Value)) {
                        return entry.Value.Replace('\\', '/');
                    }
                }
            }

            PlatformBuildPayloadReference payloadReference = scene.PayloadReferences.FirstOrDefault();
            if (payloadReference != null && !string.IsNullOrWhiteSpace(payloadReference.SourceIdentity)) {
                return payloadReference.SourceIdentity.Replace('\\', '/');
            }

            throw new InvalidOperationException($"Startup scene '{scene.SceneId}' did not resolve a cooked-relative-path metadata entry.");
        }

        throw new InvalidOperationException($"Startup scene '{startupSceneId}' was not found in the build manifest.");
    }

    /// <summary>
    /// Resolves the cooked runtime scene path for one manifest scene.
    /// </summary>
    /// <param name="scene">Manifest scene whose cooked-relative path should be resolved.</param>
    /// <returns>Runtime-relative cooked scene path.</returns>
    static string ResolveCookedRelativePath(PlatformBuildScene scene) {
        if (scene == null) {
            throw new ArgumentNullException(nameof(scene));
        }

        if (scene.ResolvedMetadata != null) {
            for (int index = 0; index < scene.ResolvedMetadata.Length; index++) {
                KeyValuePair<string, string> entry = scene.ResolvedMetadata[index];
                if (string.Equals(entry.Key, "cooked-relative-path", StringComparison.OrdinalIgnoreCase)
                    && !string.IsNullOrWhiteSpace(entry.Value)) {
                    return entry.Value.Replace('\\', '/');
                }
            }
        }

        PlatformBuildPayloadReference payloadReference = scene.PayloadReferences.FirstOrDefault();
        if (payloadReference != null && !string.IsNullOrWhiteSpace(payloadReference.SourceIdentity)) {
            return payloadReference.SourceIdentity.Replace('\\', '/');
        }

        throw new InvalidOperationException($"Scene '{scene.SceneId}' did not resolve a cooked-relative-path metadata entry.");
    }

    /// <summary>
    /// Resolves the generated runtime code-module load state for one module.
    /// </summary>
    /// <param name="loadScopes">Authored load scopes from the cooked code module.</param>
    /// <returns>C++ enumeration expression for the embedded runtime load state.</returns>
    static string ResolveCodeModuleLoadStateExpression(string[] loadScopes) {
        if (loadScopes == null || loadScopes.Length == 0) {
            return "HERuntimeCodeModuleLoadState::Unloadable";
        }

        for (int index = 0; index < loadScopes.Length; index++) {
            string loadScope = loadScopes[index];
            if (string.Equals(loadScope, "always-loaded", StringComparison.OrdinalIgnoreCase)) {
                return "HERuntimeCodeModuleLoadState::ResidentAtStartup";
            }
        }

        for (int index = 0; index < loadScopes.Length; index++) {
            string loadScope = loadScopes[index];
            if (string.Equals(loadScope, "scene-loaded", StringComparison.OrdinalIgnoreCase)) {
                return "HERuntimeCodeModuleLoadState::SceneResident";
            }
        }

        return "HERuntimeCodeModuleLoadState::Unloadable";
    }

    /// <summary>
    /// Resolves one required positive integer option from the selected graphics-profile values.
    /// </summary>
    /// <param name="selectedGraphicsOptionValues">Resolved graphics-profile option values selected for the build.</param>
    /// <param name="optionId">Stable option identifier to resolve.</param>
    /// <returns>Parsed positive integer value.</returns>
    static int ResolveRequiredPositiveIntegerOption(IReadOnlyDictionary<string, string> selectedGraphicsOptionValues, string optionId) {
        if (!TryGetOptionValue(selectedGraphicsOptionValues, optionId, out string value) || string.IsNullOrWhiteSpace(value)) {
            throw new InvalidOperationException($"Required Windows graphics option '{optionId}' was not provided to the native manifest writer.");
        }

        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int parsedValue) || parsedValue <= 0) {
            throw new InvalidOperationException($"Windows graphics option '{optionId}' must be a positive integer.");
        }

        return parsedValue;
    }

    /// <summary>
    /// Attempts to resolve one option value using a case-insensitive key comparison.
    /// </summary>
    /// <param name="selectedGraphicsOptionValues">Resolved graphics-profile option values selected for the build.</param>
    /// <param name="optionId">Stable option identifier to resolve.</param>
    /// <param name="value">Resolved option value when present.</param>
    /// <returns>True when the option value was present.</returns>
    static bool TryGetOptionValue(IReadOnlyDictionary<string, string> selectedGraphicsOptionValues, string optionId, out string value) {
        if (selectedGraphicsOptionValues.TryGetValue(optionId, out value)) {
            return true;
        }

        foreach (KeyValuePair<string, string> entry in selectedGraphicsOptionValues) {
            if (string.Equals(entry.Key, optionId, StringComparison.OrdinalIgnoreCase)) {
                value = entry.Value;
                return true;
            }
        }

        value = string.Empty;
        return false;
    }

    /// <summary>
    /// Escapes one string for safe embedding inside a C++ string literal.
    /// </summary>
    /// <param name="value">String value to escape.</param>
    /// <returns>Escaped literal contents without the surrounding quotes.</returns>
    static string EscapeCppStringLiteral(string value) {
        if (string.IsNullOrEmpty(value)) {
            return string.Empty;
        }

        StringBuilder builder = new();
        for (int index = 0; index < value.Length; index++) {
            char current = value[index];
            if (current == '\\') {
                builder.Append("\\\\");
            } else if (current == '"') {
                builder.Append("\\\"");
            } else if (current == '\n') {
                builder.Append("\\n");
            } else if (current == '\r') {
                builder.Append("\\r");
            } else if (current == '\t') {
                builder.Append("\\t");
            } else {
                builder.Append(current);
            }
        }

        return builder.ToString();
    }
}



