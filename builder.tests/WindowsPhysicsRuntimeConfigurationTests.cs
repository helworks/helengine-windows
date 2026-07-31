using helengine.windows.builder;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies Windows physics backend settings resolve into one strict authoritative configuration.
/// </summary>
public sealed class WindowsPhysicsRuntimeConfigurationTests {
    /// <summary>
    /// Ensures an absent backend preserves existing Windows builds as BEPU without requiring HelPhysics numeric settings.
    /// </summary>
    [Fact]
    public void Resolve_WhenBackendIsAbsent_ReturnsBepuWithoutHelPhysicsNumbers() {
        WindowsPhysicsRuntimeConfiguration configuration = WindowsPhysicsRuntimeConfiguration.Resolve(
            new Dictionary<string, string>(StringComparer.Ordinal));

        Assert.Equal("bepu", configuration.BackendId);
        Assert.False(configuration.UsesHelPhysics);
        Assert.Throws<InvalidOperationException>(() => configuration.FixedStepHertz);
        Assert.Throws<InvalidOperationException>(() => configuration.VelocityIterationCount);
        Assert.Throws<InvalidOperationException>(() => configuration.PenetrationCorrectionPassCount);
    }

    /// <summary>
    /// Ensures BEPU selection ignores HelPhysics-only numeric values because they are not active configuration.
    /// </summary>
    [Fact]
    public void Resolve_WhenBepuIsSelected_DoesNotValidateHelPhysicsNumbers() {
        WindowsPhysicsRuntimeConfiguration configuration = WindowsPhysicsRuntimeConfiguration.Resolve(
            new Dictionary<string, string>(StringComparer.Ordinal) {
                ["physics-runtime-backend"] = "bepu",
                ["physics-fixed-step-hertz"] = "not-used",
                ["physics-velocity-iterations"] = "not-used",
                ["physics-penetration-correction-passes"] = "not-used"
            });

        Assert.Equal("bepu", configuration.BackendId);
        Assert.False(configuration.UsesHelPhysics);
    }

    /// <summary>
    /// Ensures the isolated demo build's explicit HelPhysics settings resolve exactly.
    /// </summary>
    [Fact]
    public void Resolve_WhenHelPhysicsSettingsAreValid_ReturnsExactConfiguration() {
        WindowsPhysicsRuntimeConfiguration configuration = WindowsPhysicsRuntimeConfiguration.Resolve(
            new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
                ["PHYSICS-RUNTIME-BACKEND"] = "HELPHYSICS",
                ["PHYSICS-FIXED-STEP-HERTZ"] = "20",
                ["PHYSICS-VELOCITY-ITERATIONS"] = "4",
                ["PHYSICS-PENETRATION-CORRECTION-PASSES"] = "1"
            });

        Assert.Equal("helphysics", configuration.BackendId);
        Assert.True(configuration.UsesHelPhysics);
        Assert.Equal(20, configuration.FixedStepHertz);
        Assert.Equal(4, configuration.VelocityIterationCount);
        Assert.Equal(1, configuration.PenetrationCorrectionPassCount);
    }

    /// <summary>
    /// Ensures backend values use the same surrounding-whitespace normalization as generated-core selection.
    /// </summary>
    [Fact]
    public void Resolve_WhenBackendHasSurroundingWhitespace_NormalizesSelection() {
        Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.Ordinal) {
            ["physics-runtime-backend"] = " helphysics ",
            ["physics-fixed-step-hertz"] = "20",
            ["physics-velocity-iterations"] = "4",
            ["physics-penetration-correction-passes"] = "1"
        };

        WindowsPhysicsRuntimeConfiguration configuration = WindowsPhysicsRuntimeConfiguration.Resolve(values);

        Assert.Equal("helphysics", configuration.BackendId);
    }

    /// <summary>
    /// Ensures case-variant duplicate setting identifiers are rejected instead of allowing dictionary order to select a value.
    /// </summary>
    /// <param name="duplicateSettingId">Case-variant setting identifier duplicated in the dictionary.</param>
    [Theory]
    [InlineData("PHYSICS-RUNTIME-BACKEND")]
    [InlineData("PHYSICS-FIXED-STEP-HERTZ")]
    public void Resolve_WhenSettingIdIsDuplicatedWithDifferentCasing_Throws(string duplicateSettingId) {
        Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.Ordinal) {
            ["physics-runtime-backend"] = "helphysics",
            ["physics-fixed-step-hertz"] = "20",
            ["physics-velocity-iterations"] = "4",
            ["physics-penetration-correction-passes"] = "1",
            [duplicateSettingId] = "20"
        };

        Assert.Throws<InvalidOperationException>(() => WindowsPhysicsRuntimeConfiguration.Resolve(values));
    }

    /// <summary>
    /// Ensures unknown and explicitly blank backend values cannot silently fall back to BEPU.
    /// </summary>
    /// <param name="backendValue">Invalid explicit backend selection.</param>
    [Theory]
    [InlineData("")]
    [InlineData("unknown")]
    public void Resolve_WhenBackendIsInvalid_Throws(string backendValue) {
        Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.Ordinal) {
            ["physics-runtime-backend"] = backendValue
        };

        Assert.Throws<InvalidOperationException>(() => WindowsPhysicsRuntimeConfiguration.Resolve(values));
    }

    /// <summary>
    /// Ensures every active HelPhysics numeric option must contain only a strictly positive decimal integer.
    /// </summary>
    /// <param name="settingId">Setting receiving the malformed value.</param>
    /// <param name="settingValue">Malformed or non-positive serialized value.</param>
    [Theory]
    [InlineData("physics-fixed-step-hertz", "0")]
    [InlineData("physics-fixed-step-hertz", "-20")]
    [InlineData("physics-velocity-iterations", "4.0")]
    [InlineData("physics-velocity-iterations", " 4")]
    [InlineData("physics-penetration-correction-passes", "invalid")]
    [InlineData("physics-penetration-correction-passes", "")]
    public void Resolve_WhenHelPhysicsNumberIsInvalid_Throws(string settingId, string settingValue) {
        Dictionary<string, string> values = new Dictionary<string, string>(StringComparer.Ordinal) {
            ["physics-runtime-backend"] = "helphysics",
            ["physics-fixed-step-hertz"] = "20",
            ["physics-velocity-iterations"] = "4",
            ["physics-penetration-correction-passes"] = "1",
            [settingId] = settingValue
        };

        Assert.Throws<InvalidOperationException>(() => WindowsPhysicsRuntimeConfiguration.Resolve(values));
    }
}
