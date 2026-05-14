# Windows Font Atlas Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Windows diagnostics log identify repeated generated texture uploads as specific font atlas builds, then verify the richer log through the real City Windows editor build pipeline.

**Architecture:** Extend the existing Windows diagnostics layer in the host and render bridge instead of changing generated-core behavior. Pull the latest font-resolution trace fields from generated-core when available, attach them to `asset.build` and `scene.checkpoint` log lines, then rebuild City through the editor pipeline and manually inspect the log output.

**Tech Stack:** C++, DirectX11 Windows host bridge, generated HelEngine core trace fields, PowerShell, `dotnet` editor build pipeline

---

### Task 1: Enrich texture build diagnostics with font-atlas provenance

**Files:**
- Modify: `src/platform/windows/win32/win32_render_bridge.cpp`
- Test: `C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log`

- [ ] **Step 1: Confirm the texture-upload logging seam in the Windows render bridge**

Run:

```powershell
Select-String -Path 'src\platform\windows\win32\win32_render_bridge.cpp' -Pattern 'RecordAssetBuild|BuildTextureFromRaw|BuildTextureDiagnosticsDetail' -Context 2,2
```

Expected: the output shows the existing `RuntimeRenderDiagnostics::RecordAssetBuild(...)` call inside `BuildTextureFromRaw(...)` and the helper that formats texture diagnostics details.

- [ ] **Step 2: Add generated font-atlas labeling to the texture diagnostics helper**

Code to preserve in `src/platform/windows/win32/win32_render_bridge.cpp`:

```cpp
std::string BuildTextureDiagnosticsDetail(TextureAsset* data, const std::string& textureId) {
    if (data == nullptr) {
        return "source=unknown";
    }

    std::ostringstream builder;
    builder << "width=" << data->Width
        << " height=" << data->Height
        << " runtime_asset_id=" << data->get_RuntimeAssetId();

    if (!textureId.empty() && textureId.rfind("__generated_runtime_texture_", 0) != 0) {
        builder << " source=authored";
        return builder.str();
    }

    builder << " source=generated";
#if __has_include("FontAssetBinarySerializer.hpp")
    const std::string fontDeserializeStage = FontAssetBinarySerializer::get_LastDeserializeStage();
    if (fontDeserializeStage == "BuildRuntimeTexture") {
        builder << " generated_kind=font_atlas";
        builder << " font_deserialize_stage=" << fontDeserializeStage;
#if __has_include("RuntimeSceneAssetReferenceResolver.hpp")
        if (Core::get_Instance() != nullptr && Core::get_Instance()->get_SceneAssetReferenceResolver() != nullptr) {
            RuntimeSceneAssetReferenceResolver* referenceResolver = Core::get_Instance()->get_SceneAssetReferenceResolver();
            builder << " text_font_relative_path=" << referenceResolver->get_LastTextFontRelativePath();
            builder << " text_font_load_stage=" << referenceResolver->get_LastTextLoadStage();
        }
#endif
        return builder.str();
    }

    if (!fontDeserializeStage.empty()) {
        builder << " font_deserialize_stage=" << fontDeserializeStage;
    }
#endif
    builder << " generated_kind=unlabeled";
    return builder.str();
}
```

- [ ] **Step 3: Ensure the texture build event uses the enriched helper output**

Code to preserve in `src/platform/windows/win32/win32_render_bridge.cpp`:

```cpp
RuntimeRenderDiagnostics::RecordAssetBuild(
    "texture",
    textureId,
    BuildTextureDiagnosticsDetail(data, textureId),
    TextureResources.size());
```

- [ ] **Step 4: Commit the render-bridge diagnostics update**

Run:

```powershell
git add src/platform/windows/win32/win32_render_bridge.cpp
git commit -m "feat: label windows font atlas texture builds"
```

Expected: one commit records the render-bridge provenance update.

### Task 2: Extend scene checkpoints with font-resolution trace context

**Files:**
- Modify: `src/platform/windows/runtime/runtime_render_diagnostics.hpp`
- Modify: `src/platform/windows/runtime/runtime_render_diagnostics.cpp`
- Modify: `src/platform/windows/win32/win32_application.cpp`
- Test: `C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log`

- [ ] **Step 1: Confirm the current scene checkpoint signature and call site**

Run:

```powershell
Select-String -Path 'src\platform\windows\runtime\runtime_render_diagnostics.hpp','src\platform\windows\runtime\runtime_render_diagnostics.cpp','src\platform\windows\win32\win32_application.cpp' -Pattern 'WriteSceneCheckpoint' -Context 1,3
```

Expected: the output shows the declaration, implementation, and the single host-side call path used to write scene checkpoints.

- [ ] **Step 2: Extend the checkpoint interface with the latest font trace fields**

Code to preserve in `src/platform/windows/runtime/runtime_render_diagnostics.hpp`:

```cpp
static void WriteSceneCheckpoint(
    const std::string& label,
    const RuntimeMemorySnapshot& memorySnapshot,
    const RuntimeRenderCounters& renderCounters,
    const std::string& coreStage,
    const std::string& sceneManagerStage,
    const std::string& sceneManagerSceneId,
    int loadedSceneCount,
    int pendingOperationCount,
    const std::string& sceneLoadStage,
    int rootEntityIndex,
    int entityDepth,
    const std::string& componentTypeId,
    const std::string& textFontRelativePath,
    const std::string& textFontLoadStage,
    const std::string& fontDeserializeStage);
```

- [ ] **Step 3: Append the new fields to the diagnostics log line**

