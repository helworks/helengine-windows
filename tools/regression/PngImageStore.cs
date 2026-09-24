namespace helengine.windows.regression;

using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

/// <summary>
/// Saves and loads <see cref="RegressionImage"/> pixel data as PNG golden files, using
/// System.Drawing's Format32bppArgb pixel format so the in-memory byte order matches
/// <see cref="BmpImageReader"/>'s BGRA convention exactly.
/// </summary>
public static class PngImageStore {
    /// <summary>
    /// Saves a regression image as a PNG file.
    /// </summary>
    /// <param name="image">The image to save.</param>
    /// <param name="path">Destination PNG file path.</param>
    public static void Save(RegressionImage image, string path) {
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

    /// <summary>
    /// Loads a PNG golden file into a regression image, forcing a 32-bit ARGB conversion so the
    /// resulting BGRA bytes are directly comparable to a BMP capture read via
    /// <see cref="BmpImageReader"/>.
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
}
