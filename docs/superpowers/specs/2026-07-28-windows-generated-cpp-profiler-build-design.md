# Windows Generated-C++ Profiler Build Design

## Purpose

Add a Windows-only `profiler` player build profile that measures the generated native C++ implementation of Helengine and project code. The profiler exists to identify algorithmic bottlenecks, excessive call counts, allocation pressure, synchronization costs, and expensive shared-core call chains that affect low-performance console targets where direct profiling is difficult.

The Profiler build is an analysis binary. Absolute Windows timings are not intended to predict PlayStation 2, Nintendo DS, Nintendo 3DS, Wii, or GameCube performance. Its primary signals are relative cost, inclusive and exclusive time, call frequency, workload size, allocation behavior, and call hierarchy. Optimizations discovered through these signals must still be validated on target hardware.

## Selected Profiler

[Tracy](https://github.com/wolfpld/tracy) is the selected native profiler. It provides a game-oriented desktop viewer, native C++ instrumentation, statistical sampling, thread visualization, allocation and lock tracking, named plots, and Direct3D 11 GPU profiling.

The Profiler build uses generated Tracy zones as its primary measurement mechanism and Tracy sampling as a complementary signal. MSVC `/Gh` and `/GH` compiler hooks are not used as the primary mechanism because they indiscriminately instrument standard-library and platform code, require fragile x64 hook handling, and discard source metadata already available to the C#-to-C++ generator. Sampling-only profiling remains useful as a secondary check but does not provide exact invocation counts for short generated methods.

## Build Profiles

Windows exposes three player build profiles:

- `debug`: existing debug runtime and diagnostics behavior.
- `release`: existing shipping build with no Tracy dependency or profiling payload.
- `profiler`: optimized native player with PDB symbols, Tracy integration, and generated instrumentation.

The C# editor application may still be published using its Release configuration. The `profiler` identifier describes the generated player build and must flow independently through the editor build-profile selection, CLI, platform build request, Windows builder, native executor, CMake configuration, and C++ generator.

The native Profiler configuration uses Release-style optimization and debug information, equivalent to CMake `RelWithDebInfo`, with a dedicated `HELENGINE_WINDOWS_PROFILER` definition. Tracy's client implementation is linked only for this profile. The native executor must stop hardcoding Release and select its CMake configuration from the resolved player build profile.

## Native Build Flow

The build flow is:

```text
C# engine and project code
        |
        v
C++ generation with original-source profiling metadata
        |
        v
Generated shared-core and project C++
        |
        v
MSVC optimized native build with PDB and Tracy client
        |
        v
helengine_windows.exe connected to the Tracy desktop viewer
```

The generated shared core currently enters the Windows build through its generated unity or amalgamated translation unit. Instrumentation emitted into generated function bodies therefore measures the native code included in the final player executable rather than the C# editor execution.

## Generated Function Instrumentation

When the `profiler` profile is active, the C#-to-C++ generator emits one static RAII Tracy zone at the beginning of every generated function body. The generator must implement this behavior directly; generated files must never be rewritten after generation.

Instrumentation includes:

- shared engine methods;
- physics methods;
- project gameplay methods;
- constructors;
- property accessors;
- operators;
- generated runtime helpers;
- generated code-module functions.

Initial coverage includes every emitted function with a body. Noise is handled through Tracy filtering and subsystem categorization rather than by prematurely omitting small methods. Explicit exclusions are limited to:

- abstract or interface declarations with no body;
- compile-time-only declarations;
- external libraries and the C++ standard library;
- Tracy's implementation;
- native functions explicitly designated unsafe for profiler re-entry.

Each generated source-location record contains static data for:

- original C# namespace;
- original C# type and method;
- generated C++ symbol;
- original source file and line when available;
- subsystem category;
- generated C++ file and line as secondary information.

Zone entry must not allocate memory, format strings, or resolve symbols. Static source-location records allow Tracy to aggregate call counts and timings without per-call metadata construction. RAII scope lifetime guarantees correct closure for normal returns, early returns, and exceptions.

## Instrumentation Categories

Generated zones are assigned stable categories so captures can be filtered without losing exhaustive coverage:

- core;
- physics;
- scene;
- rendering;
- content;
- input;
- audio;
- diagnostics;
- project game code;
- uncategorized generated code.

Category resolution belongs to the generator and uses the maintained source type, namespace, assembly, and known subsystem metadata. Unknown functions remain instrumented under the uncategorized category.

## Frame and Platform Boundaries

Handwritten Windows code receives a small set of explicit zones and frame markers. Native platform code is not exhaustively instrumented because the primary objective is shared generated code, and Tracy sampling can expose time in platform libraries and external modules.

Required top-level markers are:

- complete frame;
- input collection;
- variable update;
- fixed-step scheduler;
- each individual fixed physics step;
- scene-operation commit;
- render extraction;
- draw-command construction;
- Direct3D submission;
- present and wait.

Individual fixed-step markers are required so a slow frame can be distinguished from a frame that performed several physics catch-up steps.

Tracy sampling covers the complete native process and provides a cross-check for instrumentation distortion. It also attributes sampled time inside Windows, graphics drivers, the standard library, and other external code that does not contain generated zones.

## Runtime Metrics

The Profiler player publishes named Tracy plots for:

- total frame time;
- update time;
- physics time;
- render time;
- fixed steps per frame;
- active rigid bodies;
- candidate and confirmed contact pairs;
- constraints solved;
- allocated and live bytes;
- draw calls;
- rendered triangles.

Capture metadata includes:

- project name and version;
- engine version;
- Git revision when available;
- scene identifier;
- player build timestamp;
- compiler and optimization configuration;
- enabled runtime features;
- generated-code profile;
- instrumented and excluded function counts.

Allocation tracking attaches to engine-owned generated and runtime allocation paths and records size, lifetime, thread, and active Tracy zone. Lock tracking attaches to engine-owned synchronization primitives. Direct3D 11 GPU zones distinguish CPU-side preparation and submission from GPU execution and present stalls.

## Developer Workflow

The Profiler package contains:

```text
helengine_windows.exe
helengine_windows.pdb
helengine_profile_manifest.json
```

The profile manifest records build identity, native configuration, source mapping coverage, instrumented function count, and all explicit exclusions.

A normal profiling session is:

1. Select `Profiler` in the editor or pass `--build-profile profiler` to the editor build CLI.
2. Build the Windows player.
3. Open the Tracy desktop viewer and begin listening.
4. Launch the generated player.
5. Reproduce the selected workload.
6. Stop after a controlled number of frames.
7. Save the `.tracy` capture for comparison.

Tracy uses on-demand capture. The player remains functional when no viewer is connected and continues running if the viewer disconnects. To capture startup, Tracy must listen before the player launches. The Profiler player identifies its configuration in its window title and startup log.

## Failure Behavior

Profiler-specific failures are explicit:

- missing Tracy source or build dependency fails native configuration;
- missing PDB or profile manifest fails Profiler packaging;
- an unsupported compiler/profile combination rejects the build request;
- invalid generated source metadata fails C++ generation;
- the absence of a Tracy viewer is not an error;
- a viewer disconnect does not terminate or pause the player.

The implementation must not silently fall back to an uninstrumented binary when the `profiler` profile was requested.

## Release Isolation

Debug and Release behavior and package contents remain unchanged. A Release player must contain no:

- Tracy client linkage;
- generated profiling scopes;
- profiling source-location table;
- profiler manifest;
- profiler compile definition;
- runtime dependency on the Tracy viewer.

Release isolation is a tested build contract rather than an assumption based on disabled runtime behavior.

## Repeatable Benchmark Workloads

Profiling is supported by deterministic benchmark scenes with fixed warmup and capture periods:

- empty engine frame;
- four static cubes;
- four dynamic cubes;
- increasing rigid-body counts;
- increasing contact-pair counts;
- increasing constraint counts;
- scene-update stress with minimal rendering;
- rendering stress with physics disabled.

The benchmark workload and runtime metrics are stored alongside each saved capture. This enables before-and-after comparison of algorithmic changes even though Windows does not reproduce target-console CPU architecture, cache behavior, memory bandwidth, graphics hardware, or compiler output.

## Validation

Validation covers four layers:

1. Platform metadata tests verify Windows exposes `debug`, `release`, and `profiler`.
2. Generator tests verify profiling scopes and original-source metadata are emitted only for Profiler builds.
3. Native build tests verify optimization, PDB generation, Tracy linkage, compile definitions, package contents, and Release isolation.
4. An end-to-end smoke test builds a small scene, connects Tracy, executes controlled frames, and confirms frame markers and generated method zones are captured.

Multithreaded tests verify independent nested zone stacks per thread. Control-flow tests verify scope closure for normal returns, early returns, and exceptions. Manifest tests verify instrumented and excluded function counts match generator output.

## Success Criteria

For one captured generated physics frame, the Tracy viewer must answer:

- which generated functions ran;
- how many times each function ran;
- each function's inclusive and exclusive cost;
- the parent call chain that produced the work;
- the number of physics steps, bodies, contacts, constraints, and allocations involved;
- the original maintained C# source location for each generated function;
- sampled time spent outside instrumented generated code.

The design is successful when these answers can be compared across repeatable captures and Release builds remain completely free of profiler integration.

## Non-Goals

- Predicting exact console frame rates from Windows timings.
- Profiling C# editor execution.
- Shipping Tracy in Release packages.
- Exhaustively instrumenting the Windows API, graphics driver, standard library, or third-party libraries.
- Replacing target-hardware validation.
- Building a custom profiler viewer or capture file format.
- Adding profiler support to non-Windows platform builders in this first implementation.
