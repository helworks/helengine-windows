# Windows Packaged Forward Standard Shader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Windows player use the cooked `ForwardStandardShader` for every standard material, reject any silent fallback for that shader, and add diagnostics that isolate the remaining CubeTest darkness.

**Architecture:** The editor already cooks `ForwardStandardShader.dx11.hasset`; `Win32RenderManager3D::BuildShaderResource` creates Direct3D shaders from its bytecode, and `ApplyMaterial` binds them. The native `VertexShaderSource` and `PixelShaderSource` are a separate fallback for drawables with no cached material shader. Standard materials must be required to have a packaged shader resource, while the fallback remains limited to material-less legacy drawables. CubeTest diagnosis will use the existing first-frame render diagnostics to prove the selected shader, bound PBR buffers, and directional-light state.

**Tech Stack:** C# editor packager and xUnit; C++20 Windows Direct3D 11 player; HLSL packaged by the editor.

---

## Files and responsibilities

- `builder.tests/WindowsPackagedForwardShaderSourceTests.cs` — source-contract tests for the Windows packaged-shader requirement.
- `src/platform/windows/win32/win32_render_bridge.cpp` — reject a missing shader resource for a shader-backed material; preserve the fallback only for no-material draws; emit precise first-frame diagnostics.
- `src/platform/windows/win32/win32_render_bridge.hpp` — remove obsolete fallback-only 3D shader state if the C++ implementation no longer needs it.
- `builder.tests/WindowsPlatformAssetBuilderTests.cs` — retain/extend the cooked `ForwardStandardShader` dependency assertion for Windows standard materials.
- `CMakeLists.txt` and the native build output — build and run the Windows player validation executable.

### Task 1: Lock the packaged standard-shader contract with failing tests

**Files:**
- Create: `builder.tests/WindowsPackagedForwardShaderSourceTests.cs`
- Test: `builder.tests/helengine.windows.builder.tests.csproj`

- [ ] **Step 1: Write a failing source-contract test for standard-material rendering.**

```csharp
[Fact]
public void ApplyMaterial_WhenShaderBackedMaterialHasNoCachedShaderResource_ThrowsInsteadOfUsingFallback() {
    string source = File.ReadAllText(Win32RenderBridgePath);

    Assert.Contains("if (rootMaterial != nullptr)", source, StringComparison.Ordinal);
    Assert.Contains("Standard materials require a packaged shader resource", source, StringComparison.Ordinal);
    Assert.Contains("} else {\n                    visitStage = \"bind_default_pipeline\";", source, StringComparison.Ordinal);
}
```

Add a second test that asserts `BuildShaderResource` calls `CreateVertexShader` and `CreatePixelShader` with `vertexBinary->Bytecode` and `pixelBinary->Bytecode`, and that `ApplyMaterial` logs `3d.material_path=packaged_shader`.

- [ ] **Step 2: Run the focused test and verify it fails because the missing-resource path still selects `bind_default_pipeline`.**

Run:

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter FullyQualifiedName~WindowsPackagedForwardShaderSourceTests
```

Expected: the new fallback-rejection assertion fails.

- [ ] **Step 3: Commit the red test only.**

```powershell
git add builder.tests/WindowsPackagedForwardShaderSourceTests.cs
git commit -m "test: require packaged shader for Windows standard materials"
```

### Task 2: Make shader-backed Windows materials fail fast instead of using the native fallback

**Files:**
- Modify: `src/platform/windows/win32/win32_render_bridge.cpp:1688-1755`
- Modify: `src/platform/windows/win32/win32_render_bridge.hpp:160-190` only if fallback-only 3D members become unused
- Test: `builder.tests/WindowsPackagedForwardShaderSourceTests.cs`

- [ ] **Step 1: Replace the shader-resource conditional in the 3D draw loop with two explicit paths.**

Use the packaged path whenever `rootMaterial` is present. Require `MaterialShaderResources` to contain that material id; otherwise throw `InvalidOperationException` with the text `Standard materials require a packaged shader resource.` The no-material path alone may bind the native fallback pipeline.

```cpp
if (rootMaterial != nullptr) {
    if (shaderResource == nullptr) {
        throw new InvalidOperationException("Standard materials require a packaged shader resource.");
    }

    visitStage = "apply_material";
    ApplyMaterial(runtimeMaterial != nullptr ? runtimeMaterial : rootMaterial);
    // Bind ForwardLightBuffer, ShadowBuffer, shadow resources, and shadow samplers here.
} else {
    visitStage = "bind_default_pipeline";
    // Keep this path only for a drawable that has no material at all.
}
```

- [ ] **Step 2: Preserve the packaged bytecode route unchanged.**

`BuildShaderResource` must continue to select the material’s vertex/pixel program and variant via `GetShaderBinary`, create Direct3D shaders from `ShaderBinaryAsset::Bytecode`, and build the input layout from the shader asset reflection. Do not add a second HLSL compiler path for `ForwardStandardShader`.

- [ ] **Step 3: Run the focused source-contract test and verify it passes.**

Run:

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter FullyQualifiedName~WindowsPackagedForwardShaderSourceTests
```

Expected: 2 passed.

- [ ] **Step 4: Commit the runtime contract change.**

```powershell
git add src/platform/windows/win32/win32_render_bridge.cpp src/platform/windows/win32/win32_render_bridge.hpp builder.tests/WindowsPackagedForwardShaderSourceTests.cs
git commit -m "fix: require packaged shaders for Windows materials"
```

### Task 3: Verify Windows packaging provides the exact standard-shader dependency

