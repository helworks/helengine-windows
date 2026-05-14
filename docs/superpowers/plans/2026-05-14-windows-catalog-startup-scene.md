# Windows Catalog Startup Scene Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Windows player load its startup scene through the runtime scene catalog and `SceneManager` so the first scene participates in the same owned-asset tracking and release path as every later scene.

**Architecture:** Remove the host-only startup `SceneLoadService.Load(...)` path and replace it with catalog-first startup through `SceneManager.LoadScene(..., Single)`. Keep the existing runtime scene catalog build flow, fail fast when the catalog is empty, and reuse the current diagnostics log to prove startup-scene assets now release during the first scene transition.

**Tech Stack:** C++ Windows host, generated runtime core, scene catalog manifest, PowerShell, editor Windows build pipeline

---

### Task 1: Replace the untracked startup scene load path in the Windows host

**Files:**
- Modify: `src/platform/windows/win32/win32_application.cpp`
- Test: `C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log`

- [ ] **Step 1: Confirm the current startup-scene load path**

Run:

```powershell
Select-String -Path 'src\platform\windows\win32\win32_application.cpp' -Pattern 'LoadPackagedStartupScene|SceneLoadService|RuntimeSceneCatalog|LoadScene'
```

Expected: the host still loads the startup scene by path, deserializes it directly, and sends it to `SceneLoadService->Load(...)`.

- [ ] **Step 2: Remove the direct packaged startup-scene materialization path**

Code to delete or replace in `src/platform/windows/win32/win32_application.cpp`:

```cpp
SceneAsset* startupScene = static_cast<SceneAsset*>(LoadPackagedAsset(startupSceneRelativePath));
WriteLifecycleLog("Handing packaged startup scene to scene load service.");
EngineCore->get_SceneLoadService()->Load(startupScene);
WriteLifecycleLog("Packaged startup scene applied to scene load service.");
```

- [ ] **Step 3: Resolve the first runtime scene catalog entry and load it through `SceneManager`**

Code shape to add in `src/platform/windows/win32/win32_application.cpp`:

```cpp
RuntimeSceneCatalog* sceneCatalog = EngineCore->get_InitializationOptions()->get_SceneCatalog();
if (sceneCatalog == nullptr || sceneCatalog->get_Entries() == nullptr || sceneCatalog->get_Entries()->Count() == 0) {
    throw std::runtime_error("Windows startup requires at least one runtime scene catalog entry.");
}

RuntimeSceneCatalogEntry* startupEntry = (*sceneCatalog->get_Entries())[0];
if (startupEntry == nullptr || startupEntry->get_SceneId().empty()) {
    throw std::runtime_error("Windows startup requires the first runtime scene catalog entry to define a scene id.");
}

{
    std::ostringstream messageBuilder;
    messageBuilder << "Loading startup scene from runtime scene catalog entry '" << startupEntry->get_SceneId() << "'.";
    std::string message = messageBuilder.str();
    WriteLifecycleLog(message.c_str());
}

EngineCore->get_SceneManager()->LoadScene(startupEntry->get_SceneId(), SceneLoadMode::Single);
WriteLifecycleLog("Packaged startup scene applied through scene manager.");
```

- [ ] **Step 4: Make the empty-catalog case fail fast**

Expected behavior:

```text
If the runtime scene catalog is null or contains zero entries, startup throws immediately instead of continuing without a tracked main scene.
```

- [ ] **Step 5: Commit the tracked startup-scene host change**

Run:

```powershell
git add src/platform/windows/win32/win32_application.cpp
git commit -m "feat: load windows startup scene through scene manager"
```

Expected: one commit records the Windows startup-scene path change.

### Task 2: Rebuild City through the editor pipeline and verify startup-scene release behavior

**Files:**
- Modify: `C:\dev\helprojs\city\user_settings\build_config.json` during the build only, then restore it
- Output: `C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.exe`
- Output: `C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log`

- [ ] **Step 1: Rebuild City through the real editor Windows build pipeline with the narrowed scene list**

Run:

```powershell
$buildConfigPath = 'C:\dev\helprojs\city\user_settings\build_config.json'
$backupPath = 'C:\tmp\city-build-config.windows-catalog-startup-scene.backup.json'
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

- [ ] **Step 2: Run the player manually through `DemoDiscMainMenu -> cube_test -> DemoDiscMainMenu`**

Manual check:

```text
Launch C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.exe
Browse DemoDiscMainMenu -> cube_test -> DemoDiscMainMenu
Close the player
```

Expected: the run produces a fresh diagnostics log with startup-scene release events.

- [ ] **Step 3: Verify the first menu scene now releases through the tracked scene path**

Run:

```powershell
Select-String -Path 'C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log' -Pattern 'asset.release|scene_steady_state|texture_resources='
```

Expected:
- the transition away from the first menu now emits release events for startup-scene owned assets
- the second menu `texture_resources` is near the first menu steady-state baseline instead of the old leaking `48`

- [ ] **Step 4: Commit the verified startup-scene tracking pass**

Run:

```powershell
git add src/platform/windows/win32/win32_application.cpp
git commit -m "fix: track windows startup scene through catalog"
```

Expected: one commit records the verified tracked-startup change.

### Task 3: Final verification and cleanup

**Files:**
- Test: `C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log`

- [ ] **Step 1: Re-check the final diagnostics tail**

Run:

```powershell
Get-Content 'C:\tmp\city-windows-scene-memory-diagnostics\helengine_windows.diagnostics.log' -Tail 160
```

Expected: the log shows the first menu scene releasing through the same shared path as later scene loads and the one-scene steady state no longer ratchets upward due to startup-scene ownership bypass.

- [ ] **Step 2: Confirm the working tree state**

Run:

```powershell
git status --short
git -C C:\dev\helworks\helengine status --short
```

Expected: both repos are clean or contain only intentional in-progress changes.
