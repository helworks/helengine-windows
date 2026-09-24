namespace helengine.windows.regression;

/// <summary>
/// Immutable in-memory representation of a captured or golden regression image, stored as
/// top-down 32-bit BGRA pixel data (four bytes per pixel, blue first).
/// </summary>
public sealed class RegressionImage {
    /// <summary>
    /// Initializes a regression image from its pixel dimensions and raw top-down BGRA bytes.
    /// </summary>
    /// <param name="width">Image width in pixels.</param>
    /// <param name="height">Image height in pixels.</param>
    /// <param name="bgra">Top-down pixel data, four bytes per pixel in B, G, R, A order.</param>
    public RegressionImage(int width, int height, byte[] bgra) {
        long expectedLength = (long)width * height * 4;
        if (bgra.Length != expectedLength) {
            throw new ArgumentException($"Expected {expectedLength} BGRA bytes for a {width}x{height} image but received {bgra.Length}.", nameof(bgra));
        }

        Width = width;
        Height = height;
        Bgra = bgra;
    }

    /// <summary>
    /// Gets the image width in pixels.
    /// </summary>
    public int Width { get; }

    /// <summary>
    /// Gets the image height in pixels.
    /// </summary>
    public int Height { get; }

    /// <summary>
    /// Gets the top-down pixel data, four bytes per pixel in B, G, R, A order.
    /// </summary>
    public byte[] Bgra { get; }
}
