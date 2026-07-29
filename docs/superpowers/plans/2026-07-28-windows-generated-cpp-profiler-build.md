# Windows Generated-C++ Profiler Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Windows-only `profiler` player profile that produces optimized, symbolized native C++ binaries instrumented with Tracy, including exhaustive generated-function zones, frame/physics/render metrics, allocation and lock tracking, Direct3D 11 GPU timing, and strict Release isolation.

**Architecture:** The Windows build profile supplies a profiler-specific codegen default and flows unchanged through the editor build request. The C#-to-C++ generator emits static Tracy source locations directly into generated function bodies and writes a machine-readable instrumentation manifest. The Windows builder resolves the player profile into a native CMake configuration, conditionally links a pinned Tracy client, adds handwritten host/GPU markers, requires profiler artifacts, and writes the final package manifest. Release generation and native linking never see Tracy.

**Tech Stack:** C#/.NET, Roslyn, xUnit, PowerShell, CMake 3.26+, Ninja, MSVC x64, C++20, Direct3D 11, Tracy 0.13.1.

---

## Repositories and constraints

This feature spans three sibling repositories. Before editing each repository, read its `AGENTS.md` and preserve all unrelated dirty-worktree changes.

| Repository | Role |
| --- | --- |
| `C:\dev\helworks\helengine` | Shared build wrapper, build-profile/default propagation, generated core, runtime metrics |
| `C:\dev\helworks\csharpcodegen` | Source metadata, generated Tracy scopes, instrumentation manifest, generated allocation hooks |
| `C:\dev\helworks\helengine-windows` | Windows profile metadata, native configuration, Tracy dependency, host/GPU markers, packaging |

The approved design is [2026-07-28-windows-generated-cpp-profiler-build-design.md](../specs/2026-07-28-windows-generated-cpp-profiler-build-design.md). Generated C++ must be changed only through `csharpcodegen`; never patch generated output after emission.

## Milestone order

1. Build-profile plumbing and native configuration.
2. Exhaustive generated-function CPU zones and profiler package artifacts.
3. Frame boundaries and runtime plots.
4. Allocation, lock, and D3D11 GPU instrumentation.
5. Deterministic captures and end-to-end Release-isolation validation.

Each milestone must leave `debug` and `release` usable. Do not combine all repositories into one large unverified commit.

### Task 1: Decouple the editor publish configuration from the player build profile

**Files:**

- Modify: `C:\dev\helworks\helengine\scripts\build-platform.ps1`
- Create: `C:\dev\helworks\helengine\scripts\tests\build-platform-profile.tests.ps1`

- [ ] **Step 1: Write the failing PowerShell contract test**

Create `scripts/tests/build-platform-profile.tests.ps1` with a source-level contract that requires a `BuildProfile` parameter, validates it, and passes it to `--build-profile` independently of `Configuration`:

```powershell
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$WrapperPath = Join-Path $RepositoryRoot "scripts\build-platform.ps1"
$Source = Get-Content -LiteralPath $WrapperPath -Raw

foreach ($RequiredToken in @(
        '[string]$BuildProfile = ""',
        '$ResolvedBuildProfile',
        '"--build-profile"',
        '$ResolvedBuildProfile'
    )) {
    if (-not $Source.Contains($RequiredToken)) {
        throw "The build wrapper is missing player-profile token '$RequiredToken'."
    }
}

if ($Source.Contains('$Configuration.ToLowerInvariant()')) {
    $LegacyProfileBinding = [regex]::IsMatch(
        $Source,
        '--build-profile[\s\S]{0,100}\$Configuration\.ToLowerInvariant\(\)')
    if ($LegacyProfileBinding) {
        throw "The player build profile must not be derived at the editor invocation site from Configuration."
    }
}

Write-Output "BUILD_PROFILE_TEST_PASS"
```

- [ ] **Step 2: Run the test and confirm it fails**

Run from `C:\dev\helworks\helengine`:

```powershell
& .\scripts\tests\build-platform-profile.tests.ps1
```

Expected: failure stating that `BuildProfile` is missing.

- [ ] **Step 3: Implement independent profile resolution**

Add this parameter after `Configuration`:

```powershell
[Parameter()]
[string]$BuildProfile = "",
```

After validating `Configuration`, resolve the player profile once:

```powershell
$ResolvedBuildProfile = $BuildProfile
if ([string]::IsNullOrWhiteSpace($ResolvedBuildProfile)) {
    if ($Configuration -ieq "Debug" -or $Configuration -ieq "Release") {
        $ResolvedBuildProfile = $Configuration.ToLowerInvariant()
    } else {
        [Console]::Error.WriteLine("BuildProfile is required when Configuration is not Debug or Release.")
        exit 2
    }
}
```

Replace the conditional `--build-profile` block with:

```powershell
$EditorRunArguments += @(
    "--build-profile",
    $ResolvedBuildProfile
)
```

This preserves every existing Debug/Release invocation and enables:

```powershell
-Configuration Release -BuildProfile profiler
```

- [ ] **Step 4: Run the focused wrapper tests**

```powershell
& .\scripts\tests\build-platform-profile.tests.ps1
& .\scripts\tests\build-platform-streaming.tests.ps1
```

Expected: both print their `*_TEST_PASS` marker.

- [ ] **Step 5: Commit the wrapper slice**

```powershell
git add scripts/build-platform.ps1 scripts/tests/build-platform-profile.tests.ps1
git commit -m "build: separate player profile from editor configuration"
```

### Task 2: Expose the Windows Profiler profile and bind its codegen default

**Files:**

- Modify: `builder/WindowsPlatformDefinitionFactory.cs`
- Modify: `builder/WindowsPlatformAssetBuilder.cs`
- Modify: `builder.tests/WindowsPlatformAssetBuilderTests.cs`

