# Windows Scene Memory Diagnostics Design

## Goal

Add Windows-only runtime diagnostics to `helengine-windows` so scene transitions can be inspected in detail while debugging the City project flow:

- load menu
- load `cube_test`
- return to menu

The first pass is diagnostics-only. It must not change runtime asset lifetime behavior yet. Its job is to show exactly what the Windows player loads, what remains resident after unload, how much process RAM changes across scene transitions, and whether returning to one loaded scene restores the same steady-state footprint.

## Current State

The Windows host already emits lifecycle logs from `win32_application.cpp` and contains renderer-side diagnostics in `win32_render_bridge.cpp`.

The host loads the packaged startup scene and passes it into the generated runtime scene-loading path, but the repository-local Windows layer does not currently expose:

- per-scene transition RAM snapshots
- per-asset-class cache counts
- duplicate load counts by asset id
- content-manager-style debug logging for each resolved packaged asset
- one structured summary that can be compared between the first menu load and the second menu load

The Windows render bridge also owns long-lived native caches such as uploaded textures and material shader resources, which makes observability a prerequisite before changing release behavior.

## Requirements

### Functional Requirements

- The Windows player must emit structured diagnostics for scene transitions.
- Diagnostics must include process RAM snapshots at stable transition checkpoints.
- Diagnostics must include per-asset-class counts for every resource cache owned directly by the Windows bridge.
- Diagnostics must log every native asset build/load event the Windows host and bridge can observe.
- Diagnostics must record duplicate builds by stable asset identifier so repeated loads of the same packaged asset are visible.
- Diagnostics must make it easy to compare:
  - first menu load
  - `cube_test` load
  - return to menu
- Diagnostics must remain available through the existing Windows lifecycle log path or another stable file beside the executable.

### Design Requirements

- This pass must not fix cleanup behavior or change scene lifetime semantics.
- Logging must be structured and machine-diffable rather than free-form only.
- Logging must stay focused on Windows-owned runtime behavior and generated-core seams already exposed to the Windows host.
- Instrumentation must cover both:
  - host/renderer resource accounting
  - content load/build tracing
- The design must support a later pass where fixes can be validated against the same diagnostics output.

### Non-Goals

- No asset eviction or release-behavior changes in this pass.
- No PSP runtime changes in this pass.
- No generalized engine-wide logging framework redesign.
- No external profiling UI.
- No attempt to infer precise allocator-level ownership for memory not visible through the Windows host or generated runtime seams.

## Approaches Considered

### 1. Snapshot-Only Transition Logging

Add RAM and cache-count snapshots only at scene transition boundaries.

This is the smallest implementation, but it is not sufficient for the City debugging goal. It would show that memory changed without showing which asset ids or content paths caused it.

### 2. Structured Transition Snapshots Plus Per-Load Tracing

Add named transition checkpoints, process-memory sampling, renderer cache accounting, and debug-level per-load tracing around packaged asset reads and runtime resource builds.

This provides both macro and micro visibility:

- what the process footprint did
- what scene count changed
- which assets loaded or rebuilt
- whether the same asset id was uploaded or compiled more than once

This is the best first-pass design because it creates a measurable baseline before any cleanup fixes are attempted.

### 3. Full Content-Manager Wrapper Interception

Wrap or replace the generated runtime content manager so every load path is centrally traced.

This is more invasive and depends on deeper generated-core control points than this repository currently owns directly. It is higher risk for the first diagnostics pass.

## Decision

Use approach 2.

The Windows player will gain two coordinated diagnostics layers:

- transition snapshots from the application/scene-host side
- per-load and per-cache tracing from the Windows renderer and packaged-asset load seams

## Architecture

### Diagnostics Scope

Instrumentation is split into three layers:

- `Win32Application` transition diagnostics
- Windows render-bridge cache/resource diagnostics
- packaged asset and runtime asset-resolution trace logging

Each layer has one narrow purpose so logs can be correlated without mixing ownership concerns.

### Transition Diagnostics in `Win32Application`

`Win32Application` becomes the owner of scene-transition checkpoints because it already owns:

- startup lifecycle logging
- the main loop
- startup scene loading
- access to `Core`, `SceneManager`, and the renderer bridges

It should log named checkpoints for:

- host startup before engine initialization
- before packaged startup scene load
- after packaged startup scene load
- before each runtime scene unload/load transition when observable
- after unload completes
- after load completes
- steady-state a short number of frames after the load completes

Each checkpoint records:

- timestamp
- checkpoint name
- current loaded-scene count when available
- process RAM metrics
- renderer cache metrics
- cumulative duplicate-build counters

### Renderer Diagnostics in `win32_render_bridge`

The Windows render bridge should expose readonly diagnostics snapshots describing the native resources it currently owns.

The first version should track at least:

- uploaded texture cache count
- uploaded texture ids currently present
- texture build count per id
- material shader resource cache count
- material shader resource build count per material id
- model build count per model id when the runtime model exposes one
- counts for any other long-lived Windows-owned GPU resources built from packaged assets

The diagnostics surface should separate:

- current live cache size
- cumulative builds
- duplicate builds

That separation matters because a duplicate build count can rise even when the current live count appears stable.

### Content Load and Build Trace Logging

