# Windows Multi-Input Sources Design

## Goal

Add end-to-end multi-gamepad-source support to `helengine-windows` so a Windows player build can embed any subset of `DirectInput`, `XInput`, and `SteamInput`, and the runtime can poll all enabled backends through one shared input pipeline.

The first release should let the builder choose which backends are embedded in the player, carry those selections into the native runtime, expose normalized gamepad state through the engine input model, and deduplicate overlapping controller reports with a stable priority order.

## Current State

The Windows builder already exposes player-facing settings through `WindowsPlatformDefinitionFactory` and carries selected graphics values into generated native manifest source through `WindowsRuntimeNativeManifestWriter`.

The native Windows host already has:

- a single `Win32InputBackend` implementation of `IInputBackend`
- keyboard capture through Win32 APIs
- mouse capture through Win32 APIs

The current host does not expose any native gamepad polling path and the current input frame population only sets keyboard and mouse state.

That means the current Windows runtime has no seam for:

- build-time selection of supported gamepad libraries
- per-backend native initialization
- normalized gamepad state propagation through `InputFrameState`
- deduplication when the same physical controller appears through multiple Windows APIs

## Requirements

### Functional Requirements

- A Windows player build must be able to embed any subset of:
  - `DirectInput`
  - `XInput`
  - `SteamInput`
- The Windows builder/editor surface must expose those selections independently rather than as a single exclusive choice.
- The native Windows host must only compile and link the requested backends for the current player build.
- The Windows runtime must poll every enabled backend at runtime and merge the resulting gamepad list into one input frame.
- The merged gamepad list must be deduplicated with the fixed priority order:
  - `SteamInput`
  - `XInput`
  - `DirectInput`
- Deduplication must only suppress a lower-priority device when identity evidence is strong enough to indicate that both backend reports represent the same physical controller.
- If device identity is ambiguous, the runtime must keep both devices rather than hide a potentially real second controller.

### Design Requirements

- The engine input contract must gain a minimal shared gamepad model rather than pushing gamepad logic into Windows-only code paths.
- Backend-specific native code must stay isolated behind Windows adapter boundaries.
- The existing keyboard and mouse path must remain behaviorally unchanged.
- The current generated runtime manifest flow must remain the handoff path for build-selected runtime defaults and feature flags.
- Steam-specific configuration errors must fail explicitly at build time rather than silently downgrading requested support.

### Runtime Reliability Requirements

- If a requested SteamInput SDK dependency is missing during native configure/build, the build must fail with a clear error.
- If one enabled backend fails to initialize at runtime, the player must log that failure, disable that backend, and continue with the remaining enabled backends.
- If one device poll fails for a frame, the player must drop that device for the current frame rather than terminate the player.

### Non-Goals

- No user-facing backend toggle UI inside the shipped player.
- No runtime hot-switching of embedded backends after the player has been built.
- No full plugin-discovery system for controller libraries.
- No attempt to solve every possible controller identity edge case in the first pass.
- No controller remapping/settings UX in this feature.

## Approaches Considered

### 1. Windows-Only Inline Gamepad Support

Add all build selection, native backend startup, polling, and dedupe logic directly around `Win32InputBackend` without changing the shared input contract.

This is the shortest path locally, but it keeps gamepad semantics trapped in Windows-specific code and makes future platforms depend on host-specific translation layers instead of a shared engine input model.

### 2. Minimal Shared Gamepad Contract Plus Windows Backend Multiplexer

Add a minimal gamepad representation to the engine input model, then let `helengine-windows` implement backend-specific adapters that feed a multiplexing `Win32InputBackend`.

This keeps the shared model small, preserves platform-specific native details inside the Windows layer, and creates a clean path for future platforms or future backends to reuse the same frame shape.

### 3. Full Runtime Input Plugin Architecture

Create a larger registration/discovery system where controller backends are independent runtime modules.

This is the most flexible long-term direction, but it is substantially over-scoped for the first feature and would slow delivery without solving the immediate user need better than a smaller adapter design.

## Decision

Use approach 2.

The implementation will introduce a minimal shared gamepad contract at the engine input boundary and keep backend-specific integration inside `helengine-windows`. The Windows host will become a multiplexer over the enabled backend subset and will deduplicate overlapping device reports with conservative fixed-priority suppression.

## Architecture

### Build and Editor Surface

The Windows platform definition should add three independent boolean settings:

- `embed-xinput`
- `embed-directinput`
- `embed-steaminput`

These are player/runtime capability selections, not graphics profile choices. They should live alongside the existing Windows build/runtime configuration surface so users can choose any subset for the player they are building.

The builder should treat these values as the authoritative requested backend set for the current build.

### Generated Runtime Handoff

The existing Windows native manifest writer flow should be extended so the selected backend booleans are emitted into generated native runtime metadata in the same handoff family that already carries width and height defaults.

That generated metadata should provide native code with:

- whether `XInput` was embedded
- whether `DirectInput` was embedded
- whether `SteamInput` was embedded

