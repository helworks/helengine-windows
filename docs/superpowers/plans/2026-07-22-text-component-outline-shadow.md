# Text Component Outline and Shadow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add crisp, configurable outline and shadow rendering to `TextComponent` across the shared engine, editor preview, DirectX 11, and Vulkan backends.

**Architecture:** Keep the existing CPU per-glyph text submission path. Extend the shared drawable contract and component state, then make each backend submit optional shadow and outline glyph copies before the normal glyph. Extend exact-preview state capture/synchronization so the editor uses the same values and dirty-state behavior. Use a small shared render-pass description type for deterministic pass-order tests without changing generated code.

**Tech Stack:** C#, .NET, xUnit, DirectX 11 sprite rendering, Vulkan quad rendering, engine scene persistence.

---

### Task 1: Add the authored text-effect API and state tracking

**Files:**
- Modify: `engine/helengine.core/model/interfaces/ITextDrawable2D.cs`
- Modify: `engine/helengine.core/components/2d/TextComponent.cs`
- Test: `engine/helengine.editor.tests/TextComponentRenderStateVersionTests.cs`

- [ ] **Step 1: Write failing tests for defaults, validation, and dirty-state tracking**

Add tests covering the public contract:

```csharp
[Fact]
public void Constructor_InitializesTextEffectsDisabledWithTransparentColors() {
    TextComponent component = new TextComponent();

    Assert.Equal(0f, component.OutlineScale);
    Assert.Equal(new float2(0f, 0f), component.ShadowOffset);
    Assert.Equal(new byte4(0, 0, 0, 0), component.OutlineColor);
    Assert.Equal(new byte4(0, 0, 0, 0), component.ShadowColor);
}

[Fact]
public void OutlineScale_WhenNegative_ThrowsArgumentOutOfRangeException() {
    TextComponent component = new TextComponent();

    Assert.Throws<ArgumentOutOfRangeException>(() => component.OutlineScale = -1f);
}

[Fact]
public void TextEffectProperties_ChangeTextRenderStateVersionOnlyWhenValuesChange() {
    TextComponent component = new TextComponent();
    int initialVersion = component.TextRenderStateVersion;

    component.OutlineScale = 2f;
    int afterOutlineScale = component.TextRenderStateVersion;
    component.OutlineScale = 2f;
    Assert.Equal(afterOutlineScale, component.TextRenderStateVersion);

    component.OutlineColor = new byte4(1, 2, 3, 4);
    component.ShadowOffset = new float2(3f, 4f);
    component.ShadowColor = new byte4(5, 6, 7, 8);

    Assert.True(component.TextRenderStateVersion > initialVersion);
}
```

Use the repository's existing xUnit naming and assertion style in the target test file. Keep the tests independent so each setter's no-op behavior can be diagnosed.

- [ ] **Step 2: Run the focused tests and verify they fail for missing members**

Run:

```powershell
rtk dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj -c Debug --filter FullyQualifiedName~TextComponentRenderStateVersionTests
```

Expected: compilation failure because the four new properties do not exist yet.

- [ ] **Step 3: Implement the shared properties**

Add documented backing fields and public properties to `TextComponent`, ordered with the existing fields/properties. Use transparent black `byte4(0, 0, 0, 0)` and zero offsets in the constructor. `OutlineScale` must reject values below zero and all four setters must compare components before calling the existing `MarkTextRenderStateDirty()` method.

Add the same four get/set members to `ITextDrawable2D` so renderer implementations consume the shared contract. Use `float2` for `ShadowOffset`, `float` for `OutlineScale`, and `byte4` for both colors.

- [ ] **Step 4: Run the focused tests and verify they pass**

Run the same focused `rtk dotnet test` command. Expected: all tests in `TextComponentRenderStateVersionTests` pass with zero failures.

- [ ] **Step 5: Commit the shared API change**

```powershell
rtk git add engine/helengine.core/model/interfaces/ITextDrawable2D.cs engine/helengine.core/components/2d/TextComponent.cs engine/helengine.editor.tests/TextComponentRenderStateVersionTests.cs
rtk git commit -m "feat: add text outline and shadow properties"
```

### Task 2: Extend editor exact-preview state and synchronization

