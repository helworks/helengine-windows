# Windows Platform Notes

## Builder Output

The Windows builder assembly lives under `builder/` and is loaded dynamically by the editor through `user_settings/platforms.json`.

The builder exposes the Windows platform metadata that the editor needs to render build profiles, graphics profiles, and platform-specific options without hardcoding Windows-specific knowledge in the editor process.

## Generated Core Contract

This repository does not own generated engine C++ source.

Platform builds are expected to consume generated output from an external deployment root, using the generated source path passed into the native build configuration.
