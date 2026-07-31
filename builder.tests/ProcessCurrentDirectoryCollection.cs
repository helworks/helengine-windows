namespace helengine.windows.builder.tests;

/// <summary>
/// Serializes tests that temporarily change the process-wide current directory so their project roots cannot interfere with one another.
/// </summary>
[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class ProcessCurrentDirectoryCollection {
    /// <summary>
    /// Stable xUnit collection name shared by every fixture that mutates the process-wide current directory.
    /// </summary>
    public const string Name = "Process current directory";
}