**Files:**
- Modify: `engine/helengine.editor/model/EditorExact2DPreviewRenderState.cs`
- Modify: `engine/helengine.editor/managers/scene/EditorExact2DPreviewDirtyStateComparer.cs`
- Modify: `engine/helengine.editor/managers/scene/EditorExact2DPreviewCaptureService.cs`
- Test: `engine/helengine.editor.tests/EditorExact2DPreviewDirtyStateComparerTests.cs`
- Test: `engine/helengine.editor.tests/EditorExact2DPreviewCaptureServiceTests.cs`

- [ ] **Step 1: Write failing preview-state tests**

Add tests that capture a `TextComponent` with non-default effect values, assert those values are present in `EditorExact2DPreviewRenderState`, and assert changing each value makes `RequiresRecapture` return `true`. Add a capture-service assertion that the hidden preview text component receives the same `OutlineScale`, `OutlineColor`, `ShadowOffset`, and `ShadowColor`.

- [ ] **Step 2: Run the focused preview tests and verify they fail**

```powershell
rtk dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj -c Debug --filter FullyQualifiedName~EditorExact2DPreviewDirtyStateComparerTests|FullyQualifiedName~EditorExact2DPreviewCaptureServiceTests
```

Expected: failures because the snapshot and synchronization omit the new values.

- [ ] **Step 3: Add preview snapshot fields and comparisons**

Add the four documented properties to `EditorExact2DPreviewRenderState`. Populate them in `CaptureTextState`, compare them in `RequiresRecapture`, and assign them in `SynchronizeTextComponent` immediately beside the existing text visual properties.

- [ ] **Step 4: Run the focused preview tests and verify they pass**

Run the same focused preview test command. Expected: all selected tests pass with zero failures.

- [ ] **Step 5: Commit the preview integration**

```powershell
rtk git add engine/helengine.editor/model/EditorExact2DPreviewRenderState.cs engine/helengine.editor/managers/scene/EditorExact2DPreviewDirtyStateComparer.cs engine/helengine.editor/managers/scene/EditorExact2DPreviewCaptureService.cs engine/helengine.editor.tests/EditorExact2DPreviewDirtyStateComparerTests.cs engine/helengine.editor.tests/EditorExact2DPreviewCaptureServiceTests.cs
rtk git commit -m "feat: preserve text effects in editor preview"
```

### Task 3: Implement shared effect-pass ordering in the DirectX 11 text renderer

**Files:**
- Create: `engine/helengine.core/utils/TextRenderEffectPass.cs`
- Create: `engine/helengine.core/utils/TextRenderEffectPassBuilder.cs`
- Modify: `engine/helengine.directx11/DirectX11Renderer2D.cs`
- Test: `engine/helengine.editor.tests/TextRenderEffectPassBuilderTests.cs`

- [ ] **Step 1: Add a failing shared pass-builder test for pass order and offsets**

Create a focused test around `TextRenderEffectPassBuilder.Build` for one glyph position. Assert that submissions occur in this order: shadow color at the shadow offset; outline color at each of the four cardinal offsets; normal color at the original position. Also assert a default component produces only the normal submission.

- [ ] **Step 2: Run the focused renderer test and verify it fails**

Run the selected test with `rtk dotnet test` and confirm the failure is caused by missing effect submissions rather than test setup.

- [ ] **Step 3: Add the shared pass description and refactor the DirectX 11 glyph loop**

Keep text wrapping, line offsets, glyph lookup, advances, snapping, and source rectangles unchanged. For each glyph, submit optional passes through the existing `SpriteShaderData` update and draw call:

```text
shadow:  draw at glyph position + ShadowOffset using ShadowColor
outline: draw at glyph position + (-OutlineScale, 0) using OutlineColor
         draw at glyph position + ( OutlineScale, 0) using OutlineColor
         draw at glyph position + (0, -OutlineScale) using OutlineColor
         draw at glyph position + (0,  OutlineScale) using OutlineColor
main:    draw at the original glyph position using Color
```

Represent each pass with the new `TextRenderEffectPass` class, containing the pixel offset and `byte4` color. Have `TextRenderEffectPassBuilder.Build` emit the ordered passes and let the DirectX 11 loop apply each pass to its existing `destRect`/`color` fields. Apply offsets in the same pixel coordinate space as `destRect`, preserve the existing parent rotation/transform path, and retain the existing draw-call accounting for every submitted glyph copy.

