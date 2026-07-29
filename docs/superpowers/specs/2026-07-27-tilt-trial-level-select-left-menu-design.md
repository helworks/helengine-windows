# Tilt Trial Level-Select Left Menu Layout

## Goal

Remove the `Tilt Trial` title from the left side of the Tilt Trial level selector and let the left level menu use the recovered vertical space.

## Layout behavior

- Remove the authored left-side `Tilt Trial` title entity and its serialized text.
- Preserve the left menu's existing bottom margin.
- Move that spacing to the top of the left menu after the title is removed.
- Expand the left menu panel upward into the title area while preserving its existing horizontal bounds.
- Leave the right detail panel, list-row behavior, and selector action hierarchy unchanged.

## Implementation boundary

The change will be made in the existing Tilt Trial scene-generation source so regenerated scene output remains authoritative. Runtime selector logic will not be changed unless the generated hierarchy requires a corresponding index update.

## Validation

- Update the focused layout source tests to assert the title is absent and the left panel uses the revised geometry.
- Regenerate the authored selector scene with the existing generation command.
- Run the smallest relevant layout test filter.
