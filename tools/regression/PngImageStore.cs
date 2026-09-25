namespace helengine.windows.regression;

using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

/// <summary>
/// Saves and loads <see cref="RegressionImage"/> pixel data as PNG golden files, using
/// System.Drawing's Format32bppArgb pixel format so the in-memory byte order matches
/// <see cref="BmpImageReader"/>'s BGRA convention exactly. <see cref="Load"/> composites the decoded
/// PNG through <see cref="Graphics.DrawImageUnscaled"/>, which is safe only when every pixel is
/// opaque; <see cref="SaveWithAlpha"/> and <see cref="LoadWithAlpha"/> instead copy raw ARGB bytes
/// with no drawing step, for an exact byte round trip of overlay-mode captures whose alpha is
/// meaningful, including fully transparent pixels that still carry a non-zero color.
/// </summary>
public static class PngImageStore {
    /// <summary>
    /// Saves a regression image as a PNG file, writing its BGRA bytes exactly as given. Use this for
    /// normal-window-mode captures, whose alpha is already forced opaque by <see cref="BmpImageReader.Read"/>.
    /// </summary>
    /// <param name="image">The image to save.</param>
    /// <param name="path">Destination PNG file path.</param>
    public static void Save(RegressionImage image, string path) {
        WriteBitmap(image, path);
    }

    /// <summary>
    /// Saves a regression image as a PNG file, keeping every pixel's real alpha byte exactly. Use
    /// this for overlay-mode captures, whose alpha channel is meaningful premultiplied alpha.
    /// </summary>
    /// <param name="image">The image to save.</param>
    /// <param name="path">Destination PNG file path.</param>
    public static void SaveWithAlpha(RegressionImage image, string path) {
        WriteBitmap(image, path);
    }

    /// <summary>
    /// Loads a PNG golden file into a regression image, forcing a 32-bit ARGB conversion so the
    /// resulting BGRA bytes are directly comparable to a BMP capture read via
    /// <see cref="BmpImageReader"/>. This composites the decoded pixels through
    /// <see cref="Graphics.DrawImageUnscaled"/>, which only preserves color exactly when every
    /// source pixel is opaque; use <see cref="LoadWithAlpha"/> for a golden with meaningful alpha.
    /// </summary>
    /// <param name="path">Path to the PNG file to load.</param>
    /// <returns>The decoded image with top-down BGRA pixel data and full opacity.</returns>
    public static RegressionImage Load(string path) {
        using Bitmap source = new(path);
        using Bitmap bitmap = new(source.Width, source.Height, PixelFormat.Format32bppArgb);
        using (Graphics graphics = Graphics.FromImage(bitmap)) {
            graphics.DrawImageUnscaled(source, 0, 0);
        }

        Rectangle bounds = new(0, 0, bitmap.Width, bitmap.Height);
        BitmapData data = bitmap.LockBits(bounds, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        try {
            int stride = bitmap.Width * 4;
            byte[] bgra = new byte[stride * bitmap.Height];
            for (int y = 0; y < bitmap.Height; y++) {
                IntPtr sourceRow = data.Scan0 + y * data.Stride;
                Marshal.Copy(sourceRow, bgra, y * stride, stride);
            }

            return new RegressionImage(bitmap.Width, bitmap.Height, bgra);
        } finally {
            bitmap.UnlockBits(data);
        }
    }

    /// <summary>
    /// Loads a PNG golden file into a regression image by copying its raw ARGB bytes directly, with
    /// no compositing step, so alpha 0 pixels keep their exact color instead of losing it to
    /// <see cref="Graphics"/> alpha blending. Use this for a golden saved with
    /// <see cref="SaveWithAlpha"/>.
    /// </summary>
    /// <param name="path">Path to the PNG file to load.</param>
    /// <returns>The decoded image with top-down BGRA pixel data and its exact alpha bytes.</returns>
    /// <exception cref="InvalidDataException">
    /// Thrown when the decoded PNG is not a 32-bit ARGB image, so its bytes cannot be copied
    /// directly without a compositing conversion.
    /// </exception>
    public static RegressionImage LoadWithAlpha(string path) {
        using Bitmap source = new(path);
        if (source.PixelFormat != PixelFormat.Format32bppArgb) {
            throw new InvalidDataException($"'{path}' is not a 32-bit ARGB PNG (pixel format {source.PixelFormat}); it cannot be loaded without a compositing conversion.");
        }

        Rectangle bounds = new(0, 0, source.Width, source.Height);
        BitmapData data = source.LockBits(bounds, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        try {
            int stride = source.Width * 4;
            byte[] bgra = new byte[stride * source.Height];
            for (int y = 0; y < source.Height; y++) {
                IntPtr sourceRow = data.Scan0 + y * data.Stride;
                Marshal.Copy(sourceRow, bgra, y * stride, stride);
            }

            return new RegressionImage(source.Width, source.Height, bgra);
        } finally {
            source.UnlockBits(data);
        }
    }

    /// <summary>
    /// Writes a regression image's exact BGRA bytes into a new 32-bit ARGB bitmap and saves it as a
    /// PNG file. Shared by <see cref="Save"/> and <see cref="SaveWithAlpha"/>, which differ only in
    /// name to make the caller's intent about alpha explicit; the write itself never alters alpha.
    /// </summary>
    static void WriteBitmap(RegressionImage image, string path) {
        using Bitmap bitmap = new(image.Width, image.Height, PixelFormat.Format32bppArgb);
        Rectangle bounds = new(0, 0, image.Width, image.Height);
        BitmapData data = bitmap.LockBits(bounds, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        try {
            int stride = image.Width * 4;
            for (int y = 0; y < image.Height; y++) {
                IntPtr destinationRow = data.Scan0 + y * data.Stride;
                Marshal.Copy(image.Bgra, y * stride, destinationRow, stride);
            }
        } finally {
            bitmap.UnlockBits(data);
        }

        bitmap.Save(path, ImageFormat.Png);
    }
}
