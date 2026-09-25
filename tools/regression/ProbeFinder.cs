namespace helengine.windows.regression;

/// <summary>
/// Finds stable probe pixels in a golden image for alpha-aware regression checks: the first pixel,
/// in row-major scan order, whose 5x5 neighbourhood is uniformly fully transparent, and the first
/// whose neighbourhood is uniformly fully opaque. Requiring a uniform neighbourhood kept away from
/// the image's edges ensures the probe survives small compositing or scaling noise at its exact
/// coordinates.
/// </summary>
public static class ProbeFinder {
    /// <summary>
    /// Half-width, in pixels, of the square neighbourhood that must be uniform around a probe pixel,
    /// and the minimum distance a probe pixel must keep from every image edge.
    /// </summary>
    public const int MarginPixels = 2;

    /// <summary>
    /// Finds the first pixel, in row-major scan order, whose 5x5 neighbourhood is entirely alpha 0.
    /// </summary>
    /// <param name="image">The image to scan, with its real (non-forced) alpha channel.</param>
    /// <returns>The probe candidate, or null when no fully transparent neighbourhood exists.</returns>
    public static ProbeCandidate FindTransparent(RegressionImage image) {
        return Find(image, 0);
    }

    /// <summary>
    /// Finds the first pixel, in row-major scan order, whose 5x5 neighbourhood is entirely alpha 255.
    /// </summary>
    /// <param name="image">The image to scan, with its real (non-forced) alpha channel.</param>
    /// <returns>The probe candidate, or null when no fully opaque neighbourhood exists.</returns>
    public static ProbeCandidate FindOpaque(RegressionImage image) {
        return Find(image, 255);
    }

    /// <summary>
    /// Scans an image in row-major order for the first pixel at least <see cref="MarginPixels"/> from
    /// every edge whose 5x5 neighbourhood is entirely the given alpha value.
    /// </summary>
    static ProbeCandidate Find(RegressionImage image, byte alphaValue) {
        for (int y = MarginPixels; y < image.Height - MarginPixels; y++) {
            for (int x = MarginPixels; x < image.Width - MarginPixels; x++) {
                if (HasUniformNeighbourhood(image, x, y, alphaValue)) {
                    return new ProbeCandidate(x, y);
                }
            }
        }

        return null;
    }

    /// <summary>
    /// Determines whether every pixel in the 5x5 neighbourhood centered on the given coordinates has
    /// the given alpha value.
    /// </summary>
    static bool HasUniformNeighbourhood(RegressionImage image, int centerX, int centerY, byte alphaValue) {
        for (int y = centerY - MarginPixels; y <= centerY + MarginPixels; y++) {
            for (int x = centerX - MarginPixels; x <= centerX + MarginPixels; x++) {
                int alphaOffset = (y * image.Width + x) * 4 + 3;
                if (image.Bgra[alphaOffset] != alphaValue) {
                    return false;
                }
            }
        }

        return true;
    }
}
