# Shared Runtime Asset Eviction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a shared runtime asset eviction contract for scene-owned textures, fonts, models, and materials, then make the Windows player honor it so one-scene steady-state memory stops ratcheting upward across scene round-trips.

**Architecture:** Keep scene-owned asset tracking and reference counting in shared core, but route every release through renderer-facing APIs instead of partial disposal behavior. Extend the shared renderer contracts for models and materials, wire `SceneManager` to call them when scene-owned references reach zero, then implement Windows-side cache eviction and lifecycle diagnostics on top of that contract.

**Tech Stack:** C#, generated runtime core, C++ Windows host bridge, DirectX11, PowerShell, `dotnet`, editor Windows build pipeline

---

### Task 1: Extend the shared renderer contracts for runtime asset release

**Files:**
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\RenderManager2D.cs`
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\RenderManager3D.cs`
- Test: `C:\dev\helworks\helengine\engine\helengine.editor.tests\serialization\scene\RuntimeSceneLoadServiceTests.cs`

- [ ] **Step 1: Confirm the current shared renderer release seams**

Run:

```powershell
Select-String -Path 'C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\RenderManager2D.cs','C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\RenderManager3D.cs' -Pattern 'ReleaseTexture|ReleaseFont|Dispose|BuildModelFromRaw|BuildMaterialFromRaw'
```

Expected: `RenderManager2D` already exposes `ReleaseTexture(...)` and `ReleaseFont(...)`, while `RenderManager3D` exposes model/material build methods but no matching explicit release methods yet.

- [ ] **Step 2: Add explicit 3D runtime asset release methods to the shared renderer contract**

Code to add in `C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\RenderManager3D.cs`:

```csharp
/// <summary>
/// Releases one runtime model previously created by this renderer.
/// </summary>
/// <param name="model">Runtime model that should release any renderer-owned resources.</param>
public virtual void ReleaseModel(RuntimeModel model) {
    if (model == null) {
        throw new ArgumentNullException(nameof(model));
    }
}

/// <summary>
/// Releases one runtime material previously created by this renderer.
/// </summary>
/// <param name="material">Runtime material that should release any renderer-owned resources.</param>
public virtual void ReleaseMaterial(RuntimeMaterial material) {
    if (material == null) {
        throw new ArgumentNullException(nameof(material));
    }
}

/// <summary>
/// Flushes any renderer-owned runtime asset releases that were deferred until the renderer reached a safe point.
/// </summary>
public virtual void FlushReleasedAssets() {
}
```

- [ ] **Step 3: Keep the 2D release contract as the canonical pattern**

Code to preserve in `C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\RenderManager2D.cs`:

```csharp
public virtual void ReleaseTexture(RuntimeTexture texture) {
}

public virtual void ReleaseFont(FontAsset font) {
    if (font == null) {
        throw new ArgumentNullException(nameof(font));
    }

    font.Dispose();
}

public virtual void FlushReleasedTextures() {
}
```

- [ ] **Step 4: Run the scene-runtime tests to catch contract compile breaks**

Run:

```powershell
dotnet test 'C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj' --filter "FullyQualifiedName~RuntimeSceneLoadServiceTests" -v minimal
```

Expected: tests fail only where the new `RenderManager3D` contract requires fake/test renderer updates.

- [ ] **Step 5: Commit the shared renderer contract extension**

Run:

```powershell
git -C C:\dev\helworks\helengine add engine\helengine.core\managers\rendering\RenderManager2D.cs engine\helengine.core\managers\rendering\RenderManager3D.cs
git -C C:\dev\helworks\helengine commit -m "feat: add shared runtime asset release contract"
```

Expected: one commit records the shared renderer API changes.

### Task 2: Extend shared scene-owned asset tracking to cover models and materials