**Files:**
- Modify: `builder.tests/WindowsPlatformAssetBuilderTests.cs`
- Test: `builder.tests/helengine.windows.builder.tests.csproj`

- [ ] **Step 1: Write a failing builder test for the complete PBR binding contract.**

Create a Windows `standard-shader` material definition with `base-color`, `roughness`, `metallic`, and `specular`. Assert the build result references `ForwardStandardShader`, and the cooked material has constant buffers named `BaseColorBuffer`, `RoughnessBuffer`, `MetallicBuffer`, and `SpecularBuffer`.

```csharp
Assert.Contains("ForwardStandardShader", result.ReferencedShaderAssetIds);
Assert.Contains(materialAsset.ConstantBuffers, buffer => buffer.Name == "BaseColorBuffer");
Assert.Contains(materialAsset.ConstantBuffers, buffer => buffer.Name == "RoughnessBuffer");
Assert.Contains(materialAsset.ConstantBuffers, buffer => buffer.Name == "MetallicBuffer");
Assert.Contains(materialAsset.ConstantBuffers, buffer => buffer.Name == "SpecularBuffer");
```

- [ ] **Step 2: Run the focused builder test and verify the shipped builder output.**

Run:

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter FullyQualifiedName~WindowsPlatformAssetBuilderTests
```

Expected: the test passes only when the Windows builder includes every requested PBR buffer. If it passes immediately, do not modify `WindowsPlatformAssetBuilder.cs`; record that the builder is not the CubeTest defect.

- [ ] **Step 3: Fix only a missing Windows builder translation, if the test proves one exists.**

Modify `builder/WindowsPlatformAssetBuilder.cs` to emit the missing buffer using the existing standard-material default helpers. Do not change the HLSL or Demo Disc material values in this task.

- [ ] **Step 4: Re-run the builder test and commit.**

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter FullyQualifiedName~WindowsPlatformAssetBuilderTests
git add builder/WindowsPlatformAssetBuilder.cs builder.tests/WindowsPlatformAssetBuilderTests.cs
git commit -m "fix: package Windows standard material PBR buffers"
```

### Task 4: Instrument and isolate CubeTest darkness with the packaged shader path

**Files:**
- Modify: `src/platform/windows/win32/win32_render_bridge.cpp:2880-3005`
- Test: `builder.tests/WindowsPackagedForwardShaderSourceTests.cs`
- Test run: `C:\dev\helprojs\demodisc\output\windows\helengine_windows.exe`

- [ ] **Step 1: Write a failing source-contract test requiring first-frame diagnostics for the selected shader and PBR buffers.**

Assert that the diagnostics include `3d.material_path=packaged_shader`, the shader asset id, and one line per bound `BaseColorBuffer`, `RoughnessBuffer`, `MetallicBuffer`, and `SpecularBuffer`.

- [ ] **Step 2: Add diagnostic lines at the existing `ApplyMaterial` and `BindMaterialConstantBuffers` boundaries.**

Keep diagnostics behind the existing `!HasWrittenRenderSnapshot` condition. Record the selected material id and shader asset id after `BuildShaderResource`; record binding name, slot, and byte count in `BindMaterialConstantBuffers`.

- [ ] **Step 3: Build and run Demo Disc, then inspect the runtime diagnostics for CubeTest.**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File C:\dev\helworks\helengine\scripts\build-platform.ps1 -Project C:\dev\helprojs\demodisc\project.heproj -Platform windows -Output C:\dev\helprojs\demodisc\output\windows
C:\dev\helprojs\demodisc\output\windows\helengine_windows.exe
```

Expected: CubeTest reports `3d.material_path=packaged_shader` and all four PBR buffers. If it does, use the recorded light direction, active light count, and cube normal data to diagnose the darkness as a scene-lighting or normal-transform issue rather than a shader-delivery issue.

- [ ] **Step 4: Commit the diagnostic coverage.**

```powershell
git add src/platform/windows/win32/win32_render_bridge.cpp builder.tests/WindowsPackagedForwardShaderSourceTests.cs
git commit -m "test: trace Windows packaged PBR material bindings"
```

### Task 5: Full verification and handoff

**Files:**
- Verify: `builder.tests/helengine.windows.builder.tests.csproj`
- Verify: `C:\dev\helprojs\demodisc\output\windows\helengine_windows.exe`

- [ ] **Step 1: Run all Windows builder tests.**

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj
```

Expected: all tests pass with no new warnings.

- [ ] **Step 2: Build Demo Disc using the shared platform script.**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File C:\dev\helworks\helengine\scripts\build-platform.ps1 -Project C:\dev\helprojs\demodisc\project.heproj -Platform windows -Output C:\dev\helprojs\demodisc\output\windows
```

Expected: `C:\dev\helprojs\demodisc\output\windows\helengine_windows.exe` is recreated and non-empty.

- [ ] **Step 3: Validate CubeTest manually and record the result.**

Confirm the player loads CubeTest without a missing-shader fallback and capture the runtime diagnostic lines proving the packaged shader and PBR buffers were bound. Do not claim the brightness issue is fixed unless the lit faces visibly improve or the diagnostic evidence identifies and fixes the separate lighting/normal defect.

- [ ] **Step 4: Commit only after verification succeeds.**

```powershell
git status --short
git add builder src/platform/windows/win32 builder.tests docs/superpowers/plans/2026-07-24-windows-packaged-forward-standard-shader-plan.md
git commit -m "fix: enforce Windows packaged standard shader path"
```
