namespace helengine.windows.builder;

/// <summary>
/// Names where an assets-only (editor Play) Windows build stages its cooked content. The staged tree has the same layout
/// as the shipped Windows output root (scenes, models, materials and imported textures under <c>cooked/</c>), so the
/// desktop player reads it with the same content-relative paths the native Windows runtime uses.
/// </summary>
public static class WindowsPlayerContentLayout {
    /// <summary>
    /// Cooked content root of an assets-only build, relative to the builder working root. Play builds stage here instead
    /// of the user's output directory, so pressing Play never clears or overwrites a shipped Windows build.
    /// </summary>
    public const string ContentRootRelativePath = "windows-staging";
}
