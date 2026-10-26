# DirectX 11 runtime shader loading

Runtime HLSL compilation belongs to the Windows engine backend. A renderer component submits an owned vector of `DirectX11ShaderRequest` to `DirectX11ShaderCompilation`. Each request supplies complete source, diagnostic name, entry point, shader-model profile, compiler flags and owned macro definitions. No engine core or Gevo dependency is involved.

Construction submits independent Windows thread-pool work and returns without running D3DCompile on the calling thread. `TryGetBytecodes` returns false while loading without altering the destination; on success it publishes the complete batch in request order. Independent batches progress independently. A retained compilation failure is rethrown with source, entry, profile and compiler diagnostics on every later poll. There is no synchronous fallback.

Workers retain only CPU request/result state. Removing the owning handle revokes publication immediately without joining compiler calls. Outstanding workers release their shared state after completion; they never dereference a camera, renderer, device or immediate context.

The render-thread pipeline owner polls the ticket, creates its Direct3D objects after bytecode becomes available, and retains device-creation failures. Loading skips dependent draws. Normal camera clears and independent ready 2D/3D cameras continue. Composition blend states do not depend on 2D shader readiness. A texture preview leaves both its last cached image and caller bindings untouched while its shaders load.

The common service is used by the built-in mesh/diagnostic/shadow pipeline, quad/font/rounded-rectangle pipeline, camera toon/effect pipeline and external-texture preview pipeline. Packaged material shaders are already cooked bytecode; their existing device-object creation path does not invoke HLSL compilation and remains unchanged. Runtime source includes must be resolved before submitting requests, as in the previous embedded-source paths.

Offscreen verification targets (no HWNDs or desktop capture): `helengine_windows_shader_tests`, `helengine_windows_preview_tests`, and `helengine_windows_toon_tests` (also `--shader-startup`).