The builder/native build path should also translate those same values into native configure definitions so CMake can compile and link only the requested backend source paths and dependencies.

### Shared Input Contract

The engine input model should gain a minimal gamepad representation that can be serialized through one `InputFrameState`.

The first version of that contract must include:

- normalized button state
- normalized trigger/axis values
- stable per-device identity fields sufficient for dedupe and diagnostics
- backend provenance metadata

The contract should remain intentionally small. It only needs enough information to represent an authored gamepad frame consistently across backends.

### Windows Backend Layer

`Win32InputBackend` should remain the Windows host implementation of `IInputBackend`, but internally it should compose specialized backend adapters:

- one adapter for `XInput`
- one adapter for `DirectInput`
- one adapter for `SteamInput`

Each adapter is responsible for:

- one-time backend initialization
- per-frame device enumeration/polling
- mapping raw backend data into the shared gamepad contract
- reporting stable backend/device identity information used by merge and diagnostics

### Frame Merge and Dedupe

For each frame, the Windows input backend should:

1. capture keyboard state
2. capture mouse state
3. poll every enabled gamepad adapter
4. concatenate candidate gamepad reports
5. apply conservative dedupe using fixed priority:
   - `SteamInput`
   - `XInput`
   - `DirectInput`
6. publish the merged gamepad list through `InputFrameState`

The dedupe algorithm should only suppress a lower-priority report when the runtime can confidently match it to a higher-priority device through stable identity data such as backend slot, instance identity, GUID-style metadata, or explicit API-provided correlation.

If the runtime cannot make that match confidently, both devices should remain visible for the frame.

### SteamInput Build Rules

`SteamInput` differs from the other two backends because it depends on Steamworks SDK artifacts and runtime initialization.

Therefore:

- enabling `embed-steaminput` must participate in native configure/build validation
- the native build must fail clearly if the required Steamworks SDK path, headers, or libraries are unavailable
- the generated player must only contain SteamInput code when the build explicitly requested it

This avoids shipping a player that claims SteamInput support without actually containing the required implementation.

## Data Flow

### Build Time

1. User selects any subset of the three gamepad backend settings in the Windows platform configuration.
2. The builder resolves those booleans in the selected build request.
3. The builder writes generated runtime manifest source that embeds the requested backend set.
4. The native build executor passes matching compile/configure definitions into CMake.
5. CMake includes only the requested backend source files and libraries.

### Runtime

1. The Windows player starts.
2. The native host reads generated runtime backend settings.
3. `Win32InputBackend` constructs only the enabled adapters.
4. Each initialized adapter contributes zero or more gamepad reports per frame.
5. The host merges and deduplicates those reports.
6. The final `InputFrameState` contains keyboard, mouse, and the merged gamepad list.

## Error Handling

### Build-Time Errors

- Missing or invalid Steamworks SDK dependencies for a build that requested `SteamInput` must fail the native build with a clear error.
- Invalid backend-setting values coming through the build pipeline must fail explicitly rather than silently falling back to a guessed configuration.

### Runtime Backend Initialization Errors

- Failure to initialize one backend at startup should log the failure and disable only that backend.
- Remaining enabled backends should continue operating.

### Runtime Polling Errors

- Failure to read one device for one frame should only affect that device for the current frame.
- Polling errors should not terminate the player unless the underlying backend API forces a fatal condition that leaves no safe continuation path.

## Testing

### Builder and Manifest Tests

Add coverage proving:

- the three new backend booleans appear in the Windows platform definition
- selected backend values flow through the Windows build request path
- generated runtime manifest output includes the selected backend metadata
- native configure/build arguments change when different backend subsets are requested

### Merge and Dedupe Tests

Add native or host-level coverage for:

- no enabled backend returns no gamepads
- one backend returns one gamepad unchanged
- multiple backends return distinct devices that are all preserved
- lower-priority duplicate reports are removed when identity correlation is strong
- ambiguous identity keeps both reports visible
- priority order is exactly `SteamInput > XInput > DirectInput`

### Integration Coverage

Add at least one build/integration path proving that backend subset selection changes the compiled Windows host inputs rather than only changing metadata.

If native runtime tests are limited, keep the merge/dedupe and backend selection logic behind small seams that can be tested without end-to-end executable launches.

## Implementation Notes

- Keep the shared input contract as small as possible in the first pass.
- Keep adapter boundaries explicit so backend-specific include/link requirements stay contained.
- Do not silently downgrade requested SteamInput support into XInput or DirectInput support.
- Prefer explicit diagnostics over quiet best-effort behavior for configuration errors.
- Preserve current keyboard and mouse behavior while extending the frame shape.

## Success Criteria

The feature is complete when:

- the Windows builder exposes independent toggles for `XInput`, `DirectInput`, and `SteamInput`
- a player build can embed any subset of those backends
- the Windows runtime polls every enabled backend through one input pipeline
- overlapping devices are deduplicated with the agreed priority order
- the engine input frame can carry normalized gamepad state
- SteamInput build misconfiguration fails clearly
- runtime failure of one backend degrades cleanly without crashing the player
