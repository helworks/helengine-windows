# Windows Profiler Capture

The `profiler` Windows host connects the existing `IRuntimeCpuProfileSink` to Core initialization and the 2D command-list builder. Tracy includes per-frame duration plots named `Component.Update/<native type>/entity=<scene entity id>/instance=<native pointer>` and `Entity.Update/id=<scene entity id>/instance=<native pointer>`. Values are milliseconds. Entity plots sum the measured component updates owned by that actual entity; they do not include its descendants or attribute GPU/render work to that entity. Native pointers distinguish concurrently loaded entities that share an authored id.

`Core.Stage/*.ms` and `UI.Stage/*.ms` expose the existing coarse Core and command-building probes. `Core.Entities.Registered`, `Core.Updateables.Registered`, `Render.Drawables2D.Registered`, `Render.Drawables3D.Registered`, and `Render.Cameras.Registered` report current list sizes. Executed component/entity counts and invocation totals are separate plots, so registered objects are not confused with objects updated during the frame.

The shared sink clock has one-microsecond resolution. Zero is a quantized measurement, not proof that a callback has no cost. Individual instance durations are inclusive of work called by that callback. Do not add overlapping stage or draw zones. Plot publication occurs in `Profiler.EmitComponentSamples` after `Engine.Update` and `Engine.Draw`; the probes inside those operations still have measurement overhead. Metadata is bounded to 512 component instances and 512 owning entities per process, and exceeding the bound fails explicitly. This instrumentation is compiled only into the Tracy-enabled profile and does not alter the shared Core ABI.

For local investigations use the actual built package, a fixed capture path such as `C:\temp\windows-menu.tracy`, and bounded `--scene DemoDiscMainMenu --frames 2100 --idle-throttle off` player arguments. Do not create timestamped package copies when the workspace rules require fixed build outputs.

`capture-windows-profiler.ps1` runs a bounded local Tracy capture against a Windows package built with the `profiler` profile. It never writes into the source package: it first copies that package into a new capture-session directory, then runs the copied player there.

The helper requires the explicit path to Tracy Capture. It does not assume a Tracy GUI installation path. Build Tracy Capture separately (for example from the `capture` target in this repository's `third_party\tracy` submodule), then pass that executable with `-TracyCapturePath`.

The workload executable must be deterministic enough to repeat the desired player actions. Its arguments are passed without modification. `-WorkloadProjectPath` is validated and recorded in the report; it is not implicitly passed to the workload.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File C:\dev\helworks\helengine-windows\scripts\capture-windows-profiler.ps1 `
  -ProfilerPackagePath C:\dev\helprojs\demodisc\output\windows-profiler `
  -WindowsBuildManifestPath C:\dev\helprojs\demodisc\build-work\windows-build-manifest.json `
  -TracyCapturePath C:\tools\tracy\capture.exe `
  -WorkloadExecutablePath C:\dev\helprojs\demodisc\tools\run-profiler-workload.exe `
  -WorkloadProjectPath C:\dev\helprojs\demodisc\project.heproj `
  -WorkloadArguments '--scene', 'tilt-trial', '--iterations', '20' `
  -CaptureSeconds 45 `
  -OutputDirectory C:\dev\helprojs\demodisc\profiling
```

Each run creates a new `capture-<utc>-<id>` directory under `-OutputDirectory` containing:

- `capture.tracy`, the bounded Tracy capture.
- `capture-report.json`, which records the machine, workload command, timer values, package hashes, and project/build identity.
- `manifests\windows-build-manifest.json` and `manifests\generated_profiler_manifest.json`, copied snapshots that the report links to by relative path and SHA-256.
- `package`, the isolated copy of the supplied package that was actually launched.

The builder's `windows-build-manifest.json` is normally in the builder workspace rather than the final package output, so it is deliberately an explicit argument. The helper rejects a manifest that does not list both profiler-only artifacts (`generated-profiler-manifest` and `native-pdb`).

The capture duration is bounded by `-CaptureSeconds` (1–3600); Tracy Capture receives the same duration with `-s`. The workload has only that capture window plus startup/shutdown allowance, and the helper stops only the player, workload, or capture processes it launched.
