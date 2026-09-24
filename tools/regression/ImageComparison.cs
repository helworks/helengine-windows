namespace helengine.windows.regression;

/// <summary>
/// Result of comparing an actual capture against an expected golden image, including the
/// differing pixel count, fraction and a rendered diff image.
/// </summary>
public sealed class ImageComparison {
    /// <summary>
    /// Initializes a comparison result.
    /// </summary>
    /// <param name="sizesMatch">Whether the two images had the same width and height.</param>
    /// <param name="differingPixels">Count of pixels whose B, G or R channel exceeded the tolerance.</param>
    /// <param name="totalPixels">Total pixel count of the compared images.</param>
    /// <param name="differingFraction">Fraction of pixels that differed.</param>
    /// <param name="passed">Whether the comparison is within the accepted regression threshold.</param>
    /// <param name="diffImage">Rendered diff image, or null when the image sizes did not match.</param>
    public ImageComparison(bool sizesMatch, long differingPixels, long totalPixels, double differingFraction, bool passed, RegressionImage diffImage) {
        SizesMatch = sizesMatch;
        DifferingPixels = differingPixels;
        TotalPixels = totalPixels;
        DifferingFraction = differingFraction;
        Passed = passed;
        DiffImage = diffImage;
    }

    /// <summary>
    /// Gets whether the two compared images had the same width and height.
    /// </summary>
    public bool SizesMatch { get; }

    /// <summary>
    /// Gets the count of pixels whose B, G or R channel differed by more than the tolerance.
    /// </summary>
    public long DifferingPixels { get; }

    /// <summary>
    /// Gets the total pixel count of the compared images.
    /// </summary>
    public long TotalPixels { get; }

    /// <summary>
    /// Gets the fraction of pixels that differed, in the range [0, 1].
    /// </summary>
    public double DifferingFraction { get; }

    /// <summary>
    /// Gets whether the comparison is within the accepted regression threshold.
    /// </summary>
    public bool Passed { get; }

    /// <summary>
    /// Gets the rendered diff image: magenta for differing pixels, greyscale luminance of the
    /// actual image otherwise. Null only when the image sizes did not match.
    /// </summary>
    public RegressionImage DiffImage { get; }
}
