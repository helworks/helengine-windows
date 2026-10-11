# Camera routing by host window

Date: 2026-10-11
Status: Implemented; 102 focused managed tests and native camera-routing acceptance passed.

## Contract

One process still owns one Core, one loaded scene, one simulation and one shared DirectX11 device. Cameras can now select which registered view receives their output. Existing entity and camera layer masks select the content inside that view.

CameraComponent and ICamera expose OutputWindowTag:

- Empty means every registered view, preserving shared-camera scenes.
- main selects the primary view.
- A named tag selects the corresponding secondary view, compared without case.
- A camera targeting an absent view is not drawn.
- Identifiers follow the host's existing 1..64 ASCII letters, digits, underscores and hyphens rule. Null is invalid in managed authoring.

Camera targets are serialized as ordinary component fields. The existing CameraRenderSettings binary payload is unchanged. Older camera records without the new field keep the constructor's explicit shared target.

RenderManager3D registers view tags and selects a RenderWindowHandle independently of InputWindowHandle. Duplicate tags, primary renames and unknown handles fail before changing the registry. Removing a secondary view releases its tag and restores any selected render/input handles to main. Disposing the renderer clears both registries.

The Windows host assigns each secondary's existing CLI tag during registration and selects the matching handle with each output surface. The native renderer applies the camera filter before drawing that view. A view without an assigned camera uses the existing opaque or transparent fallback clear.

## Authoring example

Assign separate layer masks to wallpaper and overlay entities, then configure their cameras:

~~~csharp
CameraComponent wallpaperCamera = new CameraComponent {
    LayerMask = 1,
    OutputWindowTag = "main"
};
CameraComponent overlayCamera = new CameraComponent {
    LayerMask = 2,
    OutputWindowTag = "glass"
};
~~~

Launch the scene with a desktop primary and a glass overlay:

~~~powershell
.\helengine_windows.exe --window-mode desktop --window glass,overlay,0,0,640,360
~~~

The primary desktop camera targets main. Automatically added monitor views retain their own desktop-monitor-N tags; this change does not introduce camera assignment by window role or monitor group.

## Input and scope

Pointer hit testing considers only cameras displayed by the active input view. Changing input views clears hover/capture with Leave rather than delivering a release activation to another view. Retargeting a captured camera away from the input view also cancels capture. Combo-box camera lookup applies the same view filter.

Camera-bound viewports now resolve their dimensions from their camera's output window. LayoutComponent anchors and ReferenceCanvasFitComponent follow the closest viewport or camera, including explicit camera bindings and camera retargeting. Resizing one output updates only layout bound to that output. Fixed viewports and render targets retain their own dimensions.

Shared cameras and screen-bound viewports intentionally retain the primary layout reference. Minimizing a view preserves its last positive layout dimensions. Closing a named view retains those dimensions for the scene; registering that tag again replaces them with the new view's usable size. Resolving layout for an unregistered tag throws instead of borrowing another window's dimensions.

The native generator emits interface event accessors that forward subscriptions to the concrete event storage. This keeps viewport bounds notifications consistent between C# and C++ when windows resize.

The loaded scene and simulation remain shared. Window lists in profiles, runtime view creation, global hotkeys and the Gevo application modules are subsequent work.

## Validation

Artifacts and test fixtures: C:\dev\helworks\builds\window-camera-routing.

- Core routing, camera ownership and existing multi-window registry tests: 19 passed.
- Pointer, camera persistence, hit resolver and native deserializer tests: 56 passed.
- Existing Windows alpha, overlay, mode configuration and launcher tests: 27 passed.
- Canonical native build uses two minimal scenes authored through the editor serializers.
- Native acceptance passed for routed normal/overlay and desktop/overlay back buffers; every view completed 30 successful presentations. Ten exact RGBA pixel probes matched the authored camera colors. The no-camera overlay was transparent across every pixel, and two shared-camera normal views matched with zero differing RGBA pixels.
- Acceptance ran on the available single 3840x2160, 96-DPI display. Real mixed-DPI multi-monitor hardware was not exercised.
- Captures export only renderer-owned back buffers.
- Source checkouts with unrelated pending changes are preserved; implementation lives in feature/window-camera-routing worktrees.

## Independent layout validation

Artifacts: C:\dev\helworks\builds\window-layout.

- Managed window registry and camera routing tests: 18 passed.
- Managed viewport, anchor, reference-canvas, input and camera persistence tests: 57 passed.
- Generator emission and native regression tests: 20 passed. This includes subscription/removal with direct and inherited publishers, independent contracts sharing an event name, and generic event arguments.
- The canonical native Windows build completed successfully.
- Three native initial-layout scenarios passed: normal plus 400x240 overlay, normal plus 800x600 overlay, and desktop plus 640x360 overlay.
- Live resizing to 1000x700 primary and 800x600 overlay, overlay minimize/restore, and secondary closure passed with zero presentation failures.
- Nine renderer-owned back buffers passed 18 exact RGBA probes, including controls anchored to the resized right/bottom edges.
- MSVC tests ran through the short validation-area junction because six long fixture paths exceeded its 260-character file limit. No generated sources were rewritten.
