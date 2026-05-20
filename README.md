# HelEngine Windows Host

This repository contains the native Windows host for HelEngine and the Windows platform builder assembly used by the editor.

## Builder Output

The Windows builder assembly lives under `builder/` and is loaded dynamically by the editor through `user_settings/platforms.json`.

The builder exposes the Windows platform metadata that the editor needs to render build profiles, graphics profiles, and platform-specific options without hardcoding Windows-specific knowledge in the editor process.

## Generated Core Contract

This repository does not own generated engine C++ source.

Platform builds are expected to consume generated output from an external deployment root, using the generated source path passed into the native build configuration.

## Windows Gamepad Backends

Windows player builds can embed any subset of the supported native gamepad backends:

- `embed-xinput`: links the XInput backend into the player build.
- `embed-directinput`: links the DirectInput 8 backend into the player build.
- `embed-steaminput`: links the SteamInput backend into the player build and requires a valid Steamworks SDK install.

When `embed-steaminput` is enabled, set `HELENGINE_STEAMWORKS_SDK_ROOT` to the Steamworks SDK `sdk` directory before the native build runs.
