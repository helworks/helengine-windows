# Text Component Outline and Shadow Design

## Goal

Add crisp, configurable outline and shadow rendering to `TextComponent` while preserving the existing text appearance and behavior when the effects are disabled.

## Approved behavior

`TextComponent` exposes four authored properties:

- `OutlineScale`: non-negative pixel-space `float`. Zero disables the outline.
- `OutlineColor`: `byte4` color used for outline glyphs. It defaults to transparent black.
- `ShadowOffset`: pixel-space `float2` offset. A zero vector disables the shadow.
- `ShadowColor`: `byte4` color used for the shadow glyph. It defaults to transparent black.

The outline is rendered as four crisp cardinal copies of each glyph at offsets `(-scale, 0)`, `(scale, 0)`, `(0, -scale)`, and `(0, scale)`. The shadow is one crisp copy at the authored offset. The render order is shadow first, outline second, and the normal text last.

All effect copies use the same font atlas, glyph metrics, wrapping, line alignment, font scale, rotation, and parent transform as the main text. The effect offsets are applied in the text drawable's pixel space before the parent transform. No blur, softening, diagonal outline samples, or automatic effect expansion of the authored layout box is introduced.

## Architecture

The shared `ITextDrawable2D` interface and `TextComponent` carry the four new values so all 2D backends consume the same contract. Property setters validate `OutlineScale`, detect unchanged values, and increment `TextRenderStateVersion` when a render-relevant value changes.

DirectX 11 and Vulkan retain their current per-glyph CPU submission paths. Each backend resolves the text layout once, then submits the optional shadow pass, optional four-copy outline pass, and normal pass through the existing sprite/quad draw operation. With all effects disabled, the current single-pass behavior remains unchanged.

The existing scene persistence path must serialize and deserialize the new public properties using the engine's established component field mechanism. Older scenes must load with the defaults above. Editor exact-2D preview must copy the values when it mirrors a `TextComponent`, so authored effects are visible in the editor and runtime.

## Testing

Add focused tests that:

1. Verify the constructor defaults disable both effects and use transparent effect colors.
2. Verify changing each effect property increments `TextRenderStateVersion`, while assigning the same value does not.
3. Verify negative outline scale is rejected.
4. Verify the preview/render-state mirror preserves the four effect properties.
5. Verify backend draw submission order and positions for a one-glyph text drawable with shadow and outline enabled.
6. Verify a default text drawable still submits only the normal glyph draw.

Run the focused engine tests first, then the relevant engine solution/project build. Finally rebuild the DemoDisc Windows target to package the updated runtime.

## Scope boundaries

This change does not add editor-specific controls beyond exposing the persisted `TextComponent` properties through the existing component property system. It does not introduce shader changes, texture baking, blur, automatic layout padding, or platform-specific effect semantics.