- [ ] **Step 1: Extend the metadata test first**

In `Descriptor_and_definition_expose_windows_metadata`, add:

```csharp
PlatformBuildProfileDefinition profilerProfile = Assert.Single(
    builder.Definition.BuildProfiles,
    profile => profile.ProfileId == "profiler");

Assert.Equal("Profiler", profilerProfile.DisplayName);
Assert.Equal("directx11", profilerProfile.GraphicsProfileId);
Assert.Equal("default", profilerProfile.CodegenProfileId);
Assert.Equal(
    "true",
    profilerProfile.CodegenSettingDefaultValues["codegen-generated-function-profiling"]);
```

Also assert that Debug and Release default this option to `false`.

- [ ] **Step 2: Run the focused test and confirm it fails**

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter "Descriptor_and_definition_expose_windows_metadata" --no-restore
```

Expected: no `profiler` profile exists.

- [ ] **Step 3: Add the codegen setting and profile defaults**

Add a boolean setting to the existing `default` codegen profile:

```csharp
new PlatformSettingDefinition(
    "codegen-generated-function-profiling",
    "Generated Function Profiling",
    PlatformSettingKind.Boolean,
    "false",
    true,
    [])
```

Give both Debug and Release this default map:

```csharp
new Dictionary<string, string>(StringComparer.Ordinal) {
    ["codegen-generated-function-profiling"] = "false"
}
```

Add the Profiler profile with the same graphics/build settings as Release and this default map:

```csharp
new PlatformBuildProfileDefinition(
    "profiler",
    "Profiler",
    "Optimized Windows player with generated C++ profiling",
    "directx11",
    "default",
    [
        new PlatformSettingDefinition(
            "texture-scale-percent",
            "Texture Scale Percent",
            PlatformSettingKind.Text,
            "100",
            true,
            []),
        new PlatformSettingDefinition(
            "shader-variant-pruning",
            "Shader Variant Pruning",
            PlatformSettingKind.Boolean,
            "true",
            true,
            [])
    ],
    new Dictionary<string, string>(StringComparer.Ordinal) {
        ["codegen-generated-function-profiling"] = "true"
    })
```

Update `CreateDescriptor()` in `WindowsPlatformAssetBuilder.cs` so its supported profile list is exactly `debug`, `release`, `profiler`.

- [ ] **Step 4: Run metadata tests**

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter "Descriptor_and_definition_expose_windows_metadata" --no-restore
```

Expected: pass.

- [ ] **Step 5: Commit the metadata slice**

```powershell
git add builder/WindowsPlatformDefinitionFactory.cs builder/WindowsPlatformAssetBuilder.cs builder.tests/WindowsPlatformAssetBuilderTests.cs
git commit -m "build: add Windows profiler profile metadata"
```

### Task 3: Preserve original source identity in the conversion model

**Files:**

- Create: `C:\dev\helworks\csharpcodegen\cs2.core\model\ConversionSourceLocation.cs`
- Modify: `C:\dev\helworks\csharpcodegen\cs2.core\model\ConversionFunction.cs`
- Modify: `C:\dev\helworks\csharpcodegen\cs2.core\model\ConversionVariable.cs`
- Modify: `C:\dev\helworks\csharpcodegen\cs2.core\ConversionPreProcessor.cs`
- Modify: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPClassEmitter.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp.tests\CPPProfilerSourceMetadataTests.cs`

- [ ] **Step 1: Write tests for methods, constructors, properties, operators, and indexers**

Use the existing converter test fixture pattern to convert one in-memory C# source containing all five forms. Assert each resulting `ConversionFunction` or `ConversionVariable` has an absolute/normalized source path, one-based line number, maintained symbol, and assembly name. Include an expression-bodied property and a synthesized auto-property accessor case.

The core assertions should be:

```csharp
Assert.Equal("ProfilerFixture.cs", Path.GetFileName(location.FilePath));
Assert.True(location.LineNumber > 0);
Assert.False(string.IsNullOrWhiteSpace(location.MaintainedSymbol));
Assert.Equal("ProfilerFixture", location.AssemblyName);
```

- [ ] **Step 2: Run the new test and confirm it fails**

```powershell
dotnet test .\cs2.cpp.tests\cs2.cpp.tests.csproj --filter "FullyQualifiedName~CPPProfilerSourceMetadataTests" --no-restore
```

- [ ] **Step 3: Add the source-location model**

`ConversionSourceLocation.cs` must contain one documented class:

```csharp
namespace cs2.core {
    /// <summary>
    /// Describes the maintained C# declaration represented by one generated native member.
    /// </summary>
    public sealed class ConversionSourceLocation {
        /// <summary>Gets or sets the source assembly that owns the declaration.</summary>
        public string AssemblyName { get; set; } = string.Empty;

        /// <summary>Gets or sets the maintained namespace, type, and member identity.</summary>
        public string MaintainedSymbol { get; set; } = string.Empty;

        /// <summary>Gets or sets the normalized maintained source path.</summary>
        public string FilePath { get; set; } = string.Empty;

