namespace helengine.windows.regression;

/// <summary>
/// Command-line entry point for the Windows player regression comparison tool.
/// </summary>
public static class Program {
    /// <summary>
    /// Runs the regression command requested on the command line.
    /// </summary>
    /// <param name="args">Command-line arguments.</param>
    /// <returns>The command's exit code.</returns>
    public static int Main(string[] args) {
        return new RegressionCommandRunner().Run(args, Console.Out);
    }
}
