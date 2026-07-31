namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Win32 host selects and configures exactly the physics runtime emitted by the Windows builder.
/// </summary>
public sealed class WindowsPhysicsRuntimeNativeSourceTests {
    /// <summary>
    /// Verifies the application consumes the generated backend manifest and keeps HelPhysics configuration exclusive from BEPU registration.
    /// </summary>
    [Fact]
    public void Win32Application_selects_one_generated_physics_runtime() {
        string source = File.ReadAllText(Path.Combine(
            ResolveWindowsRepositoryRootPath(),
            "src",
            "platform",
            "windows",
            "win32",
            "win32_application.cpp"));

        Assert.Contains("#include \"runtime/runtime_physics_backend_manifest.hpp\"", source, StringComparison.Ordinal);
        Assert.Contains("#if defined(HELENGINE_RUNTIME_PHYSICS_BACKEND_HELPHYSICS)", source, StringComparison.Ordinal);
        Assert.Contains("#elif defined(HELENGINE_RUNTIME_PHYSICS_BACKEND_BEPU)", source, StringComparison.Ordinal);
        Assert.Contains("#error \"The selected HelPhysics runtime was not generated.\"", source, StringComparison.Ordinal);
        Assert.Contains("#error \"The selected BEPU runtime was not generated.\"", source, StringComparison.Ordinal);
        Assert.Contains("new HelPhysicsWorldSettings3D(", source, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_RUNTIME_PHYSICS_VELOCITY_ITERATIONS", source, StringComparison.Ordinal);
        Assert.Contains("HELENGINE_RUNTIME_PHYSICS_PENETRATION_CORRECTION_PASSES", source, StringComparison.Ordinal);
        Assert.Contains("1.0 / static_cast<double>(HELENGINE_RUNTIME_PHYSICS_FIXED_STEP_HERTZ)", source, StringComparison.Ordinal);
        Assert.Contains("options->PhysicsFixedStepSeconds = physicsFixedStepSeconds;", source, StringComparison.Ordinal);
        Assert.Contains("HelPhysicsRuntimeComponentRegistration::RegisterConfigured(EngineCore, EngineHelPhysicsSettings);", source, StringComparison.Ordinal);
        Assert.Contains("HelPhysicsRuntimeComponentRegistration::UnregisterConfigured(EngineCore);", source, StringComparison.Ordinal);
        Assert.Contains("delete EngineHelPhysicsSettings;", source, StringComparison.Ordinal);
        Assert.Contains("Physics3DRuntimeComponentRegistration::Register(EngineCore);", source, StringComparison.Ordinal);

        int helPhysicsBranchIndex = source.LastIndexOf("#if defined(HELENGINE_RUNTIME_PHYSICS_BACKEND_HELPHYSICS)", StringComparison.Ordinal);
        int bepuBranchIndex = source.IndexOf("#elif defined(HELENGINE_RUNTIME_PHYSICS_BACKEND_BEPU)", helPhysicsBranchIndex, StringComparison.Ordinal);
        int registrationBoundaryEndIndex = source.IndexOf("#endif", bepuBranchIndex, StringComparison.Ordinal);
        string helPhysicsRegistrationBranch = source[helPhysicsBranchIndex..bepuBranchIndex];
        string bepuRegistrationBranch = source[bepuBranchIndex..registrationBoundaryEndIndex];
        Assert.DoesNotContain("Physics3DRuntimeComponentRegistration::Register", helPhysicsRegistrationBranch, StringComparison.Ordinal);
        Assert.DoesNotContain("HelPhysicsRuntimeComponentRegistration::RegisterConfigured", bepuRegistrationBranch, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies Escape closes the proof window only when the generated manifest selects HelPhysics.
    /// </summary>
    [Fact]
    public void Win32Window_limits_escape_exit_to_helphysics_builds() {
        string source = File.ReadAllText(Path.Combine(
            ResolveWindowsRepositoryRootPath(),
            "src",
            "platform",
            "windows",
            "win32",
            "win32_window.cpp"));

        Assert.Contains("#include \"runtime/runtime_physics_backend_manifest.hpp\"", source, StringComparison.Ordinal);
        Assert.Contains("#if defined(HELENGINE_RUNTIME_PHYSICS_BACKEND_HELPHYSICS)", source, StringComparison.Ordinal);
        Assert.Contains("case WM_KEYDOWN:", source, StringComparison.Ordinal);
        Assert.Contains("if (wParam == VK_ESCAPE)", source, StringComparison.Ordinal);
        Assert.Contains("DestroyWindow(Handle);", source, StringComparison.Ordinal);

        int helPhysicsBranchIndex = source.IndexOf("#if defined(HELENGINE_RUNTIME_PHYSICS_BACKEND_HELPHYSICS)", StringComparison.Ordinal);
        int branchEndIndex = source.IndexOf("#endif", helPhysicsBranchIndex, StringComparison.Ordinal);
        int escapeIndex = source.IndexOf("case WM_KEYDOWN:", StringComparison.Ordinal);
        Assert.True(helPhysicsBranchIndex < escapeIndex && escapeIndex < branchEndIndex);
    }

    /// <summary>
    /// Resolves the Windows repository root from the executing builder-test assembly.
    /// </summary>
    /// <returns>Absolute Windows repository root path.</returns>
    static string ResolveWindowsRepositoryRootPath() {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        if (!Directory.Exists(repositoryRootPath)) {
            throw new InvalidOperationException($"Could not resolve the Windows repository root from '{AppContext.BaseDirectory}'.");
        }

        return repositoryRootPath;
    }
}
