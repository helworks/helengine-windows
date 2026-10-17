using System.Globalization;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Player;

namespace helengine.windows.builder;

/// <summary>Declares and validates the Windows Play render dimensions, independently of packaged build settings.</summary>
public static class WindowsPlayerSettings {
    /// <summary>Setting id for the width of the player's offscreen render target.</summary>
    public const string RenderWidthId = "render-width";

    /// <summary>Setting id for the height of the player's offscreen render target.</summary>
    public const string RenderHeightId = "render-height";

    /// <summary>Creates the settings displayed and resolved by the existing Play Settings dialog.</summary>
    /// <returns>Required width and height declarations with the approved 1280 by 720 defaults.</returns>
    public static PlatformSettingDefinition[] CreateDefinitions() {
        return [
            new PlatformSettingDefinition(RenderWidthId, "Render Width", PlatformSettingKind.Text, "1280", true, []),
            new PlatformSettingDefinition(RenderHeightId, "Render Height", PlatformSettingKind.Text, "720", true, [])
        ];
    }

    /// <summary>Reads the resolved Windows dimensions without substituting missing or invalid values.</summary>
    /// <param name="settings">Resolved session settings supplied by the editor.</param>
    /// <returns>The selected framebuffer size with square pixels.</returns>
    public static PlayerRenderSize Resolve(PlayerPlaySettings settings) {
        if (settings == null) {
            throw new ArgumentNullException(nameof(settings));
        } else if (settings.HasVideoMode) {
            throw new InvalidOperationException("Windows Play uses render-width and render-height, not a console video mode.");
        }

        return new PlayerRenderSize(ReadDimension(settings.PlatformValues, RenderWidthId), ReadDimension(settings.PlatformValues, RenderHeightId), 1d);
    }

    /// <summary>Validates one required integer dimension against the shared player's render-target limits.</summary>
    /// <param name="values">Resolved platform-specific values.</param>
    /// <param name="settingId">Dimension being read, also included in diagnostics.</param>
    /// <returns>The validated dimension in pixels.</returns>
    static int ReadDimension(IReadOnlyDictionary<string, string> values, string settingId) {
        if (!values.TryGetValue(settingId, out string text)
            || !int.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out int dimension)
            || dimension < 1 || dimension > PlayerRenderSize.MaximumDimension) {
            throw new InvalidOperationException($"Windows Play setting '{settingId}' must be an integer from 1 to {PlayerRenderSize.MaximumDimension}; received '{text}'.");
        }

        return dimension;
    }
}
