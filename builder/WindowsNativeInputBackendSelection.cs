namespace helengine.windows.builder;

/// <summary>
/// Captures the native Windows gamepad backends that should be compiled into one player build.
/// </summary>
internal readonly record struct WindowsNativeInputBackendSelection {
    /// <summary>
    /// Initializes one backend-selection record.
    /// </summary>
    /// <param name="embedXInput">True when XInput support should be compiled into the player.</param>
    /// <param name="embedDirectInput">True when DirectInput support should be compiled into the player.</param>
    /// <param name="embedSteamInput">True when SteamInput support should be compiled into the player.</param>
    public WindowsNativeInputBackendSelection(bool embedXInput, bool embedDirectInput, bool embedSteamInput) {
        EmbedXInput = embedXInput;
        EmbedDirectInput = embedDirectInput;
        EmbedSteamInput = embedSteamInput;
    }

    /// <summary>
    /// Gets whether XInput support should be compiled into the player.
    /// </summary>
    public bool EmbedXInput { get; }

    /// <summary>
    /// Gets whether DirectInput support should be compiled into the player.
    /// </summary>
    public bool EmbedDirectInput { get; }

    /// <summary>
    /// Gets whether SteamInput support should be compiled into the player.
    /// </summary>
    public bool EmbedSteamInput { get; }
}
