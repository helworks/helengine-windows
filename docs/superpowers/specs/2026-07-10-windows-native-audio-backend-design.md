# Windows Native Audio Backend Design

## Goal

Enable audible runtime playback in the native Windows player so authored `AudioSourceComponent` instances, including the City main-menu music, can play through the existing shared runtime audio surface without any scene or asset authoring changes.

## Problem Statement

The current City menu scene correctly authors a `DemoDiscMenuMusic` entity with an `AudioSourceComponent` that references a cooked audio asset and requests automatic looping playback on the `music` bus. The cooked Windows build also contains the expected `.hasset` audio payload.

The missing link is the native Windows runtime. The playable Windows executable is hosted by `helengine-windows`, and that host currently never installs an `IAudioBackend` into the generated engine `Core`. As a result:

- `AudioSourceComponent` resolves and initializes correctly.
- `AudioSourceComponent.Play()` exits early because `Core.Instance.AudioManager` is absent.
- No audio reaches the Windows player even though authoring and cooking succeed.

## Requirements

### Functional

- The native Windows player must install an `IAudioBackend` during engine bootstrap.
- Authored `AudioSourceComponent` playback must begin automatically when `PlayOnStart` is true.
- The backend must support the current shared runtime contract:
  - `Play`
  - `Stop`
  - `IsPlaying`
  - `SetBusGain`
  - `SetBusPaused`
  - `Update`
- The backend must play the currently cooked PCM-style menu asset format used by Windows builds.
- Looping playback must work for menu music.

### Non-Functional

- The change must stay inside the existing shared runtime abstractions. No menu-specific or project-specific host logic is allowed.
- The first implementation should be Windows-only and minimal. It should not introduce a speculative cross-platform audio architecture rewrite.
- The backend should fail clearly when an audio asset is structurally invalid instead of silently manufacturing fallback behavior.

## Recommended Approach

Implement a native `Win32AudioBackend` inside `helengine-windows` and wire it into `Win32Application::InitializeEngineCore()` immediately after the core, render managers, and input backend are created.

This matches the current Windows host pattern:

- Native host code already provides platform implementations for generated runtime interfaces such as `IInputBackend`.
- The generated shared runtime already knows how to drive authored audio through `AudioManager` once an `IAudioBackend` exists.
- Adding the backend at the host boundary fixes the real runtime gap without changing scene generation, cooking, or gameplay code.

## Alternatives Considered

### 1. Native backend in `helengine-windows` (recommended)

Pros:

- Fixes the actual playable runtime.
- Aligns with existing native host responsibilities.
- Preserves the shared `AudioSourceComponent` and `AudioManager` contract for future platforms.

Cons:

- Requires native Windows audio implementation work.

### 2. Reuse the managed `helengine.core.windows` backend

Pros:

- Logic already exists in C#.

Cons:

- The shipping Windows player is native, not managed.
- This would not solve the runtime path that actually launches for City builds.

### 3. Special-case menu music in the host

Pros:

- Potentially faster one-off proof of sound output.

Cons:

- Breaks engine architecture.
- Does not solve general authored runtime audio.
- Would need deletion or redesign immediately afterward.

## Architecture

### New Native Backend

Add one new native backend type under `helengine-windows/src/platform/windows/win32/`:

- `win32_audio_backend.hpp`
- `win32_audio_backend.cpp`

Responsibilities:

- Implement generated `IAudioBackend`.
- Own active voice lifetime and playback state.
- Decode runtime `AudioAsset` data into the Windows playback surface expected by the chosen native API.
- Apply shared bus gain and bus paused state across active voices.

### Host Bootstrap Wiring

Update `Win32Application` to:

- Own one `Win32AudioBackend* EngineAudioBackend` field.
- Construct the backend during `InitializeEngineCore()`.
- Call `EngineCore->SetAudioBackend(EngineAudioBackend)` after core initialization state is valid.
- Dispose the backend during application shutdown alongside the other host-owned engine services.

### Voice Tracking

The backend will track:

- Stable integer voice ids.
- Per-voice playback object handles.
- Per-voice bus id.
- Per-voice base gain.

This mirrors the existing managed backend behavior closely enough that the shared runtime does not need new concepts.

## Playback API Choice

Use a straightforward native Windows playback API suitable for a minimal first implementation. The implementation should prioritize reliability and easy ownership over low-level optimization.

For the first slice, the backend only needs to support:

- PCM playback from already cooked bytes.
- One-shot and looped playback.
- Multiple simultaneous voices at small runtime scale.

This design intentionally does not require streaming, advanced mixing, pitch shifting, or format negotiation beyond what current cooked Windows assets already provide.

## Data Flow

1. The City menu scene loads in the native Windows player.
2. `AudioSourceComponent` initializes and sees `PlayOnStart = true`.
3. `AudioSourceComponent.Play()` asks `Core.Instance.AudioManager` to play the resolved `AudioAsset`.
4. `AudioManager` forwards the request to the installed native `Win32AudioBackend`.
5. The backend creates one native voice, starts playback, and returns the voice id.
6. The shared runtime continues to use `Update`, `Stop`, `SetBusGain`, and `SetBusPaused` through the same contract.

## Error Handling

- `Play` must throw when the incoming `AudioAsset` is null.
- `Play` must throw when the asset has invalid required playback metadata such as non-positive channel count or sample rate.
- `Stop` for unknown voice ids should remain a no-op to preserve the existing shared runtime expectations.
- Bus updates for unknown bus ids should create or normalize bus state consistently with the current shared runtime behavior.

## Testing Strategy

### Test First

Add a failing regression test before implementation that proves the Windows host wires audio into the engine bootstrap.

Preferred initial test:

- Extend the Windows source-level host tests to assert that `win32_application.cpp` constructs a Windows audio backend and calls `SetAudioBackend(...)`.

This test is small, stable, and directly guards the missing integration point that caused the silent runtime.

### Implementation Verification

After the wiring test passes, run the smallest practical validation set:

- Relevant `helengine-windows` builder/source tests.
- Windows build of City.
- Runtime verification that menu music plays in the built executable.

If a native audio backend test surface is practical without excessive harness work, add one focused backend test for successful voice creation from a valid PCM asset. If not, rely on the wiring test plus real build/runtime verification for this vertical slice.

## Scope Boundaries

Included:

- Native Windows audio backend implementation.
- Host bootstrap wiring.
- Minimal voice/bus behavior needed for menu music.
- Regression coverage for host integration.

Excluded:

- Cross-platform engine-wide audio redesign.
- DS or handheld audio conversion work.
- Advanced runtime mixing features.
- Content pipeline redesign.
- Menu-specific hacks.

## Rollout

The first milestone is successful menu music playback in the native Windows City build. Once that works, the same shared runtime authoring path can be used as the reference pattern for later platform audio bring-up.
