namespace helengine.windows.regression;

/// <summary>
/// Measures whether an overlay capture's color data is correctly premultiplied by its alpha channel,
/// and how much of the frame is fully transparent versus fully opaque. An overlay window's swap
/// chain carries meaningful premultiplied alpha, so a correctly rendered frame must never store a
/// color channel brighter than its own alpha, and a healthy capture of real UI content should show a
/// visible amount of both fully transparent background and fully opaque UI.
/// </summary>
public static class PremultipliedAlphaChecker {
    /// <summary>
    /// Measures an image's premultiplied-alpha violations and its transparent and opaque pixel
    /// fractions.
    /// </summary>
    /// <param name="image">The image to measure, with its real (non-forced) alpha channel.</param>
    /// <returns>The violation count and the transparent and opaque pixel fractions.</returns>
    public static PremultipliedAlphaMeasurement Measure(RegressionImage image) {
        long totalPixels = (long)image.Width * image.Height;
        long violating = 0;
        long transparent = 0;
        long opaque = 0;
        for (int offset = 0; offset < image.Bgra.Length; offset += 4) {
            byte b = image.Bgra[offset];
            byte g = image.Bgra[offset + 1];
            byte r = image.Bgra[offset + 2];
            byte a = image.Bgra[offset + 3];
            if (b > a || g > a || r > a) {
                violating++;
            }

            if (a == 0) {
                transparent++;
            } else if (a == 255) {
                opaque++;
            }
        }

        double transparentFraction = totalPixels == 0 ? 0 : (double)transparent / totalPixels;
        double opaqueFraction = totalPixels == 0 ? 0 : (double)opaque / totalPixels;
        return new PremultipliedAlphaMeasurement(violating, transparentFraction, opaqueFraction);
    }
}