        /// <summary>Gets or sets the one-based maintained source line.</summary>
        public int LineNumber { get; set; }
    }
}
```

Add a documented `SourceLocation` property to `ConversionFunction` and `ConversionVariable`; initialize it only from a real Roslyn declaration. Do not invent empty location objects for declarations that should have valid source metadata.

- [ ] **Step 4: Populate locations at preprocessing time**

Add a documented static method on `ConversionPreProcessor` (not a local helper) that reads `syntax.GetLocation().GetLineSpan()` and `semantic.Compilation.AssemblyName`. Set the maintained symbol from the declared method/property symbol using the same stable display format as `BuildSourceMethodKey`. Copy property metadata into `CreateGetter`/`CreateSetter`, and resolve bridge/default-constructor locations from their owning property/type declarations.

Reject inconsistent metadata with `InvalidOperationException`, for example a non-empty file path with line `0`; allow a clearly marked generated declaration only when no maintained syntax exists.

- [ ] **Step 5: Run source metadata and existing conversion tests**

```powershell
dotnet test .\cs2.cpp.tests\cs2.cpp.tests.csproj --filter "FullyQualifiedName~CPPProfilerSourceMetadataTests|FullyQualifiedName~CPPCompileValidationRegressionTests" --no-restore
```

- [ ] **Step 6: Commit the source-model slice**

```powershell
git add cs2.core/model/ConversionSourceLocation.cs cs2.core/model/ConversionFunction.cs cs2.core/model/ConversionVariable.cs cs2.core/ConversionPreProcessor.cs cs2.cpp/CPPClassEmitter.cs cs2.cpp.tests/CPPProfilerSourceMetadataTests.cs
git commit -m "codegen: preserve generated member source identity"
```

### Task 4: Emit exhaustive static Tracy zones and the generator manifest

**Files:**

- Modify: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPCodegenOptionNames.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPProfilerOptionResolver.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPProfilerCategoryResolver.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPProfilerScopeEmitter.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPGeneratedProfilerSupportWriter.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPProfilerManifestCollector.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPProfilerManifestWriter.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp\model\CPPProfilerManifestEntry.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.core\LineTrackingTextWriter.cs`
- Modify: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPClassEmitter.cs`
- Modify: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPCodeConverter.cs`
- Modify: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPGeneratedConfigWriter.cs`
- Modify: `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPWindowsHandoffWriter.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp.tests\CPPGeneratedProfilerInstrumentationTests.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp.tests\CPPProfilerManifestWriterTests.cs`

- [ ] **Step 1: Write failing profile-on/profile-off tests**

Convert a fixture containing a normal method, early return, throw, constructor, operator, expression property, storage-backed property, interface bridge, base bridge, and free operator. With `codegen-generated-function-profiling=true`, assert:

```csharp
Assert.Contains("#include \"runtime/generated_profiler.hpp\"", generatedSource);
Assert.Contains("ProfilerFixture.cs", generatedSource);
Assert.Contains("[physics]", generatedSource);
Assert.True(File.Exists(Path.Combine(outputPath, "generated_profiler_manifest.json")));
```

Assert each named fixture body has one scope, then assert the number of emitted scope macros equals `instrumentedFunctionCount` in the manifest. Do not hardcode a total that can change when a legitimate synthesized body is added.

With the option `false`, assert that the include, macro calls, support header, and manifest are all absent.

- [ ] **Step 2: Run the tests and confirm they fail**

```powershell
dotnet test .\cs2.cpp.tests\cs2.cpp.tests.csproj --filter "FullyQualifiedName~CPPGeneratedProfilerInstrumentationTests|FullyQualifiedName~CPPProfilerManifestWriterTests" --no-restore
```

- [ ] **Step 3: Add strict option resolution**

Add:

```csharp
public const string GeneratedFunctionProfiling = "codegen-generated-function-profiling";
```

`CPPProfilerOptionResolver.IsEnabled` must return false when absent, accept only parseable booleans, and throw for malformed values rather than silently disabling requested profiling.

- [ ] **Step 4: Generate a Tracy support header only for profiler output**

`CPPGeneratedProfilerSupportWriter` writes `runtime/generated_profiler.hpp` only when enabled. Its generated body is:

```cpp
#pragma once

#if !defined(TRACY_ENABLE)
#error Generated function profiling requires TRACY_ENABLE.
#endif

#include <tracy/Tracy.hpp>

