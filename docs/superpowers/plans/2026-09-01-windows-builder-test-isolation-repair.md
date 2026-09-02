# Windows Builder Test Isolation Repair Plan

**Goal:** Make the Windows builder suite deterministic under parallel execution and align profiler artifact fixtures with the current generated-function profiling contract.

**Architecture:** Tests that mutate `Directory.CurrentDirectory` will share a non-parallel xUnit collection because the current directory is process-global. Profiler fixtures will provide the same maintained-symbol-prefix policy that the real Windows profiler platform definition supplies. Production builder behavior remains unchanged.

**Tech Stack:** C#/.NET 9, xUnit

---

### Task 1: Serialize current-directory-mutating builder fixtures

**Files:**
- Create: `builder.tests/ProcessCurrentDirectoryCollection.cs`
- Modify: `builder.tests/WindowsPlatformAssetBuilderTests.cs`
- Modify: `builder.tests/WindowsProfilerArtifactPackagingTests.cs`

- [ ] Define a named xUnit collection with `DisableParallelization = true`.
- [ ] Apply the collection to both classes that temporarily replace `Directory.CurrentDirectory`.
- [ ] Preserve each fixture's current-directory restoration in `finally`/dispose paths.

### Task 2: Supply the current profiler codegen policy

**Files:**
- Modify: `builder.tests/WindowsProfilerArtifactPackagingTests.cs`

- [ ] Add `WindowsGeneratedFunctionProfilingPolicy.MaintainedSymbolPrefixesSettingId` with `CoarseMaintainedSymbolPrefixes` when the fixture creates a profiler request.
- [ ] Keep production validation and artifact-copy logic unchanged.

### Task 3: Verify and integrate

**Files:**
- Modify: none

- [ ] Run `WindowsPlatformAssetBuilderTests` and `WindowsProfilerArtifactPackagingTests` together. Expected: 16/16 passing.
- [ ] Run the complete Windows builder test suite. Expected: 60/60 passing.
- [ ] Run `git diff --check` and commit only the planned test files with message `Isolate Windows builder test working directories`.
- [ ] Fast-forward Windows main after focused and complete verification remain green.
