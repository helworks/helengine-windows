## Summary

Improve the Windows diagnostics log so repeated generated `256x256` texture uploads can be identified as font atlas builds instead of anonymous runtime textures. This is a diagnostics-only change. It does not alter asset caching, unloading, disposal, or scene behavior.

## Problem

The current diagnostics log records repeated generated textures with synthetic ids such as `__generated_runtime_texture_17` and basic dimensions. That is enough to show that repeated uploads are happening, but not enough to answer which font file caused them or whether they were produced during font deserialization versus some other generated-texture path.

For the `DemoDiscMainMenu -> cube_test -> DemoDiscMainMenu` flow, this leaves the core question unresolved: whether the repeated `256x256` uploads are multiple atlas builds for the same font, different fonts, or some unrelated generated texture path.

## Goals

- Label generated runtime texture uploads with their likely source.
- Surface the current text-font resolution context in scene checkpoints.
- Keep the output machine-readable so repeated builds can be grouped by font path.
- Preserve the current runtime behavior exactly.

## Non-Goals

- Do not change scene unload behavior.
- Do not add cache eviction or disposal fixes yet.
- Do not suppress valid second-load behavior for the menu scene.

## Design

### Texture Build Provenance

When the Windows render bridge uploads a runtime texture, the diagnostics layer will continue to emit one `asset.build` entry. The detail payload will be enriched as follows:

- Authored textures keep their dimensions, runtime asset id, and gain `source=authored`.
- Generated textures gain `source=generated`.
- If the generated-core font serializer reports `BuildRuntimeTexture`, the upload is labeled `generated_kind=font_atlas`.
- Font atlas uploads also include:
  - `font_deserialize_stage`
  - `text_font_relative_path`
  - `text_font_load_stage`

This makes repeated atlas uploads attributable to the font currently being resolved by the generated core.

### Scene Checkpoint Context

Each scene checkpoint will include the latest generated-core text/font trace fields:

- `text_font_relative_path`
- `text_font_load_stage`
- `font_deserialize_stage`

This allows the log to answer both:

- which font was most recently being resolved during a scene transition
- whether the latest generated texture uploads were happening inside font deserialization

### Fallback Behavior

If the generated-core trace headers are not present in a particular build, the Windows host will keep logging without those fields. Diagnostics must remain additive and compile-time optional.

## Expected Output

For the menu round-trip flow, the log should turn entries like:

- generated `256x256` texture with synthetic id

into entries that effectively read as:

- generated font atlas `256x256`
- sourced from `Fonts/DemoDiscBody.hefont` or `Fonts/DemoDiscTitle.hefont`
- created during `BuildRuntimeTexture`

That is enough to verify whether the menu is rebuilding the same font atlas many times during one load.

## Verification

- Build City through the editor Windows platform pipeline with the narrowed scene list used for diagnostics work.
- Run the player manually through `menu -> cube_test -> menu`.
- Confirm the diagnostics log now labels the repeated generated textures as font atlas uploads and records the relevant font path.
