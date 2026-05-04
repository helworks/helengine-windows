using helengine.baseplatform.Definitions;

namespace helengine.windows.builder;

/// <summary>
/// Provides a small command-line entrypoint for the Windows builder assembly.
/// </summary>
public static class Program {
    /// <summary>
    /// Runs the builder smoke mode or prints the builder identity.
    /// </summary>
    /// <param name="args">Command-line arguments.</param>
    /// <returns>Zero on success.</returns>
    public static int Main(string[] args) {
        if (args.Length > 0 && string.Equals(args[0], "--describe", StringComparison.OrdinalIgnoreCase)) {
            WindowsPlatformAssetBuilder builder = new();
            Console.WriteLine(builder.Descriptor.BuilderId);
            Console.WriteLine(builder.Descriptor.TargetPlatformId);
            Console.WriteLine(builder.Definition.DisplayName);
            return 0;
        }

        if (args.Length > 0 && string.Equals(args[0], "--smoke-test", StringComparison.OrdinalIgnoreCase)) {
            Console.WriteLine("windows.builder smoke test entrypoint");
            return 0;
        }

        Console.WriteLine("helengine.windows.builder --describe | --smoke-test");
        return 0;
    }
}
