# Runtime Player Profile Design

## Goal

Add a persisted player profile for `helengine-windows` so the Windows player reads its startup resolution from `profile.json` beside `helengine_windows.exe` instead of relying only on generated deployment defaults.

The first version only persists:

- `resolutionWidth`
- `resolutionHeight`

The implementation must still be shaped generically so later platforms can reuse the same runtime profile contract and loading pattern.

## Current State

The Windows builder already emits generated runtime defaults through `runtime_player_settings_manifest.hpp/.cpp`.

The native host currently consumes those generated functions directly in `win32_application.cpp` when it decides the initial client size for the main window.

That solves deployment defaults, but it does not provide a persisted user profile:

- no player-side JSON file exists
- no user override survives between launches
- no generic runtime profile-loading seam exists for other platforms to adopt later

## Requirements

### Functional Requirements

- The player must look for `profile.json` beside `helengine_windows.exe`.
- If the file does not exist, the player must create it from the generated deployment defaults and continue startup.
- If the file exists and is valid, the player must load `resolutionWidth` and `resolutionHeight` from it.
- If the file exists but contains malformed JSON, missing values, or non-positive values, the player must recreate it from the generated deployment defaults and continue startup.
- The startup log must clearly report when the player repairs or seeds `profile.json`.

### Design Requirements

- The runtime profile model must have a generic name and shape, not a Windows-specific one.
- The generated manifest must remain the deployment default source, not be replaced by hardcoded values.
- The JSON load-and-repair logic must be isolated behind a small service boundary so other platforms can adopt the same pattern later with different storage locations.
- The current implementation only needs width and height, but the contract must be easy to extend later with graphics options.

### Non-Goals

- No in-game settings UI.
- No runtime application of changed settings while the player is already open.
- No `%AppData%` or per-user roaming storage yet.
- No cross-platform shared runtime module in this pass.

## Approaches Considered

### 1. Windows-Only Ad Hoc File Logic in `win32_application.cpp`

Write `profile.json` directly inside the application bootstrap and parse it inline before creating the main window.

This is the fastest change, but it would entangle file I/O, JSON validation, repair behavior, and window bootstrap in one file. It also creates a bad precedent for other platforms because the first reusable concept would already be buried inside Windows-specific startup code.

### 2. Generic Runtime Profile Contract with a Windows File-Backed Loader

Define a small generic runtime profile model plus a loader service in the Windows native layer. The loader reads deployment defaults from the generated manifest, resolves `profile.json`, repairs or seeds it when needed, and returns one resolved profile object to the application bootstrap.

This keeps the current Windows implementation small while preserving a clear seam for future PSP, PS2, or other native runtimes.

### 3. Full Future Graphics Settings System Now

Add a larger persisted settings model and start handling more graphics options immediately.

This is over-scoped for the current requirement and would force decisions about runtime graphics application that are not needed yet.

## Decision

Use approach 2.

The player will gain a small generic runtime player profile contract and a Windows file-backed loader that resolves `profile.json` beside the executable. The generated runtime player-settings manifest stays as the deployment default seed.

## Architecture

### Generated Defaults Layer

The builder continues to generate `runtime_player_settings_manifest.hpp/.cpp` exactly as the deployment default source.

That manifest continues to provide:

- default window width
- default window height

No user persistence is added to the builder itself beyond continuing to emit the deployment defaults.

### Runtime Profile Model

Add a small native runtime model with a generic name, for example:

- `RuntimePlayerProfile`

Its first members are:

- `ResolutionWidth`
- `ResolutionHeight`

The type name must stay generic so future platforms can reuse the same concept without inheriting Windows-specific naming.

### Runtime Profile Loader

Add a native service with a generic profile-loading purpose, for example:

- `RuntimePlayerProfileLoader`

Responsibilities:

- resolve the executable directory
- compute the absolute path to `profile.json`
- read generated deployment defaults from the generated manifest
- create `profile.json` from defaults when absent
- load and validate existing JSON when present
- repair invalid JSON by rewriting the file from generated defaults
- return the final resolved `RuntimePlayerProfile`
- emit clear startup log messages for:
  - seeded profile creation
  - repaired profile recreation
  - successfully loaded profile values

