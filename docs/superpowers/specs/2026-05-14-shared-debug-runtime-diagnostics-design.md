# Shared Debug Runtime Diagnostics Design

## Summary

Introduce a shared, debug-build-only runtime diagnostics abstraction in the engine core so platforms can report deeper process/runtime memory state through one portable API. Windows will be the first concrete provider and will move its current host-side memory snapshot path behind this shared contract. Release builds must exclude this system at compile time.

## Goals

- Provide one shared engine-level abstraction for runtime memory/process diagnostics.
- Keep the abstraction portable and semantic rather than Windows-specific.
- Restrict the entire diagnostics system to debug/development builds.
- Preserve platform-specific richness through optional detail fields.
- Use the shared service to report actual tracked loaded scene ids instead of the current host-side latched value.
- Support deeper investigation of RAM growth that is not explained by renderer asset counters.

## Non-Goals

- Replacing the engine allocator or instrumenting every allocation site in this pass.
- Guaranteeing identical metrics across all platforms.
- Shipping this diagnostics surface in release builds.
- Solving the remaining RAM growth in this design alone.

## Current Problems

The current Windows diagnostics pipeline mixes two concerns:

- shared engine scene/resource lifecycle information
- Windows-only process memory sampling

That has two practical issues:

1. The useful memory/process investigation surface is trapped in the Windows host instead of living behind a reusable engine seam.
2. `tracked_scene_ids` is currently maintained by a host-side latch and is not reliable after the first scene transition.

The asset/resource diagnostics are already useful, but they no longer explain the remaining working-set/private-usage growth. We need a shared debug-only runtime diagnostics surface that can correlate:

- loaded scenes
- resource counters
- process memory state
- platform-specific details

## Recommended Approach

Implement a shared debug-only `RuntimeDiagnosticsService` in engine core with a platform-provided `IRuntimeDiagnosticsProvider`. The provider returns portable snapshot data plus optional backend-specific detail fields. Windows becomes the first provider and continues writing the existing diagnostics log, but now sources process/runtime snapshot data through the shared service.

This keeps ownership boundaries clean:

- core defines diagnostics vocabulary and lifecycle
- platforms provide measurements they can actually observe
- host/platform logging remains optional presentation on top of the shared model

## Architecture

### Shared Core Types

Add debug-only core types for:

- `RuntimeDiagnosticsService`
- `IRuntimeDiagnosticsProvider`
- `RuntimeMemoryDiagnosticsSnapshot`
- `RuntimeDiagnosticsCheckpoint` or equivalent named sample-point payload

The shared snapshot should prefer semantic fields:

- `ResidentBytes`
- `PeakResidentBytes`
- `CommittedBytes`
- `PeakCommittedBytes`
- `AvailablePhysicalBytes`
- `PageFaultCount`
- `TrackedSceneIds`

Optional platform-specific details should be carried separately, for example through a string-keyed metrics collection, so the shared type stays portable.

### Provider Contract

Platforms register one diagnostics provider during core initialization in debug builds only. The provider is responsible for returning:

- current generic memory/process counters
- tracked loaded scene identifiers when the platform or core can supply them
- optional platform-specific detail metrics

The contract must allow partial implementations. A platform may return only the fields it can measure honestly.

### Scene Tracking

The shared service should source tracked scene ids from the actual loaded-scene state, not from a host latch of the last transition event. The host can still log scene-manager transition stages, but the steady-state scene identity should come from the loaded scene set represented by core.

### Windows Implementation

Windows becomes the first real provider:

- move the current process-memory sampling behind the shared provider
- keep Windows-specific extra fields such as paged pool, nonpaged pool, and system commit totals as optional detail metrics
- continue writing the existing diagnostics log, but populate shared snapshot fields from the provider

This keeps the diagnostics output useful immediately without committing the engine to Windows-only naming.

## Build Behavior

This system must be debug-build only.

Requirements:

- compile the shared diagnostics service only in debug/development builds
- compile provider implementations only in debug/development builds
- exclude diagnostics logging and snapshot collection code from release/shipping builds at compile time
- avoid runtime no-op branches as the primary mechanism when compile-time exclusion is available

In release builds:

- no diagnostics provider is created
- no runtime diagnostics snapshots are captured
- no new diagnostics files are written by this shared service

## Logging and Consumption

The shared service defines the snapshot/checkpoint data model. Platforms or hosts may choose how to emit it.

For Windows:

- preserve the existing diagnostics file flow
- enrich it with the shared snapshot fields
- keep renderer/resource counters alongside memory counters
- record actual tracked scene ids from the loaded-scene set

This design intentionally keeps logging separate from measurement so other platforms can surface the same data differently.

## Rollout Plan

1. Introduce the shared debug-only core abstraction and provider contract.
2. Wire the provider through core initialization in debug builds.
3. Move Windows process-memory sampling behind the Windows provider.
4. Route steady-state tracked scene ids through the actual loaded-scene data instead of the current host latch.
5. Keep existing Windows renderer/resource diagnostics and combine them with the shared snapshot output.
6. Rebuild and rerun the City all-scenes pass to evaluate whether the remaining RAM growth is outside tracked renderer resources.

## Risks

- If the shared API includes too many Windows-shaped fields, it will become awkward for other platforms.
- If scene identity remains sourced from transition traces instead of loaded-scene state, the new abstraction will not solve the current labeling bug.
- If compile-time guards are inconsistent across repos/platforms, diagnostics code may accidentally leak into non-debug builds.

## Success Criteria

- A shared engine-level debug-only diagnostics abstraction exists in core.
- Windows uses that abstraction for runtime memory/process snapshots.
- Steady-state scene diagnostics report the actual loaded scene ids.
- Release builds do not include this diagnostics path.
- The next all-scenes diagnostics run can distinguish renderer-resource baseline from unexplained process memory growth.
