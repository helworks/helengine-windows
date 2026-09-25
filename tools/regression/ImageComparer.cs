namespace helengine.windows.regression;

/// <summary>
/// Compares an actual capture against an expected golden image on a per-pixel basis, tolerating
/// small per-channel noise and a small fraction of differing pixels, and renders a diff image.
/// <see cref="Compare"/> only looks at the B, G and R channels, for normal-window-mode captures
/// whose alpha is undefined; <see cref="CompareRgba"/> also compares the alpha channel, for
/// overlay-mode captures whose alpha is meaningful premultiplied alpha.
/// </summary>
public static class ImageComparer {
    /// <summary>
    /// Maximum absolute per-channel difference that is not counted as a differing pixel.
    /// </summary>
    public const int ChannelTolerance = 8;

    /// <summary>
    /// Maximum fraction of differing pixels, out of the total pixel count, that still counts as a
    /// passing comparison.
    /// </summary>
    public const double MaxDifferingFraction = 0.001;

    /// <summary>
    /// Compares two images pixel by pixel over their B, G and R channels and produces the resulting
    /// comparison, including a diff image when the sizes match. Alpha never affects the result: it is
    /// undefined for a normal-window-mode capture.
    /// </summary>
    /// <param name="expected">The golden reference image.</param>
    /// <param name="actual">The captured image being validated.</param>
    /// <returns>The comparison result.</returns>
    public static ImageComparison Compare(RegressionImage expected, RegressionImage actual) {
        return CompareChannels(expected, actual, false);
    }

    /// <summary>
    /// Compares two images pixel by pixel over all four B, G, R and A channels, using the same
    /// tolerance and passing-fraction rules as <see cref="Compare"/>, and produces the resulting
    /// comparison including a diff image when the sizes match. Use this for overlay-mode captures,
    /// whose alpha channel is meaningful premultiplied alpha rather than an undefined value.
    /// </summary>
    /// <param name="expected">The golden reference image.</param>
    /// <param name="actual">The captured image being validated.</param>
    /// <returns>The comparison result.</returns>
    public static ImageComparison CompareRgba(RegressionImage expected, RegressionImage actual) {
        return CompareChannels(expected, actual, true);
    }

    /// <summary>
    /// Shared pixel-comparison loop for <see cref="Compare"/> and <see cref="CompareRgba"/>: compares
    /// B, G and R always, plus A when <paramref name="includeAlpha"/> is set, against the same
    /// per-channel tolerance and differing-fraction threshold, and renders an opaque diff image
    /// (magenta for a differing pixel, greyscale luminance of the actual pixel otherwise).
    /// </summary>
    static ImageComparison CompareChannels(RegressionImage expected, RegressionImage actual, bool includeAlpha) {
        if (expected.Width != actual.Width || expected.Height != actual.Height) {
            return new ImageComparison(false, 0, 0, 0, false, null);
        }

        long totalPixels = (long)actual.Width * actual.Height;
        long differing = 0;
        byte[] diff = new byte[actual.Bgra.Length];
        for (int offset = 0; offset < actual.Bgra.Length; offset += 4) {
            int deltaB = Math.Abs(expected.Bgra[offset] - actual.Bgra[offset]);
            int deltaG = Math.Abs(expected.Bgra[offset + 1] - actual.Bgra[offset + 1]);
            int deltaR = Math.Abs(expected.Bgra[offset + 2] - actual.Bgra[offset + 2]);
            int deltaA = includeAlpha ? Math.Abs(expected.Bgra[offset + 3] - actual.Bgra[offset + 3]) : 0;
            if (deltaB > ChannelTolerance || deltaG > ChannelTolerance || deltaR > ChannelTolerance || deltaA > ChannelTolerance) {
                differing++;
                diff[offset] = 255;
                diff[offset + 1] = 0;
                diff[offset + 2] = 255;
                diff[offset + 3] = 255;
            } else {
                byte luminance = (byte)((actual.Bgra[offset] * 29 + actual.Bgra[offset + 1] * 150 + actual.Bgra[offset + 2] * 77) >> 8);
                diff[offset] = luminance;
                diff[offset + 1] = luminance;
                diff[offset + 2] = luminance;
                diff[offset + 3] = 255;
            }
        }

        double differingFraction = totalPixels == 0 ? 0 : (double)differing / totalPixels;
        bool passed = differingFraction <= MaxDifferingFraction;
        RegressionImage diffImage = new(actual.Width, actual.Height, diff);
        return new ImageComparison(true, differing, totalPixels, differingFraction, passed, diffImage);
    }
}
