namespace helengine.windows.regression;

/// <summary>
/// A single pixel location found by <see cref="ProbeFinder"/>: the coordinates of a pixel whose
/// neighbourhood is uniformly fully transparent or uniformly fully opaque, suitable as a stable
/// sample point for alpha-aware regression checks.
/// </summary>
public sealed class ProbeCandidate {
    /// <summary>
    /// Initializes a probe candidate at the given pixel coordinates.
    /// </summary>
    /// <param name="x">Zero-based column of the probe pixel.</param>
    /// <param name="y">Zero-based row of the probe pixel.</param>
    public ProbeCandidate(int x, int y) {
        X = x;
        Y = y;
    }

    /// <summary>
    /// Gets the zero-based column of the probe pixel.
    /// </summary>
    public int X { get; }

    /// <summary>
    /// Gets the zero-based row of the probe pixel.
    /// </summary>
    public int Y { get; }
}
