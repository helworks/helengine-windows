# Shared screen-space diffuse GI

The Windows renderer owns one screen-space capture for two explicitly registered,
synchronized 3D cameras. Their presentation targets remain separate. Register with
`Win32RenderManager3D::ConfigureSceneSsgi(Core* owner, DirectionalLightComponent* sun,
ICamera* firstCamera, ICamera* secondCamera, bool indirectOnly)` after both cameras
have their ordinary toon/effect registrations. Call `ClearSceneSsgi()` before any
borrowed camera, sun or owning scene is destroyed. No portable-core API changes or
additional entities/lights are involved.

The capture visits `ObjectManager.Drawables3D` once per scene elapsed-update value,
using the union of the two cameras' layer masks. It does not rely on either output's
render queue. Camera pose/projection must match. Physical viewport extents resolve
through each camera's named output layout; transient resize disagreement makes the
shared pass not ready until the layouts synchronize, without presenting stale GI.

Source attachments contain nearest opaque geometry's full-precision world position
and normal, plus HDR diffuse outgoing radiance from authored base color, diffuse
texture and the existing directional sun. The source omits ambient light, effects,
reflections and indirect feedback. Material alpha below 0.999 is outside this first
opaque diffuse adapter. Source lighting follows the requested unshadowed direct-light
policy; registration does not enable shadow maps or alter per-camera shadow settings.

The source is half resolution, capped to 960x540 with the original projection aspect.
Eight cosine-weighted hemisphere rays per source pixel traverse at most 24 crossed
screen cells each. Homogeneous clipping and perspective-correct world intervals avoid
skipping an opaque pixel between fixed world-space march endpoints. The first crossed
surface ends a ray even if black or backfacing. Maximum world reach is 3.84 units;
the 24-cell screen budget can end a ray earlier. Missing/offscreen surfaces and budget
exhaustion contribute zero with the fixed estimator denominator. There is no offscreen
transport, temporal reconstruction, ambient fallback or voxel fallback.

Irradiance is written to an independent HDR attachment. Each output's actual 3D receiver
samples the shared result with bilinear spatial filtering, normal agreement, world
plane-distance and position-footprint rejection. A different captured foreground
surface cannot serve as a background receiver. Surfaces hidden in the shared capture
receive zero; this is a limitation of a single-layer screen-space representation.
Normal presentation adds diffuse indirect irradiance to ordinary scene shading.

`indirectOnly=true` displays raw indirect irradiance luminance in equal RGB channels
on 3D geometry. Receiver albedo, direct light, ambient light, pearl/toon color,
reflections, outlines, bloom, AO and tone mapping do not enter this diagnostic display.
Opaque diagnostic clear pixels are neutral black; transparent output clear alpha is
preserved. Shell 2D remains usable. The application exposes this with
`HELENGINE_SSGI_INDIRECT_ONLY=1`; ordinary launches use normal scene presentation.

SSGI registration clears any configured optional voxel solver and explicitly unbinds
its b9/t17 field during source and receiver draws. Pixel-shader b10 and t18-t21 belong
to the shared pass and clear before unrelated cameras, UI or teardown. Explicitly
configuring voxel GI later clears SSGI, so both transports cannot accumulate together.
The voxel module remains optional and is inactive for Gevo's SSGI builds.

The `helengine_windows_ssgi_tests` target requires no generated core or voxel module.
It runs owned offscreen WARP resources, compiling the actual source/trace/receiver
HLSL and all nine production toon variants. Fixtures cover diffuse sun/albedo source
energy, real captured nearest-depth geometry and cross-geometry bounce, identical
shared resource identity across output bindings, half-resolution/cap allocation,
colored bounce, screen misses, backfacing sources, black source/no feedback, in-frustum
receiver plane/normal/position rejection, grayscale, and thin black blockers under
orthographic and translated perspective projections. No desktop/window capture is used.
The bounded budget is not a measured guarantee of production frame rate.