- [ ] **Step 4: Run the focused renderer test and verify it passes**

Run the focused renderer test. Expected: pass for enabled effects and the single-draw disabled case.

- [ ] **Step 5: Commit the DirectX 11 renderer change**

```powershell
rtk git add engine/helengine.core/utils/TextRenderEffectPass.cs engine/helengine.core/utils/TextRenderEffectPassBuilder.cs engine/helengine.directx11/DirectX11Renderer2D.cs engine/helengine.editor.tests/TextRenderEffectPassBuilderTests.cs
rtk git commit -m "feat: render text effects on directx11"
```

### Task 4: Implement the same effect passes in Vulkan

**Files:**
- Modify: `engine/helengine.vulkan/VulkanRenderer2D.cs`
- Test: `engine/helengine.editor.tests/TextRenderEffectPassBuilderTests.cs`

- [ ] **Step 1: Confirm the shared pass-builder test covers Vulkan's required pass order**

The shared `TextRenderEffectPassBuilderTests` test is the deterministic contract for both backends: Vulkan must consume the same ordered pass sequence as DirectX 11. No Vulkan-specific GPU test is added because the current engine test project has no Vulkan render-submission seam.

- [ ] **Step 2: Run the focused Vulkan test and verify it fails**

Run the selected test and confirm it fails because Vulkan still submits only the normal glyph.

- [ ] **Step 3: Add effect-aware `DrawQuad` submissions to `VulkanRenderer2D.DrawText`**

Preserve the existing layout loop and call `DrawQuad` with the effect color and translated position for the optional passes, followed by the existing main call. Do not alter atlas UVs, wrapping, alignment, line-height, or rotation behavior.

- [ ] **Step 4: Run the focused Vulkan test and verify it passes**

Expected: enabled-effect ordering and disabled-effect draw count both pass.

- [ ] **Step 5: Commit the Vulkan renderer change**

```powershell
rtk git add engine/helengine.vulkan/VulkanRenderer2D.cs
rtk git commit -m "feat: render text effects on vulkan"
```

### Task 5: Verify persistence, editor build, and DemoDisc Windows packaging

**Files:**
- Modify only files required by failing persistence tests; do not patch generated output.
- Test: the focused engine/editor test projects and the DemoDisc Windows build output.

- [ ] **Step 1: Run the focused engine test set**

```powershell
rtk dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj -c Debug --filter FullyQualifiedName~TextComponentRenderStateVersionTests|FullyQualifiedName~EditorExact2DPreviewDirtyStateComparerTests|FullyQualifiedName~EditorExact2DPreviewCaptureServiceTests
```

Expected: all selected tests pass.

- [ ] **Step 2: Run the relevant engine project build**

```powershell
rtk dotnet build C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\helengine.editor.app.csproj -c Debug
```

Expected: build exits with code 0. Existing repository warnings may remain, but no new compilation errors are allowed.

- [ ] **Step 3: Build DemoDisc for Windows**

```powershell
rtk proxy powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\dev\helworks\helengine\scripts\build-platform.ps1 -Project C:\dev\helprojs\demodisc\project.heproj -Platform windows -Output C:\dev\helprojs\demodisc\output\windows
```

Expected: the script reports `Build completed for platform 'windows'` and writes `C:\dev\helprojs\demodisc\output\windows\helengine_windows.exe`.

- [ ] **Step 4: Verify the packaged executable exists and is fresh**

```powershell
rtk proxy powershell.exe -NoProfile -Command "Get-Item 'C:\dev\helprojs\demodisc\output\windows\helengine_windows.exe' | Select-Object FullName,Length,LastWriteTime"
```

Expected: the executable exists with a timestamp from the current build.

- [ ] **Step 5: Commit the completed implementation**

```powershell
rtk git status --short
rtk git add engine docs/superpowers/plans/2026-07-22-text-component-outline-shadow.md
rtk git commit -m "feat: support text outlines and shadows"
```

Review any generated or authored changes before committing; never rewrite generated files to hide a generator defect.