Code to preserve in `src/platform/windows/runtime/runtime_render_diagnostics.cpp`:

```cpp
lineBuilder << " text_font_relative_path=\"" << textFontRelativePath << "\"";
lineBuilder << " text_font_load_stage=\"" << textFontLoadStage << "\"";
lineBuilder << " font_deserialize_stage=\"" << fontDeserializeStage << "\"";
```

- [ ] **Step 4: Read the generated-core font trace state in the Windows host checkpoint writer**

Code to preserve in `src/platform/windows/win32/win32_application.cpp`:

```cpp
std::string textFontRelativePath;
std::string textFontLoadStage;
std::string fontDeserializeStage;

#if __has_include("RuntimeSceneAssetReferenceResolver.hpp")
RuntimeSceneAssetReferenceResolver* referenceResolver = EngineCore->get_SceneAssetReferenceResolver();
if (referenceResolver != nullptr) {
    textFontRelativePath = referenceResolver->get_LastTextFontRelativePath();
    textFontLoadStage = referenceResolver->get_LastTextLoadStage();
}
#endif
#if __has_include("FontAssetBinarySerializer.hpp")
fontDeserializeStage = FontAssetBinarySerializer::get_LastDeserializeStage();
#endif
```

- [ ] **Step 5: Pass the new fields through the host checkpoint call**

Code to preserve in `src/platform/windows/win32/win32_application.cpp`:

```cpp
RuntimeRenderDiagnostics::WriteSceneCheckpoint(
    label != nullptr ? std::string(label) : std::string(),
    memorySnapshot,
    renderCounters,
    coreStage,
    sceneManagerStage,
    sceneManagerSceneId,
    loadedSceneCount,
    pendingOperationCount,
    sceneLoadStage,
    rootEntityIndex,
    entityDepth,
    componentTypeId,
    textFontRelativePath,
    textFontLoadStage,
    fontDeserializeStage);
```

- [ ] **Step 6: Commit the scene-checkpoint trace update**

Run:

```powershell
git add src/platform/windows/runtime/runtime_render_diagnostics.hpp src/platform/windows/runtime/runtime_render_diagnostics.cpp src/platform/windows/win32/win32_application.cpp
git commit -m "feat: add windows font trace scene diagnostics"
```

Expected: one commit records the host-side checkpoint enrichment.

### Task 3: Rebuild City through the editor pipeline and verify the richer log

**Files:**
- Modify: `C:\dev\helprojs\city\user_settings\build_config.json` during the build only, then restore it
- Output: `C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.exe`
- Output: `C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log`

- [ ] **Step 1: Rebuild City through the real editor Windows build pipeline with the narrowed scene list**

Run:

```powershell
$buildConfigPath = 'C:\dev\helprojs\city\user_settings\build_config.json'
$backupPath = 'C:\tmp\city-build-config.windows-scene-memory-diagnostics.backup.json'
Copy-Item -LiteralPath $buildConfigPath -Destination $backupPath -Force
try {
    $json = Get-Content -LiteralPath $buildConfigPath -Raw | ConvertFrom-Json
    $windowsConfig = $json.platforms | Where-Object { $_.platformId -eq 'windows' } | Select-Object -First 1
    if ($null -eq $windowsConfig) {
        throw 'Windows build configuration was not found in City build_config.json.'
    }

    $windowsConfig.selectedSceneIds = @('DemoDiscMainMenu', 'cube_test')
    $windowsConfig.sceneOrders = @(
        [pscustomobject]@{ sceneId = 'DemoDiscMainMenu'; orderNumber = 1 },
        [pscustomobject]@{ sceneId = 'cube_test'; orderNumber = 2 }
    )
    $windowsConfig.outputDirectoryPath = 'C:\tmp\city-windows-scene-memory-diagnostics'

    $json | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $buildConfigPath -Encoding utf8

    dotnet 'C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\bin\Debug\net9.0-windows\helengine.editor.app.dll' --project 'C:\dev\helprojs\city\project.heproj' --build windows --output 'C:\tmp\city-windows-scene-memory-diagnostics'
}
finally {
    Copy-Item -LiteralPath $backupPath -Destination $buildConfigPath -Force
}
```

Expected: the editor pipeline produces a fresh Windows player in `C:\tmp\city-windows-scene-memory-diagnostics` and City’s original build config is restored.

- [ ] **Step 2: Run the built player manually through `menu -> cube_test -> menu`**

Manual check:

```text
Launch C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.exe
Browse DemoDiscMainMenu -> cube_test -> DemoDiscMainMenu
Close the player
```

Expected: the run produces a fresh `helengine_windows.diagnostics.log`.

- [ ] **Step 3: Verify the new labels appear in the diagnostics log**

Run:

```powershell
Select-String -Path 'C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log' -Pattern 'generated_kind=font_atlas|text_font_relative_path=|font_deserialize_stage='
```

Expected: the log contains `generated_kind=font_atlas` on repeated generated texture builds and shows the matching font-relative-path fields.

- [ ] **Step 4: Commit the validated diagnostics pass**

Run:

```powershell
git add src/platform/windows/runtime/runtime_render_diagnostics.hpp src/platform/windows/runtime/runtime_render_diagnostics.cpp src/platform/windows/win32/win32_application.cpp src/platform/windows/win32/win32_render_bridge.cpp
git commit -m "feat: improve windows font atlas diagnostics"
```

Expected: one commit records the validated diagnostics improvements after the editor-pipeline rebuild.
