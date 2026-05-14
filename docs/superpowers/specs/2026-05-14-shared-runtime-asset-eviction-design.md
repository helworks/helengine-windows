## Summary

Introduce a shared runtime asset eviction contract for scene-owned `RuntimeTexture`, `FontAsset`, `RuntimeModel`, and `RuntimeMaterial` so platform players can release renderer-owned resources through one consistent engine seam. Keep diagnostics and player-specific logging as observers of that lifecycle rather than the mechanism that performs eviction.

## Problem

The current runtime scene unload flow tracks scene-owned assets in shared core, but backend eviction support is inconsistent across platforms. Windows currently builds textures, models, and materials into backend caches yet does not expose a complete matching release path, which allows scene round-trips to grow cache counts and process memory over time.

The current diagnostics baseline for City on Windows shows two distinct issues:

- the same font atlas is rebuilt many times during one menu load
- scene-owned runtime assets are not being evicted cleanly when leaving a scene

The second issue is broader than Windows-specific texture cleanup. The engine needs one shared release contract that all runtime backends can implement for the resource types they create.

## Goals

- Define one shared runtime asset release contract for:
  - `RuntimeTexture`
  - `FontAsset`
  - `RuntimeModel`
  - `RuntimeMaterial`
- Keep scene-owned asset lifetime decisions in shared core.
- Keep actual native or GPU eviction in the backend renderers.
- Allow optional diagnostics or player listeners to observe release lifecycle events without owning eviction behavior.
- Make Windows honor the shared release contract so scene round-trips stop monotonically growing renderer caches.

## Non-Goals

- Do not redesign the content manager caching model in this pass.
- Do not solve repeated font-atlas rebuilds during one scene load in this pass.
- Do not move asset lifetime ownership out of `SceneManager` and `RuntimeSceneAssetReferenceResolver`.
- Do not make diagnostics required for eviction to work.

## Recommended Approach

Use the renderer APIs as the shared eviction seam.

Shared core already knows which runtime assets are scene-owned. Renderers already know how those runtime assets map to backend-native resources. The correct boundary is for shared core to request release through renderer contracts, and for each backend to implement the actual resource destruction or deferred release strategy it needs.

This approach aligns with the existing `RenderManager2D.ReleaseTexture(...)` seam and with the Vulkan renderer, which already performs real texture release. It avoids coupling `SceneManager` directly to backend caches and avoids introducing a second ownership system above the renderers.

## Alternatives Considered

### 1. Shared runtime asset lifecycle service above the renderers

This would centralize release notifications, but it adds another ownership layer and duplicates renderer knowledge about native resources. It is less direct than using the existing renderer boundary.

### 2. Scene-manager eviction callbacks

This exposes scene context, but it is too scene-specific. It does not generalize well to runtime assets that are not tied directly to a scene unload, and it encourages backend-specific handling to leak upward into scene management.

### 3. Content-manager eviction hooks

This helps packaged asset loads, but runtime-generated assets such as font atlas textures are renderer-created and would still require renderer release semantics. It does not solve the core ownership boundary cleanly.

## Architecture

### Shared Ownership Flow

Shared core retains responsibility for deciding when scene-owned runtime assets are no longer needed.

- `RuntimeSceneAssetReferenceResolver` tracks scene-owned runtime assets while a scene is materialized.
- `RuntimeSceneOwnedAssetSet` carries the scene-owned runtime asset references alongside the loaded scene.
- `SceneManager` releases scene-owned runtime assets when the owning scene unloads and reference counts reach zero.

That ownership flow remains intact. The change is that all scene-owned runtime asset types must release through explicit renderer contracts instead of relying on partial disposal behavior.

### Renderer Release Boundary

Backends remain responsible for destroying or evicting renderer-owned resources.

- `RenderManager2D` owns runtime texture and font release semantics.
- `RenderManager3D` owns runtime model and material release semantics.
- Backends may destroy immediately or defer destruction until a safe point.
- Backends may maintain caches internally, but those caches must be updated consistently when release completes.

### Lifecycle Observation

Diagnostics and player-specific tooling observe release lifecycle events but do not trigger release themselves.

Release notifications are emitted from the renderer release path, not from `SceneManager`. This keeps the event source close to the backend that knows whether a resource was queued, evicted immediately, or flushed later.

## Contract Shape

### RenderManager2D

The shared 2D renderer contract remains the primary seam for 2D scene-owned resources:

- `ReleaseTexture(RuntimeTexture texture)`
- `ReleaseFont(FontAsset font)`
- `FlushReleasedTextures()`

Backends that cannot destroy textures immediately may queue them during `ReleaseTexture(...)` and complete the eviction during `FlushReleasedTextures()`.

### RenderManager3D

The shared 3D renderer contract gains matching release seams for 3D scene-owned resources:

- `ReleaseModel(RuntimeModel model)`
- `ReleaseMaterial(RuntimeMaterial material)`
- `FlushReleasedAssets()` or an equivalently named 3D flush method if deferred destruction is required

The exact method names should stay consistent with the current engine naming style, but the intent is fixed: shared core must have one explicit API to request release for models and materials.

### Release Notifications

Release notifications are optional, additive, and backend-neutral.

Each notification should carry enough data to support diagnostics without forcing any one logging format:

- asset kind
- runtime asset identifier when available
- release phase
  - `requested`
  - `completed`
- optional backend or platform context

Immediate-destroy backends emit `requested` and `completed` during the same release path. Deferred backends emit `requested` when queued and `completed` when the queued eviction is flushed.

## Windows Responsibilities

Windows must implement the shared contract fully for the resource types it creates and caches.

This includes:

- removing released runtime textures from `TextureResources`
- releasing font-owned runtime textures through the 2D renderer path
- releasing model-owned GPU buffers when runtime models are released
- evicting material shader resources and authored material constant-buffer state when runtime materials are released
- clearing any remaining renderer-owned caches during renderer disposal

Windows diagnostics should also log release-requested and release-completed events so the current scene-memory log can prove the contract is being exercised.

## Compatibility

Existing backends that already honor part of the contract, such as Vulkan texture release, should remain compatible with only the minimal changes required by the new shared API surface.

The design does not require every backend to adopt the same cache implementation. It only requires every backend to expose the same release semantics to shared core.

## Verification

### Shared Runtime Verification

- Add or update shared engine tests that prove scene-owned runtime assets are tracked and released through the renderer contracts for textures, fonts, models, and materials.
- Verify repeated unloads do not double-release shared scene-owned assets while references remain alive.

### Windows Verification

- Rebuild City through the editor Windows build pipeline using the existing narrowed diagnostics scenario.
- Run `DemoDiscMainMenu -> cube_test -> DemoDiscMainMenu`.
- Confirm release-requested and release-completed events appear for scene-owned textures, fonts, models, and materials.
- Confirm renderer cache counters return near the original one-scene steady-state baseline instead of monotonically increasing.
- Confirm RAM usage after returning to one loaded scene is near the original one-scene steady-state footprint rather than continuing to ratchet upward.

## Expected Outcome

After this work, scene unload behavior becomes shareable instead of backend-accidental.

Shared core will request release of all tracked runtime asset classes consistently. Each platform backend will own its actual eviction strategy. Players and diagnostics will be able to observe release lifecycle events without embedding platform-specific cleanup logic into scene management.
