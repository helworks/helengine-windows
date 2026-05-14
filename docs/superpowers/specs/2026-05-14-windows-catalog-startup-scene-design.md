## Summary

Make the Windows player load its startup scene through the runtime scene catalog and `SceneManager` instead of loading a raw packaged startup scene directly through `SceneLoadService`. The first catalog entry becomes the authoritative startup scene. If the catalog is empty, Windows startup fails fast.

## Problem

The current Windows host loads the first scene through a special-case startup manifest path:

- it resolves one packaged scene path
- deserializes the scene asset directly
- calls `SceneLoadService.Load(...)`

That bypasses `SceneManager` tracking for the initial scene. As a result, the startup scene's owned textures and fonts are not registered in `LoadedSceneRecord` or `RuntimeSceneOwnedAssetSet`, so the shared eviction contract does not apply when the player transitions away from that scene.

The latest diagnostics prove this split clearly:

- `cube_test` assets are released correctly through the shared eviction path
- the initial menu scene assets are not released
- returning to menu still leaves `texture_resources` near `48` instead of near the original `24` baseline

## Goals

- Make the runtime scene catalog the authoritative startup source on Windows.
- Load the initial scene through `SceneManager.LoadScene(..., Single)`.
- Ensure the first scene is tracked exactly like every later scene transition.
- Fail fast when the runtime scene catalog is empty.

## Non-Goals

- Do not preserve the old startup-scene manifest path as an active fallback.
- Do not solve repeated font-atlas rebuilds during one menu load in this pass.
- Do not change the runtime scene catalog build order semantics.

## Recommended Approach

Use the runtime scene catalog order as the startup contract.

The first runtime scene catalog entry is the main scene. The Windows host should initialize the core, verify that the catalog exists and contains at least one scene entry, then ask `SceneManager` to load the first scene in `Single` mode.

This removes the split ownership model between “startup scene” and “tracked scene,” and it uses the same scene lifecycle path for every scene from the first frame onward.

## Alternatives Considered

### 1. Keep the startup manifest path and manually register the loaded scene

This could make the first scene tracked after the fact, but it duplicates `SceneManager` responsibilities in the host and creates another place where ownership bookkeeping can drift.

### 2. Keep the startup manifest as a fallback when the catalog is empty

This preserves two startup systems and weakens the contract. The user requirement is clearer: if there are no scenes in the catalog, startup should fail.

### 3. Continue loading startup directly through `SceneLoadService`

This is the current broken path. It bypasses owned-asset tracking for the most important scene in the player.

## Design

### Startup Authority

Windows startup will no longer treat the startup manifest scene path as authoritative for loading the first runtime scene.

Instead:

- the runtime scene catalog must exist
- the runtime scene catalog must contain at least one entry
- the first catalog entry is the startup scene

### Startup Load Path

The Windows host will:

1. initialize the engine core with the built runtime scene catalog
2. resolve the first catalog entry
3. call `SceneManager.LoadScene(firstSceneId, SceneLoadMode.Single)`

This ensures the first scene:

- produces a normal `LoadedSceneRecord`
- owns a normal `RuntimeSceneOwnedAssetSet`
- participates in normal owned-asset reference counting
- releases through the same shared contract as later scenes

### Failure Behavior

If the runtime scene catalog is null or empty, Windows startup throws immediately with a clear error.

That is intentional. Running without a tracked startup scene violates the intended ownership model and hides configuration errors.

### Diagnostics Expectations

The existing diagnostics system remains in place, but the startup scene should now behave like a tracked scene:

- release events should appear when transitioning away from the first menu scene
- texture and material counters should return near the original one-scene steady-state baseline after returning to menu

## Verification

- Build City through the editor Windows pipeline with the narrowed diagnostics scenario.
- Run `DemoDiscMainMenu -> cube_test -> DemoDiscMainMenu`.
- Confirm startup still succeeds and the first scene is the menu scene.
- Confirm release events now appear for the first menu scene when entering `cube_test`.
- Confirm the second menu steady state is near the first menu steady-state baseline instead of near the old leaking `48` texture count.

## Expected Outcome

After this change, the first runtime scene is no longer special from an ownership perspective.

Windows startup will use the same tracked scene lifecycle from frame one, which allows the shared eviction contract to reclaim startup-scene assets correctly during later transitions.
