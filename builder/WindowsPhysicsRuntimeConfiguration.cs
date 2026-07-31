namespace helengine.windows.builder;

/// <summary>
/// Resolves one authoritative Windows physics backend selection and its backend-specific numeric settings.
/// </summary>
public sealed class WindowsPhysicsRuntimeConfiguration {
    /// <summary>
    /// Stable identifier for the existing BEPU runtime backend.
    /// </summary>
    public const string BepuBackendId = "bepu";

    /// <summary>
    /// Stable identifier for the console-first HelPhysics runtime backend.
    /// </summary>
    public const string HelPhysicsBackendId = "helphysics";

    /// <summary>
    /// Stable code-generation setting identifier selecting the physics runtime backend.
    /// </summary>
    public const string BackendSettingId = "physics-runtime-backend";

    /// <summary>
    /// Stable code-generation setting identifier selecting HelPhysics fixed-step frequency.
    /// </summary>
    public const string FixedStepHertzSettingId = "physics-fixed-step-hertz";

    /// <summary>
    /// Stable code-generation setting identifier selecting HelPhysics velocity solve iterations.
    /// </summary>
    public const string VelocityIterationsSettingId = "physics-velocity-iterations";

    /// <summary>
    /// Stable code-generation setting identifier selecting HelPhysics penetration correction passes.
    /// </summary>
    public const string PenetrationCorrectionPassesSettingId = "physics-penetration-correction-passes";

    /// <summary>
    /// Stores the active HelPhysics fixed-step frequency, or zero while BEPU is selected.
    /// </summary>
    readonly int FixedStepHertzValue;

    /// <summary>
    /// Stores the active HelPhysics velocity iteration count, or zero while BEPU is selected.
    /// </summary>
    readonly int VelocityIterationCountValue;

    /// <summary>
    /// Stores the active HelPhysics penetration correction pass count, or zero while BEPU is selected.
    /// </summary>
    readonly int PenetrationCorrectionPassCountValue;

    /// <summary>
    /// Initializes one already validated backend configuration.
    /// </summary>
    /// <param name="backendId">Canonical backend identifier.</param>
    /// <param name="fixedStepHertz">Validated HelPhysics frequency or zero for BEPU.</param>
    /// <param name="velocityIterationCount">Validated HelPhysics iteration count or zero for BEPU.</param>
    /// <param name="penetrationCorrectionPassCount">Validated HelPhysics correction pass count or zero for BEPU.</param>
    WindowsPhysicsRuntimeConfiguration(
        string backendId,
        int fixedStepHertz,
        int velocityIterationCount,
        int penetrationCorrectionPassCount) {
        BackendId = backendId;
        FixedStepHertzValue = fixedStepHertz;
        VelocityIterationCountValue = velocityIterationCount;
        PenetrationCorrectionPassCountValue = penetrationCorrectionPassCount;
    }

    /// <summary>
    /// Gets the canonical active backend identifier.
    /// </summary>
    public string BackendId { get; }

    /// <summary>
    /// Gets whether this configuration selects HelPhysics and therefore owns numeric solver settings.
    /// </summary>
    public bool UsesHelPhysics => string.Equals(BackendId, HelPhysicsBackendId, StringComparison.Ordinal);

    /// <summary>
    /// Gets the exact HelPhysics fixed-step frequency.
    /// </summary>
    /// <exception cref="InvalidOperationException">Thrown when BEPU is selected and this value is not applicable.</exception>
    public int FixedStepHertz => GetRequiredHelPhysicsValue(FixedStepHertzValue, FixedStepHertzSettingId);

    /// <summary>
    /// Gets the exact HelPhysics sequential velocity iteration count.
    /// </summary>
    /// <exception cref="InvalidOperationException">Thrown when BEPU is selected and this value is not applicable.</exception>
    public int VelocityIterationCount => GetRequiredHelPhysicsValue(VelocityIterationCountValue, VelocityIterationsSettingId);

    /// <summary>
    /// Gets the exact HelPhysics penetration correction pass count.
    /// </summary>
    /// <exception cref="InvalidOperationException">Thrown when BEPU is selected and this value is not applicable.</exception>
    public int PenetrationCorrectionPassCount => GetRequiredHelPhysicsValue(
        PenetrationCorrectionPassCountValue,
        PenetrationCorrectionPassesSettingId);