**Files:**
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\RuntimeSceneAssetReferenceResolver.cs`
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\RuntimeSceneOwnedAssetSet.cs`
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\SceneManager.cs`
- Modify: `C:\dev\helworks\helengine\engine\helengine.editor.tests\serialization\scene\RuntimeSceneLoadServiceTests.cs`
- Possibly modify: `C:\dev\helworks\helengine\engine\helengine.editor.tests\testing\TestRenderManager3D.cs`

- [ ] **Step 1: Confirm the current owned-asset tracking shape**

Run:

```powershell
Select-String -Path 'C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\RuntimeSceneAssetReferenceResolver.cs','C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\RuntimeSceneOwnedAssetSet.cs','C:\dev\helworks\helengine\engine\helengine.core\SceneManager.cs' -Pattern 'ActiveOwnedTextures|ActiveOwnedFonts|RuntimeSceneOwnedAssetSet|ReleaseTexture|ReleaseFont'
```

Expected: the resolver tracks only textures/fonts today and `SceneManager` only releases those owned asset classes.

- [ ] **Step 2: Track scene-owned models and materials in the resolver**

Code shape to add in `C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\RuntimeSceneAssetReferenceResolver.cs`:

```csharp
List<RuntimeModel> ActiveOwnedModels;
List<RuntimeMaterial> ActiveOwnedMaterials;
```

and when resolving:

```csharp
RuntimeModel runtimeModel = Core.Instance.RenderManager3D.BuildModelFromRaw(modelAsset);
TrackOwnedModel(runtimeModel);
return runtimeModel;
```

```csharp
RuntimeMaterial runtimeMaterial = Core.Instance.RenderManager3D.BuildMaterialFromRaw(materialAsset, shaderAsset);
TrackOwnedMaterial(runtimeMaterial);
ApplyMaterialDiffuseTexture(runtimeMaterial, materialAsset, fullPath);
return runtimeMaterial;
```

- [ ] **Step 3: Extend `RuntimeSceneOwnedAssetSet` to carry all four asset classes**

Code shape to add in `C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\RuntimeSceneOwnedAssetSet.cs`:

```csharp
public IReadOnlyList<RuntimeTexture> OwnedTextures { get; }
public IReadOnlyList<FontAsset> OwnedFonts { get; }
public IReadOnlyList<RuntimeModel> OwnedModels { get; }
public IReadOnlyList<RuntimeMaterial> OwnedMaterials { get; }
```

Expected constructor shape:

```csharp
public RuntimeSceneOwnedAssetSet(
    IReadOnlyList<RuntimeTexture> ownedTextures,
    IReadOnlyList<FontAsset> ownedFonts,
    IReadOnlyList<RuntimeModel> ownedModels,
    IReadOnlyList<RuntimeMaterial> ownedMaterials) {
```

- [ ] **Step 4: Update `SceneManager` reference counting and unload release paths for all four asset classes**

Code shape to add in `C:\dev\helworks\helengine\engine\helengine.core\SceneManager.cs`:

```csharp
readonly Dictionary<RuntimeModel, int> ownedModelReferenceCounts;
readonly Dictionary<RuntimeMaterial, int> ownedMaterialReferenceCounts;
```

and release paths:

```csharp
Core.Instance.RenderManager3D.ReleaseModel(model);
Core.Instance.RenderManager3D.ReleaseMaterial(material);
Core.Instance.RenderManager3D.FlushReleasedAssets();
```

- [ ] **Step 5: Add a failing shared test that proves scene unload requests model/material release**

Test code to add in `C:\dev\helworks\helengine\engine\helengine.editor.tests\serialization\scene\RuntimeSceneLoadServiceTests.cs`:

```csharp
[Fact]
public void UnloadSceneImmediate_ReleasesSceneOwnedModelAndMaterialWhenReferenceCountReachesZero() {
    TestRenderManager3D renderManager3D = new TestRenderManager3D();
    TestRenderManager2D renderManager2D = new TestRenderManager2D();
    Core core = CreateRuntimeCore(renderManager3D, renderManager2D);

    RuntimeModel model = new RuntimeModel();
    RuntimeMaterial material = new RuntimeMaterial();
    RuntimeSceneOwnedAssetSet ownedAssets = new RuntimeSceneOwnedAssetSet(
        new List<RuntimeTexture>(),
        new List<FontAsset>(),
        new List<RuntimeModel> { model },
        new List<RuntimeMaterial> { material });

    Scene scene = new Scene();
    core.SceneManager.RegisterLoadedSceneForTests(scene, ownedAssets);

    core.SceneManager.UnloadSceneImmediate(scene);

    Assert.Contains(model, renderManager3D.ReleasedModels);
    Assert.Contains(material, renderManager3D.ReleasedMaterials);
}
```

- [ ] **Step 6: Run the targeted scene-runtime tests**

Run:

```powershell
dotnet test 'C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj' --filter "FullyQualifiedName~RuntimeSceneLoadServiceTests" -v minimal
```

Expected: tests pass once the resolver, owned-asset set, scene manager, and test doubles all match the new contract.

- [ ] **Step 7: Commit the shared scene-owned asset release plumbing**

Run:

```powershell
git -C C:\dev\helworks\helengine add engine\helengine.core\scene\runtime\RuntimeSceneAssetReferenceResolver.cs engine\helengine.core\scene\runtime\RuntimeSceneOwnedAssetSet.cs engine\helengine.core\SceneManager.cs engine\helengine.editor.tests\serialization\scene\RuntimeSceneLoadServiceTests.cs engine\helengine.editor.tests\testing\TestRenderManager3D.cs
git -C C:\dev\helworks\helengine commit -m "feat: release scene owned runtime models and materials"
```

Expected: one commit records the shared owned-asset release behavior.

### Task 3: Add optional release lifecycle diagnostics hooks

**Files:**
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\RenderManager2D.cs`
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\managers\rendering\RenderManager3D.cs`
- Modify: `src/platform/windows/runtime/runtime_render_diagnostics.hpp`
- Modify: `src/platform/windows/runtime/runtime_render_diagnostics.cpp`
- Modify: `src/platform/windows/win32/win32_render_bridge.cpp`

- [ ] **Step 1: Define minimal shared release lifecycle callbacks**

Code shape to add in the shared renderer base classes:

```csharp
public event Action<string, string> RuntimeAssetReleaseRequested;
public event Action<string, string> RuntimeAssetReleaseCompleted;
```

Use:
- first string: asset kind
- second string: runtime or backend asset identifier

- [ ] **Step 2: Add protected helpers so backends can emit release lifecycle notifications**

Code shape to add:

```csharp
protected void OnRuntimeAssetReleaseRequested(string assetKind, string assetId) {
    RuntimeAssetReleaseRequested?.Invoke(assetKind, assetId);
}

protected void OnRuntimeAssetReleaseCompleted(string assetKind, string assetId) {
    RuntimeAssetReleaseCompleted?.Invoke(assetKind, assetId);
}
```

- [ ] **Step 3: Add Windows diagnostics helpers for release events**

Code shape to add in `src/platform/windows/runtime/runtime_render_diagnostics.cpp`:

```cpp
void RuntimeRenderDiagnostics::RecordAssetReleaseRequested(
    const std::string& assetClass,
    const std::string& assetId,
    const std::string& detail);

void RuntimeRenderDiagnostics::RecordAssetReleaseCompleted(
    const std::string& assetClass,
    const std::string& assetId,
    const std::string& detail);
```

Expected log lines:

```text
asset.release phase="requested" class="texture" asset_id="..."
asset.release phase="completed" class="texture" asset_id="..."
```

- [ ] **Step 4: Run the shared scene-runtime tests again**

Run:

```powershell
dotnet test 'C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj' --filter "FullyQualifiedName~RuntimeSceneLoadServiceTests" -v minimal
```

Expected: lifecycle event additions do not break shared scene-runtime behavior.

- [ ] **Step 5: Commit the shared release diagnostics hooks**

Run:

```powershell
git -C C:\dev\helworks\helengine add engine\helengine.core\managers\rendering\RenderManager2D.cs engine\helengine.core\managers\rendering\RenderManager3D.cs
git add src/platform/windows/runtime/runtime_render_diagnostics.hpp src/platform/windows/runtime/runtime_render_diagnostics.cpp
git -C C:\dev\helworks\helengine commit -m "feat: add runtime asset release lifecycle hooks"
git commit -m "feat: add windows asset release diagnostics"
```

Expected: shared hooks and Windows log support are committed.

### Task 4: Implement the Windows runtime asset eviction paths

**Files:**
- Modify: `src/platform/windows/win32/win32_render_bridge.hpp`
- Modify: `src/platform/windows/win32/win32_render_bridge.cpp`
- Modify: `src/platform/windows/win32/win32_application.cpp`

- [ ] **Step 1: Confirm the current Windows cache owners**

Run:

```powershell
Select-String -Path 'src\platform\windows\win32\win32_render_bridge.hpp','src\platform\windows\win32\win32_render_bridge.cpp' -Pattern 'TextureResources|MaterialShaderResources|MaterialConstantBuffers|Win32RuntimeModel|BuildModelFromRaw|BuildMaterialFromRaw'
```

Expected: the output shows the texture cache plus the material shader and constant-buffer caches that must shrink on scene unload.

- [ ] **Step 2: Add explicit Windows release overrides for 2D and 3D runtime assets**

Code shape to add in `src/platform/windows/win32/win32_render_bridge.hpp`:

```cpp
void ReleaseTexture(RuntimeTexture* texture) override;
void ReleaseFont(FontAsset* font) override;
void ReleaseModel(RuntimeModel* model) override;
void ReleaseMaterial(RuntimeMaterial* material) override;
void FlushReleasedAssets() override;
```

- [ ] **Step 3: Evict released textures from `TextureResources`**

Code shape to add in `src/platform/windows/win32/win32_render_bridge.cpp`:

```cpp
void Win32RenderManager2D::ReleaseTexture(RuntimeTexture* texture) {
    if (texture == nullptr) {
        throw std::invalid_argument("Runtime texture must be provided for release.");
    }

    RuntimeRenderDiagnostics::RecordAssetReleaseRequested("texture", texture->get_Id(), "");
    TextureResources.erase(texture->get_Id());
    texture->Dispose();
    RuntimeRenderDiagnostics::RecordAssetReleaseCompleted("texture", texture->get_Id(), "");
}
```

- [ ] **Step 4: Release materials and models through Windows backend caches**

Code shape:

```cpp
void Win32RenderManager3D::ReleaseMaterial(RuntimeMaterial* material) {
    if (material == nullptr) {
        throw std::invalid_argument("Runtime material must be provided for release.");
    }

    const std::string materialId = material->get_Id();
    RuntimeRenderDiagnostics::RecordAssetReleaseRequested("material", materialId, "");
    MaterialShaderResources.erase(materialId);
    MaterialConstantBuffers.erase(materialId);
    material->Dispose();
    RuntimeRenderDiagnostics::RecordAssetReleaseCompleted("material", materialId, "");
}
```

and:

```cpp
void Win32RenderManager3D::ReleaseModel(RuntimeModel* model) {
    if (model == nullptr) {
        throw std::invalid_argument("Runtime model must be provided for release.");
    }

    RuntimeRenderDiagnostics::RecordAssetReleaseRequested("model", model->get_Id(), "");
    model->Dispose();
    RuntimeRenderDiagnostics::RecordAssetReleaseCompleted("model", model->get_Id(), "");
}
```

- [ ] **Step 5: Clear remaining backend caches in renderer disposal and flush methods**

Code shape:

```cpp
void Win32RenderManager3D::FlushReleasedAssets() {
}

void Win32RenderManager3D::Dispose() {
    MaterialShaderResources.clear();
    MaterialConstantBuffers.clear();
}
```

and:

```cpp
void Win32RenderManager2D::Dispose() {
    TextureResources.clear();
}
```

- [ ] **Step 6: Ensure the Windows host shuts down through core disposal before raw deletes**

Code shape to add in `src/platform/windows/win32/win32_application.cpp`:

```cpp
if (EngineCore != nullptr) {
    EngineCore->Dispose();
}
```

- [ ] **Step 7: Build City through the editor pipeline with the narrowed scene list**

Run:

```powershell
$buildConfigPath = 'C:\dev\helprojs\city\user_settings\build_config.json'
$backupPath = 'C:\tmp\city-build-config.shared-runtime-asset-eviction.backup.json'
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

Expected: the editor pipeline produces a fresh Windows player and restores City’s original build config.

- [ ] **Step 8: Run the player manually and verify release events plus lower steady-state cache counts**

Manual run:

```text
Launch C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.exe
Browse DemoDiscMainMenu -> cube_test -> DemoDiscMainMenu
Close the player
```

Then verify:

```powershell
Select-String -Path 'C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log' -Pattern 'asset.release|texture_resources=|material_shader_resources=|material_constant_buffers='
```

Expected:
- release-requested and release-completed entries appear
- second-menu `texture_resources` is near the first-menu baseline rather than `49`
- material counters return near the original one-scene steady-state footprint

- [ ] **Step 9: Commit the Windows eviction implementation**

Run:

```powershell
git add src/platform/windows/win32/win32_render_bridge.hpp src/platform/windows/win32/win32_render_bridge.cpp src/platform/windows/win32/win32_application.cpp src/platform/windows/runtime/runtime_render_diagnostics.hpp src/platform/windows/runtime/runtime_render_diagnostics.cpp
git commit -m "feat: release windows runtime scene assets"
```

Expected: one commit records the Windows backend eviction implementation and diagnostics verification support.

### Task 5: Run the final targeted verifications

**Files:**
- Test: `C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj`
- Output: `C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log`

- [ ] **Step 1: Run the targeted shared runtime tests**

Run:

```powershell
dotnet test 'C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj' --filter "FullyQualifiedName~RuntimeSceneLoadServiceTests" -v minimal
```

Expected: PASS.

- [ ] **Step 2: Re-check the latest City diagnostics log after the manual round-trip**

Run:

```powershell
Get-Content 'C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log' -Tail 120
```

Expected: the log shows release events and the final one-scene steady state is materially closer to the original menu baseline than the pre-fix monotonic growth pattern.

- [ ] **Step 3: Commit any last verification-driven adjustments**

Run:

```powershell
git -C C:\dev\helworks\helengine status --short
git status --short
```

Expected: both repos are clean or only contain intentional documentation/work-in-progress changes.
