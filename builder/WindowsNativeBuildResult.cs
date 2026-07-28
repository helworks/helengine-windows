namespace helengine.windows.builder;

/// <summary>
/// Records the native player artifacts produced by one Windows CMake build.
/// </summary>
internal sealed class WindowsNativeBuildResult {
    /// <summary>
    /// Initializes the artifact paths produced by a completed native Windows build.
    /// </summary>
    /// <param name="executablePath">Absolute path to the produced player executable.</param>
    /// <param name="pdbPath">Absolute path to the produced PDB, or an empty string when the profile does not produce one.</param>
    public WindowsNativeBuildResult(string executablePath, string pdbPath) {
        if (string.IsNullOrWhiteSpace(executablePath)) {
            throw new ArgumentException("Executable path must be provided.", nameof(executablePath));
        } else if (pdbPath == null) {
            throw new ArgumentNullException(nameof(pdbPath));
        }

        ExecutablePath = executablePath;
        PdbPath = pdbPath;
    }

    /// <summary>
    /// Gets the absolute path to the produced native player executable.
    /// </summary>
    public string ExecutablePath { get; }

    /// <summary>
    /// Gets the absolute path to the produced PDB, or an empty string when no PDB was produced.
    /// </summary>
    public string PdbPath { get; }
}