    /// <summary>
    /// Resolves selected code-generation values into one strict backend-specific configuration.
    /// </summary>
    /// <param name="selectedCodegenOptionValues">Resolved code-generation values supplied by the editor build request.</param>
    /// <returns>Canonical BEPU or HelPhysics configuration.</returns>
    /// <exception cref="ArgumentNullException">Thrown when the selected option dictionary is null.</exception>
    /// <exception cref="InvalidOperationException">Thrown when an explicit backend or active HelPhysics number is invalid.</exception>
    public static WindowsPhysicsRuntimeConfiguration Resolve(
        IReadOnlyDictionary<string, string> selectedCodegenOptionValues) {
        if (selectedCodegenOptionValues == null) {
            throw new ArgumentNullException(nameof(selectedCodegenOptionValues));
        }

        if (!TryGetOptionValue(selectedCodegenOptionValues, BackendSettingId, out string backendId)) {
            return new WindowsPhysicsRuntimeConfiguration(BepuBackendId, 0, 0, 0);
        }
        if (string.IsNullOrWhiteSpace(backendId)) {
            throw new InvalidOperationException(
                $"Windows physics setting '{BackendSettingId}' must select a runtime backend.");
        }

        string normalizedBackendId = backendId.Trim();
        if (string.Equals(normalizedBackendId, BepuBackendId, StringComparison.OrdinalIgnoreCase)) {
            return new WindowsPhysicsRuntimeConfiguration(BepuBackendId, 0, 0, 0);
        }
        if (!string.Equals(normalizedBackendId, HelPhysicsBackendId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException(
                $"Windows physics runtime backend '{backendId}' is not supported. Expected '{BepuBackendId}' or '{HelPhysicsBackendId}'.");
        }

        int fixedStepHertz = ResolveRequiredPositiveIntegerOption(
            selectedCodegenOptionValues,
            FixedStepHertzSettingId);
        int velocityIterationCount = ResolveRequiredPositiveIntegerOption(
            selectedCodegenOptionValues,
            VelocityIterationsSettingId);
        int penetrationCorrectionPassCount = ResolveRequiredPositiveIntegerOption(
            selectedCodegenOptionValues,
            PenetrationCorrectionPassesSettingId);
        return new WindowsPhysicsRuntimeConfiguration(
            HelPhysicsBackendId,
            fixedStepHertz,
            velocityIterationCount,
            penetrationCorrectionPassCount);
    }

    /// <summary>
    /// Returns one validated HelPhysics value and rejects access while the selected backend does not own it.
    /// </summary>
    /// <param name="value">Stored backend-specific positive value or zero for BEPU.</param>
    /// <param name="settingId">Stable setting identifier used by the diagnostic.</param>
    /// <returns>The stored positive HelPhysics value.</returns>
    int GetRequiredHelPhysicsValue(int value, string settingId) {
        if (!UsesHelPhysics) {
            throw new InvalidOperationException(
                $"Windows physics setting '{settingId}' is available only when '{HelPhysicsBackendId}' is selected.");
        }

        return value;
    }

    /// <summary>
    /// Resolves one required unsigned decimal integer greater than zero from active HelPhysics settings.
    /// </summary>
    /// <param name="selectedCodegenOptionValues">Resolved code-generation option values.</param>
    /// <param name="settingId">Required numeric setting identifier.</param>
    /// <returns>Strictly positive parsed decimal integer.</returns>
    static int ResolveRequiredPositiveIntegerOption(
        IReadOnlyDictionary<string, string> selectedCodegenOptionValues,
        string settingId) {
        if (!TryGetOptionValue(selectedCodegenOptionValues, settingId, out string value) ||
            string.IsNullOrEmpty(value) ||
            !ContainsOnlyAsciiDigits(value) ||
            !int.TryParse(value, out int parsedValue) ||
            parsedValue <= 0) {
            throw new InvalidOperationException(
                $"Windows physics setting '{settingId}' must be a positive decimal integer.");
        }

        return parsedValue;
    }

    /// <summary>
    /// Determines whether every character is one ASCII decimal digit.
    /// </summary>
    /// <param name="value">Non-empty serialized numeric value.</param>
    /// <returns>True when every character lies between zero and nine; otherwise false.</returns>
    static bool ContainsOnlyAsciiDigits(string value) {
        for (int characterIndex = 0; characterIndex < value.Length; characterIndex++) {
            char character = value[characterIndex];
            if (character < '0' || character > '9') {
                return false;
            }
        }

        return true;
    }

    /// <summary>
    /// Resolves one option by exact or ordinal case-insensitive key comparison.
    /// </summary>
    /// <param name="selectedCodegenOptionValues">Resolved code-generation option values.</param>
    /// <param name="settingId">Stable setting identifier to locate.</param>
    /// <param name="value">Resolved serialized value when found.</param>
    /// <returns>True when the setting exists; otherwise false.</returns>
    static bool TryGetOptionValue(
        IReadOnlyDictionary<string, string> selectedCodegenOptionValues,
        string settingId,
        out string value) {
        bool foundValue = false;
        value = string.Empty;
        foreach (KeyValuePair<string, string> entry in selectedCodegenOptionValues) {
            if (string.Equals(entry.Key, settingId, StringComparison.OrdinalIgnoreCase)) {
                if (foundValue) {
                    throw new InvalidOperationException(
                        $"Windows physics setting '{settingId}' was provided more than once with different key casing.");
                }

                foundValue = true;
                value = entry.Value;
            }
        }

        return foundValue;
    }
}
