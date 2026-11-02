# Dynamic camera GTAO

The Windows toon camera uses a current-frame GTAO approximation based on the
analytic slice integral in Jimenez et al., *Practical Real-Time Strategies for
Accurate Indirect Occlusion*, and the bounded horizon falloff discussed by Intel
XeGTAO. The SM5 implementation is independent and does not import XeGTAO source.

References:
- https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf
- https://github.com/GameTechDev/XeGTAO

## Radiance ownership

Geometry attachment 6 (the seventh MRT) stores premultiplied diffuse indirect
radiance: ambient, shared SSGI/optional voxel irradiance, or InkAmbient. Direct
lights, their authored ink shadow-color fill, specular reflection and emission
remain in scene color. Final composition subtracts only the occluded indirect
term before the existing SDR/HDR display transform. Metals suppress diffuse
indirect with the same metallic weight used for scene color.

Transparent materials blend the indirect MRT with premultiplied transmittance,
while keeping opaque position, normal, coverage and part identity unchanged.
Clear glass therefore retains the opaque occluder while transmitting its shaded
indirect contribution. This is a single opaque-layer screen-space approximation;
a transparent surface does not contribute a separate occluding layer.

## Current-frame passes

1. Resolve opaque position/normal and diffuse indirect consistently with 1x/4x
   scene coverage. Recover covered positions from their accumulated sample weight.
2. Evaluate three rotated slice pairs, four quadratic-distance steps per side
   (at most 24 position reads). Track the maximum horizon on each side and
   analytically integrate the cosine-weighted hemisphere arc. The world radius,
   bias and maximum pixel radius bound sampling; missing/offscreen samples do not
   occlude. A stable spatial rotation pattern uses no frame index or history.
3. Filter visibility with a 3x3 spatial kernel. Symmetric point-to-plane distance,
   normal agreement and sample coverage reject incompatible surfaces. Both AO
   targets are full-resolution R8_UNORM textures.
4. Apply strength to the filtered visibility only when composing diffuse indirect.
   Raw reflection-probe captures skip the AO passes and disable AO composition.

The existing camera scratch pool owns the indirect MRT and both visibility
textures; sequential compatible cameras reuse them. The extra storage at 1080p is
about 19.8 MiB at 1x and 83.1 MiB at 4x (including the resolved indirect texture at 4x),
per live incompatible scratch set. The implementation reserves only SRV 26/27;
shared GI/SSGI/lightmap/probe slots 17–25 are preserved. All touched slots are
unbound before becoming render targets and restored after composition.

## Settings and limits

Gevo starts at strength 0.65, radius 0.22 scene units, bias 0.008 scene units and a
24-pixel sampling cap. These are artistic defaults, not a calibrated physical
visibility measure. No temporal accumulation or TAA is required. Finite screen
coverage misses offscreen or hidden occluders, and the pixel-radius cap truncates
very close geometry. The spatial-only pattern can shimmer during camera motion;
this implementation does not claim the full XeGTAO quality/performance pipeline.

## Verification

Build the actual checkout into the fixed Windows test output:

```powershell
cmake -S C:\dev\helworks\helengine-windows -B C:\temp\windows -G "Visual Studio 17 2022" -A x64 -DHELENGINE_WINDOWS_INCLUDE_GENERATED_CORE=OFF
cmake --build C:\temp\windows --config Release --target helengine_windows_toon_tests -j 8
C:\temp\windows\Release\helengine_windows_toon_tests.exe --gtao
C:\temp\windows\Release\helengine_windows_toon_tests.exe
```

The fixtures create owned offscreen GPU textures only. They assert coplanar and
separated controls, contact occlusion, translated-camera equivalence, immediate
movement/removal/return, viewport borders, FP16 direct/emissive preservation,
authored alpha, transparent indirect transmittance/opaque ownership, SRV 17–27
restoration, and both supported sample counts. The original regression failed
with `AO attenuated direct sunlight.` before the radiance split.

Synthetic 1080p timestamp measurements report the complete camera pass with AO
off/on on the available GPU. They are not measurements of GTX 970/1070 or whole-app
frame rate; the target 1080p60 budget still requires measurement on those GPUs.
