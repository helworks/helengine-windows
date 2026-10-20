using helengine;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Manifest;

namespace helengine.windows.builder.tests;

/// <summary>Verifies the supported Windows scalar pairs and the fixed GPU upload boundary.</summary>
public sealed class WindowsNumericPrecisionTests {
    /// <summary>Native startup sources bind Core/content/module data to the exact selected scalar contract.</summary>
    [Theory]
    [InlineData(32)]
    [InlineData(64)]
    public void WindowsPoliciesReachCoreAndModules(int bits) {
        PlatformNumericCapabilities capabilities = WindowsPlatformDefinitionFactory.Create().CodegenProfiles.Single().NumericCapabilities;
        capabilities.RequireSupported(new(bits, bits));
        Assert.Throws<NotSupportedException>(() => capabilities.RequireSupported(new(16, 16)));
        string root = @"C:\temp\numeric-precision\windows\manifest-fixture";
        PlatformBuildManifest manifest = new(2, "test", "1", "1", "windows", "1", "menu",
            [new("menu", "Menu", "menu.hasset", [], [new("cooked-relative-path", "menu.hasset")])], [], [],
            [new("game", "code/game", "test", [], [], new(bits, bits))], [], new("", []), [], PlatformBuildRuntimeFeatureManifest.Empty, new(bits, bits));
        new WindowsRuntimeNativeManifestWriter().Write(root, manifest, new Dictionary<string, string> { ["default-width"] = "640", ["default-height"] = "480" });
        string source = File.ReadAllText(Path.Combine(root, "runtime", "runtime_startup_manifest.cpp"));
        Assert.Contains("static_assert(HE_CPP_REAL_STORAGE_BITS == " + bits, source);
        Assert.Contains("static_assert(HE_CPP_REAL_ARITHMETIC_BITS == " + bits, source);
        Assert.Contains("static_assert(HE_CPP_REAL_REPRESENTATION_VERSION == 1", source);
    }

    /// <summary>Wide engine storage does not widen native shader vertex/uniform packets.</summary>
    [Fact]
    public void DoubleEnginePreservesGpuLayout() {
        Assert.Equal(8, System.Runtime.InteropServices.Marshal.SizeOf<GpuFloat2>());
        Assert.Equal(12, System.Runtime.InteropServices.Marshal.SizeOf<GpuFloat3>());
        Assert.Equal(16, System.Runtime.InteropServices.Marshal.SizeOf<GpuFloat4>());
        Assert.Equal(64, System.Runtime.InteropServices.Marshal.SizeOf<GpuFloat4x4>());
        Assert.Equal(16777216f, GpuFloat3.FromEngine(new float3(16777217d, 0, 0)).X);
    }
}
