namespace helengine.windows.regression;

/// <summary>
/// Detects blank (stuck-on-a-single-color) captures, which usually indicate the Windows player
/// failed to render a frame rather than that the frame genuinely matched a solid-color golden.
/// </summary>
public static class BlankFrameDetector {
    /// <summary>
    /// Minimum fraction of pixels that must exactly match the top-left reference pixel's B, G and
    /// R channels for an image to be considered blank.
    /// </summary>
    public const double BlankFraction = 0.999;

    /// <summary>
    /// Determines whether an image is blank: whether at least <see cref="BlankFraction"/> of its
    /// pixels exactly match the top-left pixel's B, G and R channels (alpha is ignored).
    /// </summary>
    /// <param name="image">The image to inspect.</param>
    /// <returns>True when the image is considered blank.</returns>
    public static bool IsBlank(RegressionImage image) {
        long totalPixels = (long)image.Width * image.Height;
        if (totalPixels == 0) {
            return true;
        }

        byte referenceB = image.Bgra[0];
        byte referenceG = image.Bgra[1];
        byte referenceR = image.Bgra[2];
        long matching = 0;
        for (int offset = 0; offset < image.Bgra.Length; offset += 4) {
            if (image.Bgra[offset] == referenceB && image.Bgra[offset + 1] == referenceG && image.Bgra[offset + 2] == referenceR) {
                matching++;
            }
        }

        double matchingFraction = (double)matching / totalPixels;
        return matchingFraction >= BlankFraction;
    }
}
