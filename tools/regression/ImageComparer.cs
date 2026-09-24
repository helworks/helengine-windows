namespace helengine.windows.regression;

/// <summary>
/// Compares an actual capture against an expected golden image on a per-pixel basis, tolerating
/// small per-channel noise and a small fraction of differing pixels, and renders a diff image.
/// </summary>
public static class ImageComparer {
    /// <summary>
    /// Maximum absolute per-channel (B, G, R) difference that is not counted as a differing pixel.
    /// </summary>
    public const int ChannelTolerance = 8;

    /// <summary>
    /// Maximum fraction of differing pixels, out of the total pixel count, that still counts as a
    /// passing comparison.
    /// </summary>
    public const double MaxDifferingFraction = 0.001;

    /// <summary>
    /// Compares two images pixel by pixel and produces the resulting comparison, including a diff
    /// image when the sizes match.
    /// </summary>
    /// <param name="expected">The golden reference image.</param>
    /// <param name="actual">The captured image being validated.</param>
    /// <returns>The comparison result.</returns>
    public static ImageComparison Compare(RegressionImage expected, RegressionImage actual) {
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
            if (deltaB > ChannelTolerance || deltaG > ChannelTolerance || deltaR > ChannelTolerance) {
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