#define HE_PROFILE_GENERATED_FUNCTION(Identifier, Name, Function, File, Line, Color) \
    static constexpr tracy::SourceLocationData Identifier##_source_location { \
        Name, Function, File, static_cast<uint32_t>(Line), Color }; \
    tracy::ScopedZone Identifier( \
        &Identifier##_source_location, TRACY_CALLSTACK, true)
```

This uses Tracy's static `SourceLocationData`/RAII API. It performs no per-call formatting, allocation, or symbol lookup.

`CPPGeneratedConfigWriter` must also emit `HE_CPP_GENERATED_FUNCTION_PROFILING` as `1` or `0` from the same strict option resolver. Runtime templates use this compile-time value; they must not re-parse string options.

- [ ] **Step 5: Centralize zone emission**

`CPPProfilerScopeEmitter.WriteScope` must:

- validate maintained source metadata;
- derive a stable subsystem category and category color;
- build a static display name such as `[physics] helengine.bepu.BepuPhysicsWorld3D.Step`;
- include the generated C++ symbol in Tracy's function field;
- allocate a deterministic identifier such as `helengine_profile_zone_000123`;
- write one macro invocation immediately after the opening brace;
- record maintained and generated source paths/lines in `CPPProfilerManifestCollector`.

Call this one method after every body-opening line currently emitted at these ten sites in `CPPClassEmitter.cs`: interface getter/setter bridges, base getter/setter bridges, implicit default constructor, expression-bodied getter, storage getter/setter, normal function definition, and free function definition. Do not duplicate formatting logic at those sites.

- [ ] **Step 6: Track generated source lines without rewriting output**

Wrap each generated source `StreamWriter` with `LineTrackingTextWriter` before invoking `CPPClassEmitter`. The wrapper increments `CurrentLineNumber` in `Write`, `WriteLine`, and async overrides, then delegates unchanged text. `CPPProfilerScopeEmitter` records the line on which its macro invocation begins. This is emission-time metadata, not post-processing.

- [ ] **Step 7: Write the generator manifest**

Write `generated_profiler_manifest.json` only when profiling is enabled, with this stable shape:

```json
{
  "schemaVersion": 1,
  "instrumentedFunctionCount": 1,
  "excludedFunctionCount": 0,
  "functions": [
    {
      "zoneId": "helengine_profile_zone_000001",
      "category": "physics",
      "assembly": "helengine.bepu",
      "maintainedSymbol": "helengine.bepu.BepuPhysicsWorld3D.Step(double)",
      "generatedSymbol": "helengine::bepu::BepuPhysicsWorld3D::Step",
      "sourceFile": "C:/dev/helworks/helengine/engine/helengine.bepu/BepuPhysicsWorld3D.cs",
      "sourceLine": 1,
      "generatedFile": "BepuPhysicsWorld3D.cpp",
      "generatedLine": 1
    }
  ],
  "exclusions": []
}
```

Record exclusions for bodyless declarations and explicitly unsafe native stubs. Counts must be derived from the collector, never entered independently.

- [ ] **Step 8: Run generator tests and compile-validation tests**

```powershell
dotnet test .\cs2.cpp.tests\cs2.cpp.tests.csproj --filter "FullyQualifiedName~CPPGeneratedProfilerInstrumentationTests|FullyQualifiedName~CPPProfilerManifestWriterTests|FullyQualifiedName~CPPCompileValidationRegressionTests" --no-restore
```

- [ ] **Step 9: Commit the generated-zone slice**

```powershell
git add cs2.core cs2.cpp cs2.cpp.tests
git commit -m "codegen: emit generated C++ Tracy function zones"
```

### Task 5: Pin Tracy and make CMake profiling conditional

**Files:**

- Create: `.gitmodules`
- Add submodule: `third_party/tracy` pinned to tag `v0.13.1`
- Modify: `CMakeLists.txt`
- Create: `cmake/tests/verify_profiler_configuration.ps1`

- [ ] **Step 1: Write a failing CMake source contract**

The PowerShell test reads `CMakeLists.txt` and requires all of:

```text
HELENGINE_WINDOWS_PROFILER
third_party/tracy
Tracy::TracyClient
TRACY_ON_DEMAND
TRACY_NO_SAMPLING
RelWithDebInfo
```

It must also reject unconditional `add_subdirectory(third_party/tracy)` or unconditional Tracy linkage.

- [ ] **Step 2: Run the contract and confirm it fails**

```powershell
& .\cmake\tests\verify_profiler_configuration.ps1
```

- [ ] **Step 3: Add the pinned dependency**

```powershell
git submodule add https://github.com/wolfpld/tracy.git third_party/tracy
git -C third_party/tracy checkout v0.13.1
git add .gitmodules third_party/tracy
```

Keep Tracy's BSD-3-Clause `LICENSE` in the submodule. Do not use `FetchContent`; native builds must not depend on network access.

- [ ] **Step 4: Add profiler-only CMake integration**

Add:

```cmake
option(HELENGINE_WINDOWS_PROFILER "Build the optimized Tracy-instrumented Windows player." OFF)

if(HELENGINE_WINDOWS_PROFILER)
    if(NOT MSVC)
        message(FATAL_ERROR "The Windows profiler profile currently requires MSVC.")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy/CMakeLists.txt")
        message(FATAL_ERROR "Tracy 0.13.1 is missing. Initialize the third_party/tracy submodule.")
    endif()

    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    set(TRACY_CALLSTACK OFF CACHE BOOL "" FORCE)
    set(TRACY_NO_SAMPLING OFF CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/tracy EXCLUDE_FROM_ALL)
endif()
```

After `add_executable`:

```cmake
if(HELENGINE_WINDOWS_PROFILER)
    target_compile_definitions(helengine_windows PRIVATE HELENGINE_WINDOWS_PROFILER=1)
    target_link_libraries(helengine_windows PRIVATE Tracy::TracyClient)
endif()
```

Do not define `TRACY_ENABLE` manually on the player; the linked Tracy target publishes the matching definition. Release must not add the Tracy subdirectory or target.

- [ ] **Step 5: Run the contract and a Release configure**

```powershell
& .\cmake\tests\verify_profiler_configuration.ps1
cmake -S . -B .\build\release-isolation -G Ninja -DCMAKE_BUILD_TYPE=Release -DHELENGINE_WINDOWS_PROFILER=OFF -DHELENGINE_WINDOWS_INCLUDE_GENERATED_CORE=OFF
```

Expected: contract passes; configure succeeds without creating a Tracy target in the build graph.

- [ ] **Step 6: Commit the dependency/CMake slice**

```powershell
git add .gitmodules third_party/tracy CMakeLists.txt cmake/tests/verify_profiler_configuration.ps1
git commit -m "build: add profiler-only Tracy client"
```

### Task 6: Resolve native Debug, Release, and Profiler configurations explicitly

**Files:**

- Create: `builder/WindowsNativeBuildProfile.cs`
- Create: `builder/WindowsNativeBuildProfileResolver.cs`
- Create: `builder/WindowsNativeBuildResult.cs`
- Modify: `builder/WindowsNativeBuildExecutor.cs`
- Modify: `builder/WindowsBuildWorkspace.cs`
- Modify: `builder.tests/WindowsNativeBuildExecutorTests.cs`
- Create: `builder.tests/WindowsNativeBuildProfileResolverTests.cs`

- [ ] **Step 1: Write failing resolver and command-line tests**

Assert exact mappings:

| Player profile | CMake build type | Profiler flag | PDB required |
| --- | --- | --- | --- |
| `debug` | `Debug` | `OFF` | no |
| `release` | `Release` | `OFF` | no |
| `profiler` | `RelWithDebInfo` | `ON` | yes |

Also assert unknown profiles throw and profiler configure arguments contain:

```text
-DCMAKE_BUILD_TYPE=RelWithDebInfo
-DHELENGINE_WINDOWS_PROFILER=ON
```

Add paired assertions that Release contains `-DCMAKE_BUILD_TYPE=Release` and `-DHELENGINE_WINDOWS_PROFILER=OFF`. Add request-validation tests proving Profiler rejects a false or missing generated-function option and Debug/Release reject a true generated-function option.

- [ ] **Step 2: Run and confirm failure**

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter "FullyQualifiedName~WindowsNativeBuildProfileResolverTests|FullyQualifiedName~WindowsNativeBuildExecutorTests" --no-restore
```

- [ ] **Step 3: Add the typed profile resolver**

`WindowsNativeBuildProfile` is an enum with `Debug`, `Release`, and `Profiler`. `WindowsNativeBuildProfileResolver.Resolve(string)` validates and returns the enum. Add documented methods for CMake build type, profiler-enabled state, and symbol requirement. Do not pass unvalidated strings into CMake.

- [ ] **Step 4: Return all native artifacts explicitly**

Change `IWindowsNativeBuildExecutor.Build` to accept `WindowsNativeBuildProfile` and return:

```csharp
internal sealed class WindowsNativeBuildResult {
    /// <summary>Initializes the immutable native-build artifact set.</summary>
    public WindowsNativeBuildResult(string executablePath, string programDatabasePath) {
        ExecutablePath = executablePath;
        ProgramDatabasePath = programDatabasePath;
    }

    /// <summary>Gets the produced player executable path.</summary>
    public string ExecutablePath { get; }

    /// <summary>Gets the PDB path, or an empty string for profiles that did not emit one.</summary>
    public string ProgramDatabasePath { get; }
}
```

The executor requires a PDB for Profiler and throws if it is missing.

- [ ] **Step 5: Build profile-specific CMake commands**

Configure with the single-config Ninja build type and explicit profiler flag. The Profiler test expects `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DHELENGINE_WINDOWS_PROFILER=ON`; the Release test expects `-DCMAKE_BUILD_TYPE=Release -DHELENGINE_WINDOWS_PROFILER=OFF`.

Remove the hardcoded `--config Release`; for Ninja use `cmake --build` with the validated `buildRoot` argument. Pass `request.SelectedBuildProfileId` through `WindowsNativeBuildProfileResolver` in `WindowsBuildWorkspace`. Before generation/native build, enforce that only Profiler can enable `codegen-generated-function-profiling`, and that Profiler must enable it.

- [ ] **Step 6: Run focused builder tests**

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter "FullyQualifiedName~WindowsNativeBuildProfileResolverTests|FullyQualifiedName~WindowsNativeBuildExecutorTests" --no-restore
```

- [ ] **Step 7: Commit the native-profile slice**

```powershell
git add builder builder.tests
git commit -m "build: select native Windows configuration by player profile"
```

### Task 7: Require and package the profiler manifest and PDB

**Files:**

- Create: `builder/WindowsProfilerPackageManifest.cs`
- Create: `builder/WindowsProfilerPackageManifestWriter.cs`
- Modify: `builder/WindowsBuildWorkspace.cs`
- Create: `builder.tests/WindowsProfilerPackageManifestWriterTests.cs`
- Modify: `builder.tests/WindowsPlatformAssetBuilderTests.cs`

- [ ] **Step 1: Write failing package tests**

Cover:

- Profiler fails when `generated_profiler_manifest.json` is absent.
- Profiler fails when the native PDB is absent.
- Profiler output contains EXE, PDB, and `helengine_profile_manifest.json`.
- Release output contains none of the generated/final profiler manifests and does not require a PDB.
- Final instrumented/excluded counts match the generator manifest exactly.

- [ ] **Step 2: Run and confirm failure**

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter "FullyQualifiedName~WindowsProfilerPackageManifestWriterTests|FullyQualifiedName~WindowsPlatformAssetBuilderTests" --no-restore
```

- [ ] **Step 3: Implement strict profiler packaging**

For Profiler only, deserialize and validate the generator manifest, copy the PDB, and write:

```json
{
  "schemaVersion": 1,
  "playerBuildProfile": "profiler",
  "nativeConfiguration": "RelWithDebInfo",
  "profiler": "Tracy",
  "profilerVersion": "0.13.1",
  "projectName": "...",
  "projectVersion": "...",
  "engineVersion": "...",
  "gitRevision": "...",
  "buildTimestampUtc": "...",
  "compiler": "MSVC",
  "instrumentedFunctionCount": 0,
  "excludedFunctionCount": 0,
  "enabledRuntimeFeatures": [],
  "sourceMappings": [],
  "exclusions": []
}
```

Use `System.Text.Json`; validate required strings/counts before writing. Copy the generator's function mappings and exclusions into the final manifest so the package remains the three-file contract from the design; do not ship a dangling `generated_profiler_manifest.json` reference. Missing Git revision may be represented as `"unavailable"`, but missing required build/profile identity must throw. Do not catch and downgrade validation errors.

- [ ] **Step 4: Run package tests**

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter "FullyQualifiedName~WindowsProfilerPackageManifestWriterTests|FullyQualifiedName~WindowsPlatformAssetBuilderTests" --no-restore
```

- [ ] **Step 5: Commit the packaging slice**

```powershell
git add builder builder.tests
git commit -m "build: package Windows profiler symbols and manifest"
```

### Task 8: Add top-level CPU zones, frame markers, and runtime plots

**Files:**

- Create: `src/platform/windows/profiling/windows_profiler.hpp`
- Create: `src/platform/windows/profiling/windows_profiler.cpp`
- Modify: `src/platform/windows/win32/win32_application.cpp`
- Modify: `src/platform/windows/win32/win32_input_bridge.cpp`
- Modify: `src/platform/windows/win32/win32_render_bridge.cpp`
- Modify: `engine/helengine.core/Core.cs`
- Modify: `engine/helengine.core/physics/IPhysicsRuntime.cs`
- Modify: `engine/helengine.core/physics/ISceneBindablePhysicsRuntime.cs`
- Modify: `engine/helengine.bepu/BepuPhysicsWorld3D.cs`
- Modify: `engine/helengine.physics3d/PhysicsWorld3D.cs`
- Add focused tests beside the modified engine projects and in `builder.tests`

- [ ] **Step 1: Add failing tests for stable metric accessors**

Move `RegisteredBodyCount` from `ISceneBindablePhysicsRuntime` to `IPhysicsRuntime` and add maintained properties for candidate pairs, confirmed pairs, and solved constraints to `IPhysicsRuntime`. Update all four implementations (`BepuPhysicsWorld3D`, `PhysicsWorld3D`, `PhysicsWorld3DCompatibilityRuntime`, and `TestPhysicsRuntime`). Add fixed-step, draw-call, and submitted-triangle snapshots to `Core`. Tests must verify counters reset at frame/step boundaries and represent the latest completed frame, not cumulative stale values.

- [ ] **Step 2: Implement maintained metrics at their owners**

Keep physics counters in each physics runtime, render counters in render ownership, and aggregate only read-only snapshots on `Core`. Do not make the Windows host inspect BEPU internals with casts. Every new C# member requires substantive XML comments.

- [ ] **Step 3: Add the profiler bridge**

`windows_profiler.hpp` compiles to no-ops when `HELENGINE_WINDOWS_PROFILER` is absent and maps to Tracy when present:

```cpp
#if defined(HELENGINE_WINDOWS_PROFILER)
#include <tracy/Tracy.hpp>
#define HELENGINE_PROFILE_ZONE(Name) ZoneScopedN(Name)
#define HELENGINE_PROFILE_FRAME() FrameMark
#define HELENGINE_PROFILE_PLOT(Name, Value) TracyPlot(Name, Value)
#else
#define HELENGINE_PROFILE_ZONE(Name)
#define HELENGINE_PROFILE_FRAME()
#define HELENGINE_PROFILE_PLOT(Name, Value)
#endif
```

Use named variables (`ZoneNamedN`) when two zones can exist in the same lexical scope.

- [ ] **Step 4: Place the required handwritten boundaries**

Add zones at the narrowest owning methods for:

- complete frame in `Win32Application::RenderFrame`;
- input collection in `win32_input_bridge.cpp`;
- generated `Core.Update` and `Core.Draw` are already exhaustively zoned;
- fixed-step scheduler and each individual physics step in maintained `Core.cs`, so generated output captures each catch-up step separately;
- scene-operation commit, render extraction, and draw-command construction in their maintained C# owners;
- D3D submission and present/wait in the Windows render bridge/presenter.

Finish each successful frame with `HELENGINE_PROFILE_FRAME()`.

During profiler initialization, append `[Profiler]` to the player window title, set Tracy's program name, and write the Profiler build identity to the existing lifecycle log. Add a test that Debug/Release titles and logs remain unchanged.

- [ ] **Step 5: Publish plots once per completed frame**

Plot frame/update/physics/render duration, fixed-step count, body/pair/constraint counts, allocation/live bytes, draw calls, and triangles. Configure plots once during profiler initialization; do not build plot names dynamically per frame.

- [ ] **Step 6: Verify generated-core and Windows tests**

```powershell
dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --filter "FullyQualifiedName~CoreTimingTests" --no-restore
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter "Profiler" --no-restore
```

- [ ] **Step 7: Commit separately in each repository**

Use one commit for maintained engine metrics and one for Windows host boundaries.

### Task 9: Instrument engine-owned allocations and synchronization

**Files:**

- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp\.net.cpp\runtime\generated_profiler_memory.hpp`
- Modify: object/array creation and deletion paths in `C:\dev\helworks\csharpcodegen\cs2.cpp\CPPConversiorProcessor.cs`
- Modify: `C:\dev\helworks\csharpcodegen\cs2.cpp\.net.cpp\system\runtime\interopservices\native_memory.hpp`
- Modify engine-owned synchronization templates under `C:\dev\helworks\csharpcodegen\cs2.cpp\.net.cpp\system\threading\`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp.tests\CPPProfilerAllocationInstrumentationTests.cs`
- Create: `C:\dev\helworks\csharpcodegen\cs2.cpp.tests\CPPProfilerLockInstrumentationTests.cs`

- [ ] **Step 1: Write allocation lifetime tests first**

Generate object, array, scoped temporary, reassignment, explicit delete, and exception-cleanup cases. In Profiler output, assert exactly one named allocation event follows successful ownership acquisition and exactly one matching free precedes disposal. In Release output, assert no Tracy memory macro appears.

- [ ] **Step 2: Add generated memory wrappers**

Use stable macros that become Tracy calls only in Profiler:

```cpp
#if defined(HE_CPP_GENERATED_FUNCTION_PROFILING) && HE_CPP_GENERATED_FUNCTION_PROFILING
#include <tracy/Tracy.hpp>
#define HE_PROFILE_ALLOC(Pointer, Size, Pool) TracyAllocN(Pointer, Size, Pool)
#define HE_PROFILE_FREE(Pointer, Size, Pool) TracyFreeN(Pointer, Pool)
#else
#define HE_PROFILE_ALLOC(Pointer, Size, Pool)
#define HE_PROFILE_FREE(Pointer, Size, Pool)
#endif
```

Emit hooks from the generator's semantic allocation/deletion paths; never search-and-replace generated `new`/`delete` text. Preserve existing ownership guards and exception behavior. Pass the known object/array allocation size to both hooks so profiler-only atomic counters can publish allocated and live bytes without maintaining an allocating pointer map. Cover `NativeMemory` malloc/free separately with pool name `native-memory`; if its existing free contract cannot recover size, extend the maintained ownership record to retain the allocation size instead of guessing.

- [ ] **Step 3: Write and implement lock tests**

Wrap only engine-owned mutexes/condition primitives in Tracy lockable types under the profiler config. Preserve the same public API and storage/lifetime. Release aliases must resolve to the current standard-library primitives without Tracy includes.

- [ ] **Step 4: Run allocation/lock and compile-validation tests**

```powershell
dotnet test .\cs2.cpp.tests\cs2.cpp.tests.csproj --filter "FullyQualifiedName~CPPProfilerAllocationInstrumentationTests|FullyQualifiedName~CPPProfilerLockInstrumentationTests|FullyQualifiedName~CPPCompileValidationRegressionTests" --no-restore
```

- [ ] **Step 5: Commit the runtime-instrumentation slice**

```powershell
git add cs2.cpp cs2.cpp.tests
git commit -m "codegen: profile generated allocations and locks"
```

### Task 10: Add Direct3D 11 GPU profiling

**Files:**

- Modify: `src/platform/windows/directx11/directx11_bootstrap.hpp`
- Modify: `src/platform/windows/directx11/directx11_bootstrap.cpp`
- Modify: `src/platform/windows/directx11/directx11_presenter.cpp`
- Modify: `src/platform/windows/win32/win32_render_bridge.cpp`
- Create: `builder.tests/WindowsD3D11ProfilerSourceTests.cs`

- [ ] **Step 1: Write source-contract tests**

Require profiler-only uses of `TracyD3D11Context`, `TracyD3D11NewFrame`, `TracyD3D11Zone`, `TracyD3D11Collect`, and `TracyD3D11Destroy`. Assert every symbol is behind `HELENGINE_WINDOWS_PROFILER`.

- [ ] **Step 2: Add a single owned Tracy GPU context**

Create it after the D3D11 device and immediate context exist:

```cpp
#if defined(HELENGINE_WINDOWS_PROFILER)
TracyGpuContext = TracyD3D11Context(Device.Get(), DeviceContext.Get());
TracyD3D11ContextName(TracyGpuContext, "Helengine Direct3D 11", 20);
#endif
```

Destroy it before releasing the D3D device/context. No fallback context is allowed when Profiler was requested.

- [ ] **Step 3: Add GPU frame and submission zones**

Call `TracyD3D11NewFrame` once at frame start, place GPU zones around render-target clear, 3D submission, 2D submission, and final composition, and call `TracyD3D11Collect` after submission/present. Keep CPU present/wait as a separate CPU zone.

- [ ] **Step 4: Run source tests and a native Profiler compile**

```powershell
dotnet test .\builder.tests\helengine.windows.builder.tests.csproj --filter "FullyQualifiedName~WindowsD3D11ProfilerSourceTests" --no-restore
```

Then perform the smallest generated-core Profiler native build available. Expected: Tracy D3D11 code links only in Profiler.

- [ ] **Step 5: Commit the GPU slice**

```powershell
git add src/platform/windows/directx11 src/platform/windows/win32 builder.tests
git commit -m "profile: add Direct3D 11 GPU zones"
```

### Task 11: Add deterministic benchmark execution and capture smoke testing

**Files:**

- Create: `profiling/README.md`
- Create: `profiling/benchmarks/windows-profiler-benchmarks.json`
- Create: `scripts/run-profiler-benchmark.ps1`
- Create: `scripts/test-profiler-capture.ps1`
- Modify: `src/platform/windows/win32/win32_application.cpp`
- Create benchmark scene assets under `C:\dev\helprojs\demodisc\assets\profiler-benchmarks\`
- Create benchmark gameplay components under `C:\dev\helprojs\demodisc\code\profiler-benchmarks\`

- [ ] **Step 1: Define benchmark metadata**

The JSON catalog contains stable IDs and fixed warmup/capture frames for:

```json
{
  "schemaVersion": 1,
  "benchmarks": [
    { "id": "empty-frame", "warmupFrames": 300, "captureFrames": 600 },
    { "id": "four-static-cubes", "warmupFrames": 300, "captureFrames": 600 },
    { "id": "four-dynamic-cubes", "warmupFrames": 300, "captureFrames": 600 },
    { "id": "rigid-body-scaling", "warmupFrames": 300, "captureFrames": 600 },
    { "id": "contact-pair-scaling", "warmupFrames": 300, "captureFrames": 600 },
    { "id": "constraint-scaling", "warmupFrames": 300, "captureFrames": 600 },
    { "id": "scene-update-stress", "warmupFrames": 300, "captureFrames": 600 },
    { "id": "render-stress", "warmupFrames": 300, "captureFrames": 600 }
  ]
}
```

- [ ] **Step 2: Add Profiler-only controlled-frame execution**

Read explicit command-line values such as `--profile-warmup-frames` and `--profile-capture-frames` only under `HELENGINE_WINDOWS_PROFILER`. Emit a named frame mark when capture begins and exit cleanly after the requested frame count. Invalid or negative values must fail startup.

- [ ] **Step 3: Automate Tracy capture without bundling the viewer**

`run-profiler-benchmark.ps1` requires an explicit `-TracyCapturePath` (or documented `TRACY_CAPTURE_PATH`), starts Tracy's official capture utility, launches the packaged player with deterministic frame arguments, waits for both, and writes the `.tracy` capture beside a copy of `helengine_profile_manifest.json` plus benchmark metadata. It must not download tools or silently run without capture.

- [ ] **Step 4: Add the end-to-end smoke assertions**

The smoke test builds the Demo Disc profile benchmark fixture, captures controlled frames, and requires an explicit `-TracyCsvExportPath` pointing to Tracy's official `csvexport` utility. It asserts that the capture contains:

- frame markers;
- one generated fixture method;
- one physics step zone;
- one D3D11 GPU zone;
- nonzero fixed-step/body plots.

If the official export utility is not installed at the supplied path, report the test as an explicit prerequisite failure, not a pass or skip.

- [ ] **Step 5: Commit deterministic workflow files**

```powershell
git add profiling scripts src/platform/windows/win32
git commit -m "profile: add deterministic Windows capture workflow"
```

### Task 12: Run full validation and prove Release isolation

**Files:**

- Modify documentation only if validation exposes missing setup details.

- [ ] **Step 1: Run focused test suites in all repositories**

```powershell
dotnet test C:\dev\helworks\csharpcodegen\cs2.cpp.tests\cs2.cpp.tests.csproj --filter "Profiler|CompileValidationRegressionTests" --no-restore
dotnet test C:\dev\helworks\helengine-windows\builder.tests\helengine.windows.builder.tests.csproj --filter "Profiler|NativeBuild|Descriptor_and_definition" --no-restore
dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --filter "CoreTimingTests|BuildProfileDefaultResolver" --no-restore
& C:\dev\helworks\helengine\scripts\tests\build-platform-profile.tests.ps1
```

- [ ] **Step 2: Build Demo Disc Profiler**

```powershell
& C:\dev\helworks\helengine\scripts\build-platform.ps1 `
    -Project C:\dev\helprojs\demodisc `
    -Platform windows `
    -Output C:\dev\helprojs\demodisc\output\windows-profiler `
    -Configuration Release `
    -BuildProfile profiler
```

Verify the output contains exactly the expected profiler artifacts plus normal cooked content:

```powershell
Get-Item `
    C:\dev\helprojs\demodisc\output\windows-profiler\helengine_windows.exe, `
    C:\dev\helprojs\demodisc\output\windows-profiler\helengine_windows.pdb, `
    C:\dev\helprojs\demodisc\output\windows-profiler\helengine_profile_manifest.json
```

- [ ] **Step 3: Capture and inspect one controlled benchmark**

Connect the official Tracy 0.13.1 viewer/capture utility before launching. Confirm the success criteria from the design: generated call hierarchy, inclusive/exclusive timing, invocation counts, maintained C# source, frame/physics workload plots, sampled external time, and GPU timing.

- [ ] **Step 4: Build Demo Disc Release separately**

```powershell
& C:\dev\helworks\helengine\scripts\build-platform.ps1 `
    -Project C:\dev\helprojs\demodisc `
    -Platform windows `
    -Output C:\dev\helprojs\demodisc\output\windows-release-isolation `
    -Configuration Release `
    -BuildProfile release 2>&1 | Tee-Object `
        -FilePath C:\dev\helprojs\demodisc\output\windows-release-isolation-build.log
```

- [ ] **Step 5: Prove Release contains no profiler payload**

Check all of:

```powershell
$ReleaseRoot = 'C:\dev\helprojs\demodisc\output\windows-release-isolation'
if (Test-Path (Join-Path $ReleaseRoot 'helengine_profile_manifest.json')) { throw 'Release contains profiler manifest.' }
if (Get-ChildItem -LiteralPath $ReleaseRoot -Recurse -File | Where-Object Name -Match 'tracy|profiler') { throw 'Release contains profiler-named payload.' }
$ReleaseBuildLog = Get-Content -LiteralPath 'C:\dev\helprojs\demodisc\output\windows-release-isolation-build.log' -Raw
if ($ReleaseBuildLog -match 'TracyClient\.cpp|third_party[\\/]tracy') { throw 'Release native build compiled Tracy.' }
```

Also inspect the Release generated core and assert it contains no `HE_PROFILE_GENERATED_FUNCTION`, `generated_profiler.hpp`, or `generated_profiler_manifest.json`. Because Tracy is statically linked, build-graph/log inspection is the linkage proof; PE imports alone are insufficient.

- [ ] **Step 6: Review instrumentation distortion**

Capture the same deterministic scene with generated zones enabled and with a temporary profiler diagnostic option that retains sampling/frame markers but disables generated zones. Record frame-time overhead in the profiler documentation. This diagnostic option must not become a fourth public build profile.

- [ ] **Step 7: Final commits and cross-repository status check**

Run `git status --short` in all three repositories, confirm only intended changes remain, and commit any documentation corrections separately. Do not stage pre-existing unrelated changes.

## Completion checklist

- [ ] Windows editor metadata exposes `debug`, `release`, and `profiler`.
- [ ] `-Configuration Release -BuildProfile profiler` publishes the editor in Release and builds the player as Profiler.
- [ ] Native Profiler uses `RelWithDebInfo`, MSVC PDBs, `HELENGINE_WINDOWS_PROFILER`, and Tracy 0.13.1 on demand.
- [ ] Every emitted generated function body receives one static RAII zone unless recorded as an explicit exclusion.
- [ ] Tracy shows maintained C# and generated C++ identity without per-call string construction.
- [ ] Frame, fixed-step, runtime workload, allocation, lock, and D3D11 GPU data are visible.
- [ ] Profiler packaging fails rather than falling back when Tracy, PDB, source metadata, or manifests are missing.
- [ ] A deterministic `.tracy` capture can be produced from a documented command.
- [ ] Release has no Tracy linkage, generated scopes, profiling table, definition, or manifest.
