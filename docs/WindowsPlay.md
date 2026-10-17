# Windows editor Play

The Windows platform supports the existing desktop Play host through `helengine.windows.builder.WindowsPlayerAdapter`.
An assets-only build stages into `windows-staging` beneath the builder working root. It does not replace the packaged
Windows executable, write generated C++, or invoke CMake. Normal native builds retain their output path and behavior.

Play Settings exposes `render-width` and `render-height`, with defaults of 1280 and 720. Both are required integers from
1 to 8192. These settings are independent of the native build's `default-width` and `default-height`. Window Scale
magnifies that framebuffer; the player maps mouse positions back to its render pixels, including after a DPI change.
Keyboard, mouse and gamepad are declared. Runtime platform identity matches the builder/native startup manifest.

Textures and models use the shared runtime format without aliases or conversion. Fonts use the existing packaged-font
processor registered by the player. Windows materials use the shader-material serializer and translate their float4
base color, diffuse texture id, alpha blending and two-sided culling to `PlayerMaterialDescription`. Imported texture ids
resolve beneath `cooked/imported`, matching the native shader loader. Missing textures and invalid color buffers fail
explicitly. Non-seekable material streams are supported.

## Current shared-player limits

- Material colors are quantized to normalized 8-bit RGBA; HDR values are clamped.
- The shared player rebuilds the built-in standard material: custom shader programs, PBR parameters, extra texture maps,
  individual shadow flags and depth-state overrides are not reproduced by its current material-description contract.
- Front-face culling cannot be represented and is rejected explicitly.

## Verification

The Windows builder tests cover actual material cook/decode round trips, shared texture/model loading, invalid settings,
non-seekable input, and assets-only staging that preserves the existing executable and generated sources. The player
tests cover mouse coordinate conversion at 1x, magnified and non-square display sizes, including positions outside the
window. Source-inspection tests locate the real checkout using compiler-supplied source paths, so test binaries can live
outside the repository.

A headless smoke on 2026-10-16 used the current DemoDisc assets as a temporary fixture and actual source projects from
the main checkouts. Both scenes cooked through `EditorPlatformBuildGraphRunner.ExecuteAssetsOnly`, decoded through the
adapter/content processors, and finished loading in the runtime core with headless test renderers:

| Scene | Entities | Materials | Textures | Models | Fonts |
| --- | ---: | ---: | ---: | ---: | ---: |
| DemoDiscMainMenu | 150 | 1 | 2 | 0 | 3 |
| cube_test | 26 | 2 | 6 | 1 | 1 |

This verifies cooking, decoding and scene construction, not visual output or live user interaction. No GUI was opened.
The original DemoDisc project was not modified. Temporary fixtures, generated script modules, managed intermediates and
logs were confined to `C:\temp\helengine-windows` and removed after validation. Final binaries use the fixed output
folders `C:\dev\helworks\builds\windows\player` and `C:\dev\helworks\builds\windows\builder`.
