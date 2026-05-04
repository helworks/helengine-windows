using helengine.baseplatform.Builders;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.windows.builder;

/// <summary>
/// Implements the Windows platform asset builder contract.
/// </summary>
public sealed class WindowsPlatformAssetBuilder : IPlatformAssetBuilder {
    /// <summary>
    /// Native build executor used to invoke the Windows CMake build.
    /// </summary>
    readonly IWindowsNativeBuildExecutor NativeBuildExecutor;

    /// <summary>
    /// Initializes one Windows builder instance with the current platform metadata.
    /// </summary>
    public WindowsPlatformAssetBuilder() {
        NativeBuildExecutor = WindowsNativeBuildExecutor.Instance;
        Descriptor = CreateDescriptor();
        Definition = WindowsPlatformDefinitionFactory.Create();
    }

    /// <summary>
    /// Initializes one Windows builder instance with the current platform metadata and a custom native build executor.
    /// </summary>
    /// <param name="nativeBuildExecutor">Custom native build executor used by tests.</param>
    internal WindowsPlatformAssetBuilder(IWindowsNativeBuildExecutor nativeBuildExecutor) {
        NativeBuildExecutor = nativeBuildExecutor ?? WindowsNativeBuildExecutor.Instance;
        Descriptor = CreateDescriptor();
        Definition = WindowsPlatformDefinitionFactory.Create();
    }

    /// <summary>
    /// Gets the explicit builder descriptor for the Windows builder assembly.
    /// </summary>
    public PlatformBuilderDescriptor Descriptor { get; }

    /// <summary>
    /// Gets the typed Windows platform definition exposed to the editor.
    /// </summary>
    public PlatformDefinition Definition { get; }

    /// <summary>
    /// Returns the builder-owned cooked material payload for one Windows material schema request.
    /// </summary>
    /// <param name="request">Material translation request to process.</param>
    /// <returns>Cooked material payload and shader dependencies for the request.</returns>
    public PlatformMaterialCookResult CookMaterial(PlatformMaterialCookRequest request) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        }

        throw new InvalidOperationException("Windows material cooking is not implemented by this builder yet.");
    }

    /// <summary>
    /// Executes one Windows build request through the staged payload workspace.
    /// </summary>
    /// <param name="request">The resolved build request.</param>
    /// <param name="progressReporter">The progress reporter.</param>
    /// <param name="diagnosticReporter">The diagnostic reporter.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The final build report.</returns>
    public Task<PlatformBuildReport> BuildAsync(
        PlatformBuildRequest request,
        IPlatformBuildProgressReporter progressReporter,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        CancellationToken cancellationToken) {
        return WindowsBuildWorkspace.BuildAsync(request, progressReporter, diagnosticReporter, NativeBuildExecutor, cancellationToken);
    }

    /// <summary>
    /// Creates the standard builder descriptor used by both constructors.
    /// </summary>
    /// <returns>Builder descriptor for the Windows plugin.</returns>
    static PlatformBuilderDescriptor CreateDescriptor() {
        return new PlatformBuilderDescriptor(
            "helengine.windows.builder",
            "1.0.0",
            "windows",
            new EngineCompatibilityRange("1.0.0", "999.0.0"),
            new ManifestCompatibilityRange(1, 1),
            ["windows"],
            ["debug", "release"]);
    }
}