Debug-level logging should be added around each observable asset load seam in the Windows-controlled code:

- packaged startup scene file open and deserialize
- packaged asset file reads from `LoadPackagedAsset`
- runtime texture builds from `BuildTextureFromRaw`
- runtime material builds from `BuildMaterialFromRaw`
- runtime model builds from `BuildModelFromRaw`
- shader-resource construction from packaged shader/material inputs

For each event, the log should record:

- event kind
- asset type
- stable identifier if available
- path if available
- whether this is the first build or a duplicate
- elapsed time for the operation when available

This layer is intentionally verbose and should be treated as debug diagnostics for memory investigation builds.

### Process RAM Sampling

Process RAM sampling should be owned by the Windows host and gathered through Win32 process-memory APIs.

The first snapshot contract should include:

- working set
- private bytes / private usage when available
- pagefile usage / committed usage when available

The log format should keep both raw bytes and a human-readable MB value so the output is easy to diff and easy to read.

### Scene Comparison Model

The diagnostics should make it possible to compare one scene's steady-state footprint against later returns to that same scene.

The key comparison for the City investigation is:

1. menu steady-state after first load
2. `cube_test` steady-state
3. menu steady-state after returning

The design should preserve stable checkpoint names so those three snapshots can be compared directly in logs without manual reconstruction.

## Data Flow

### Startup Scene Load

1. Host initializes and logs a pre-load memory snapshot.
2. Host loads the packaged startup scene asset.
3. Packaged asset load is logged with path and timing.
4. Generated runtime scene loading runs normally.
5. Windows bridge logs each observable runtime resource build.
6. Host logs a post-load snapshot.
7. After a small number of frames, host logs a steady-state snapshot.

### Runtime Scene Change

1. A scene change is requested from the runtime scene manager.
2. Host logs a pre-transition snapshot when it can observe the operation begin.
3. Unload/load work proceeds normally.
4. Windows bridge logs resource builds during the new scene load.
5. Host logs post-unload and post-load snapshots when those boundaries are observable.
6. After a small number of frames, host logs a steady-state snapshot for comparison.

### Duplicate Load Visibility

1. A resource build function resolves one stable asset id.
2. Diagnostics check whether that id has been seen before for that asset class.
3. The event is logged as:
   - first build
   - duplicate build
4. Aggregate duplicate counters are included in later snapshots.

## Log Format

Diagnostics should use structured single-line log records so they can be diffed and filtered with text tools.

Examples of event categories:

- `scene.checkpoint`
- `asset.load`
- `asset.build`
- `cache.snapshot`

Each record should prefer stable key/value fields rather than prose-only messages.

Example shape:

```text
[Diag] scene.checkpoint name=menu_after_load loaded_scenes=1 working_set_bytes=12345678 private_bytes=23456789 texture_cache_count=14 texture_duplicate_builds=0 material_cache_count=6
```

```text
[Diag] asset.build kind=texture asset_id=ui/menu_atlas path=cooked/textures/ui/menu_atlas.hasset duplicate=false elapsed_ms=1.42
```

This keeps the logs easy to scan and easy to compare between runs.

## Error Handling

Diagnostics must not mask runtime failures.

If RAM sampling fails or one diagnostics field cannot be gathered:

- the player should keep running
- the failure should be logged explicitly
- the rest of the diagnostics record should still be emitted when possible

If an asset lacks a stable id, the diagnostics should log an explicit placeholder classification such as generated or anonymous rather than inventing a misleading authored id.

## Testing

### Builder/Unit-Level Coverage

Add tests for the pure formatting and snapshot-building helpers where feasible, including:

- process-memory snapshot formatting
- cache snapshot formatting
- duplicate-build accounting
- stable log record generation

### Native Host Coverage

Where direct runtime integration coverage is practical in this repository, add tests for:

- lifecycle checkpoint emission
- transition checkpoint naming
- packaged asset load trace formatting

### Manual Validation Scenario

The primary validation workflow for this diagnostics pass is manual:

1. build the Windows player for the City project
2. launch into menu
3. transition to `cube_test`
4. return to menu
5. inspect the diagnostics log for:
   - RAM deltas
   - cache counts
   - duplicate asset builds
   - first-menu vs second-menu steady-state mismatch

This manual path is part of the design because the immediate goal is investigation rather than behavior correction.

## Implementation Notes

- Prefer narrow helper types for diagnostics snapshots instead of bloating `Win32Application` or the renderer classes with formatting logic.
- Keep renderer diagnostics readonly from the outside during this pass.
- Track asset classes separately so textures, models, and materials can be compared independently.
- Reuse existing lifecycle logging destinations where possible so one run produces one coherent diagnostic timeline.
- Keep the first pass Windows-specific even if some naming is generic enough for later reuse.

## Success Criteria

The diagnostics pass is complete when:

- the Windows player emits memory and cache snapshots for startup and scene transitions
- every observable Windows-controlled asset load/build is logged with asset type and identifier data
- duplicate builds are visible in the logs
- the City `menu -> cube_test -> menu` flow produces enough diagnostics to compare first-menu and second-menu steady-state footprints
- no runtime cleanup behavior has been changed yet
- the resulting logs are strong enough to validate later disposal fixes
