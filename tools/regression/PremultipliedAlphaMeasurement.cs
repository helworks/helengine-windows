namespace helengine.windows.regression;

/// <summary>
/// Result of measuring an image against the premultiplied-alpha invariant: how many pixels violate
/// it, and what fraction of the image is fully transparent or fully opaque.
/// </summary>
public sealed class PremultipliedAlphaMeasurement {
    /// <summary>
    /// Initializes a premultiplied-alpha measurement.
    /// </summary>
    /// <param name="violatingPixels">Count of pixels whose B, G or R channel exceeds its own alpha channel.</param>
    /// <param name="transparentFraction">Fraction of pixels whose alpha channel is exactly 0.</param>
    /// <param name="opaqueFraction">Fraction of pixels whose alpha channel is exactly 255.</param>
    public PremultipliedAlphaMeasurement(long violatingPixels, double transparentFraction, double opaqueFraction) {
        ViolatingPixels = violatingPixels;
        TransparentFraction = transparentFraction;
        OpaqueFraction = opaqueFraction;
    }

    /// <summary>
    /// Gets the count of pixels whose B, G or R channel exceeds its own alpha channel, which is
    /// impossible for correctly premultiplied color.
    /// </summary>
    public long ViolatingPixels { get; }

    /// <summary>
    /// Gets the fraction of pixels whose alpha channel is exactly 0 (fully transparent).
    /// </summary>
    public double TransparentFraction { get; }

    /// <summary>
    /// Gets the fraction of pixels whose alpha channel is exactly 255 (fully opaque).
    /// </summary>
    public double OpaqueFraction { get; }
}
