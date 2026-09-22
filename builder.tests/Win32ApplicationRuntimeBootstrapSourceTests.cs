namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the Windows host installs generated runtime modules before packaged scene startup.
/// </summary>
public sealed class Win32ApplicationRuntimeBootstrapSourceTests {
    /// <summary>
    /// Verifies generated registration is available independently of the legacy header and runs before startup-scene materialization.
    /// </summary>
    [Fact]
    public void Win32Application_registers_generated_runtime_modules_before_startup_scene() {
        string sourcePath = Path.Combine(AppContext.BaseDirectory, "TestInputs", "win32_application.cpp");
        string sourceCode = File.ReadAllText(sourcePath);

        Assert.Contains("#include \"GeneratedRuntimeModuleRegistration.hpp\"", sourceCode, StringComparison.Ordinal);
        Assert.Contains("#if defined(HELENGINE_WINDOWS_HAS_GENERATED_RUNTIME_MODULE_REGISTRATION)", sourceCode, StringComparison.Ordinal);
        Assert.DoesNotContain(
            "#if defined(HELENGINE_WINDOWS_HAS_GENERATED_RUNTIME_MODULE_REGISTRATION) && !defined(HELENGINE_WINDOWS_HAS_PHYSICS3D_RUNTIME)",
            sourceCode,
            StringComparison.Ordinal);
        Assert.Contains(
            "#elif !defined(HELENGINE_WINDOWS_HAS_GENERATED_RUNTIME_MODULE_REGISTRATION)",
            sourceCode,
            StringComparison.Ordinal);

        int initializeIndex = sourceCode.IndexOf(
            "EngineCore->Initialize(EngineRenderManager3D, EngineRenderManager2D, EngineInputBackend, platformInfo, options);",
            StringComparison.Ordinal);
        int registrationIndex = sourceCode.IndexOf(
            "RegisterGeneratedRuntimeModules(EngineCore);",
            StringComparison.Ordinal);
        int startupSceneIndex = sourceCode.IndexOf("LoadPackagedStartupScene();", StringComparison.Ordinal);

        Assert.True(initializeIndex >= 0, "Core initialization call was not found.");
        Assert.True(registrationIndex > initializeIndex, "Generated runtime registration must follow Core initialization.");
        Assert.True(startupSceneIndex > registrationIndex, "Generated runtime registration must precede startup-scene loading.");
    }


}
