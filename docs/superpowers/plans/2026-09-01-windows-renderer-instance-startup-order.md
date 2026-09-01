# Windows Renderer Instance Startup Order Plan

> **Worker:** Implement with `superpowers:systematic-debugging` and `superpowers:test-driven-development`; use `gpt-5.6-luna` at `xhigh`.

**Goal:** Keep the packaged DemoDisc DX11 player alive through first-frame scene loading by giving the 3D bridge a guaranteed active 2D renderer instance before material defaults are applied.

**Reproduction:** Fresh release artifact `a8c2e338-4e6f-428d-adcc-b4ab06b55746` builds successfully, then faults during the first `Core::Draw`. A symbolized Debug reproduction reports `Value cannot be null. Parameter name: renderManager2D` from `StandardMaterialTextureBindingDefaults::Apply`, called by `Win32RenderManager3D::BuildMaterialFromRaw`. The current bridge derives the pointer from `OwnerCore`, which is null at this material-load point.

**Architecture:** `Win32Application` already owns both render bridges. Construct `Win32RenderManager2D` first, then construct `Win32RenderManager3D` with a non-null reference/pointer to that exact instance. Store it as a non-owning lifetime-safe bridge dependency and use it for material defaults and pixel fallback. Do not use global `Core`, do not allocate another renderer, and do not touch Vulkan.

## Task 1: Pin the startup-order contract red

- [ ] Extend `builder.tests/Win32RenderBridgeSourceTests.cs` to require constructor injection/storage of the active 2D renderer and to reject `OwnerCore->get_RenderManager2D()` in material/fallback paths.
- [ ] Extend the application source contract to require 2D construction before 3D and passing the same instance into the 3D constructor.
- [ ] Run the focused tests red before implementation.

## Task 2: Inject the active renderer instance

- [ ] Change `Win32RenderManager3D` construction to accept a non-null `Win32RenderManager2D` dependency and store it non-owningly for the application's shared lifetime.
- [ ] Construct `EngineRenderManager2D` before `EngineRenderManager3D` in `Win32Application::InitializeEngineCore`, passing the exact 2D instance to 3D.
- [ ] Use the stored instance in `BuildMaterialFromRaw` and `BindMaterialTextures`; preserve renderer-owned `PixelTexture` semantics and current slot clearing.
- [ ] Keep `UpdateTextureRegionCore` unchanged and keep DirectX 11 selected.

## Task 3: Verify at source, native, and runtime levels

- [ ] Run focused Windows bridge/application tests and `rtk git diff --check`.
- [ ] Rebuild the retained symbolized native target and require the startup scene to survive first draw for at least 15 seconds with no fatal exception.
- [ ] Rebuild the release native target/package and run the automated menu path to Software Path Tracer; require the process to remain responsive, load the software scene, accept Return, and close cleanly.
- [ ] Confirm no Vulkan initialization and no `codegen.exe` crash event.

## Task 4: Commit narrowly

- [ ] Commit only Windows application/bridge source and focused tests. Do not commit diagnostic binaries, PDBs, logs, generated caches, or DemoDisc output.