This service owns file I/O and JSON validation so `win32_application.cpp` only consumes a resolved profile.

### Win32 Bootstrap Integration

`win32_application.cpp` should stop pulling width and height directly from the generated manifest functions for final window sizing.

Instead it should:

1. construct or invoke the runtime profile loader
2. get the resolved runtime profile
3. use `ResolutionWidth` and `ResolutionHeight` when creating the main window
4. log the chosen values

The generated manifest still matters because it seeds the runtime profile loader defaults, but bootstrap no longer owns profile repair logic.

## File Format

`profile.json` lives beside `helengine_windows.exe`.

Initial JSON shape:

```json
{
  "resolutionWidth": 640,
  "resolutionHeight": 480
}
```

This stays intentionally minimal. The loader and profile naming should assume future extensibility, but the file should not include speculative unused fields in this pass.

## Validation and Repair Rules

### Missing File

If `profile.json` is missing:

- build one profile from generated defaults
- write `profile.json`
- continue startup using the generated defaults
- log that the profile was seeded from deployment defaults

### Malformed JSON

If JSON parsing fails:

- treat the file as invalid
- rebuild the profile from generated defaults
- overwrite `profile.json`
- continue startup using the repaired file
- log that the profile was malformed and was recreated from deployment defaults

### Missing or Invalid Values

If either value is absent or non-positive:

- treat the file as invalid
- rebuild the profile from generated defaults
- overwrite `profile.json`
- continue startup using the repaired file
- log that the profile values were invalid and were recreated from deployment defaults

### No Silent Invented Values

Repair always comes from generated deployment defaults, not from ad hoc fallback literals inside the loader.

That keeps one authoritative default source for the deployment.

## Data Flow

### First Launch

1. Builder emits generated runtime player-settings manifest.
2. Player starts.
3. Runtime profile loader reads generated defaults.
4. Loader does not find `profile.json`.
5. Loader writes `profile.json` with generated defaults.
6. Loader returns the resolved runtime profile.
7. Win32 application sizes the main window from that profile.

### Later Launch with Valid Profile

1. Player starts.
2. Loader reads generated defaults.
3. Loader finds `profile.json`.
4. Loader parses and validates it.
5. Loader returns the persisted values.
6. Win32 application sizes the main window from those values.

### Later Launch with Broken Profile

1. Player starts.
2. Loader reads generated defaults.
3. Loader finds `profile.json`.
4. Loader fails to parse or validate it.
5. Loader overwrites the file with generated defaults.
6. Loader returns the repaired values.
7. Win32 application sizes the main window from those values.

## Error Handling

The repair path is specifically for invalid file content.

If the player cannot write beside the executable because the directory is unwritable, that is an environment failure rather than invalid settings content. That failure should remain explicit and should not be silently ignored or rerouted to another location in this pass.

This keeps the first implementation honest and avoids hidden platform-specific storage behavior.

## Testing

### Builder Tests

Keep the existing manifest-writer coverage that proves generated deployment defaults are emitted correctly.

### New Loader Tests

Add tests for:

- creating `profile.json` from generated defaults when absent
- loading valid persisted width and height from `profile.json`
- recreating the file when JSON is malformed
- recreating the file when width is missing
- recreating the file when height is missing
- recreating the file when width or height is non-positive

### Integration Coverage

Add coverage for the bootstrap-facing behavior where practical:

- the resolved profile returned to startup uses the persisted values when valid
- the resolved profile returned to startup uses generated defaults after repair

If direct native integration tests are limited, keep the bootstrap wrapper thin and test the loader logic directly.

## Implementation Notes

- Prefer a tiny JSON contract and a small parser/writer over introducing a broad configuration system.
- Keep storage-path resolution in one place so later platforms can swap the location policy without rewriting bootstrap.
- Keep the loader result model generic enough to extend later with graphics options like fullscreen mode or quality levels.

## Success Criteria

The feature is complete when:

- the first Windows player launch writes `profile.json` beside the executable
- later launches honor edited `resolutionWidth` and `resolutionHeight`
- invalid `profile.json` content is repaired automatically from deployment defaults
- the startup log explains when the file was seeded or repaired
- generated deployment defaults remain the only authoritative default source
- the design leaves a clean seam for future non-Windows platform profile loading
