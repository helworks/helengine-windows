namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the runtime player profile carries optional idle-throttle fields, that a malformed idle field is
/// reported as a configuration error instead of being silently repaired like a resolution problem, and that the
/// native build compiles the new configuration-error source.
/// </summary>
public sealed class RuntimePlayerProfileSourceTests {
    /// <summary>
    /// Verifies RuntimePlayerProfile gains the four idle-throttle members with the documented default values, so
    /// the existing <c>{w, h}</c> aggregate initialization in the loader keeps compiling unchanged.
    /// </summary>
    [Fact]
    public void RuntimePlayerProfile_declares_idle_fields_with_default_values() {
        string profileHeader = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile.hpp");

        Assert.Contains("bool IdleThrottleEnabled = false;", profileHeader, StringComparison.Ordinal);
        Assert.Contains("int IdleAfterMilliseconds = 500;", profileHeader, StringComparison.Ordinal);
        Assert.Contains("int IdleFramesPerSecond = 10;", profileHeader, StringComparison.Ordinal);
        Assert.Contains("bool IdleFieldsPresent = false;", profileHeader, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the new RuntimePlayerProfileConfigurationError type derives from std::runtime_error with a
    /// message constructor, and that the native build compiles its source file.
    /// </summary>
    [Fact]
    public void RuntimePlayerProfileConfigurationError_is_a_runtime_error_and_is_compiled() {
        string errorHeader = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_configuration_error.hpp");
        string errorSource = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_configuration_error.cpp");
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("class RuntimePlayerProfileConfigurationError : public std::runtime_error {", errorHeader, StringComparison.Ordinal);
        Assert.Contains("explicit RuntimePlayerProfileConfigurationError(const std::string& message);", errorHeader, StringComparison.Ordinal);
        Assert.Contains(": std::runtime_error(message)", errorSource, StringComparison.Ordinal);
        Assert.Contains("src/platform/windows/runtime/runtime_player_profile_configuration_error.cpp", cmakeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the loader declares the three optional-field parsers and the idle validator with the documented
    /// signatures.
    /// </summary>
    [Fact]
    public void RuntimePlayerProfileLoader_declares_idle_parsing_members() {
        string loaderHeader = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.hpp");

        Assert.Contains("bool TryParseOptionalInteger(const std::string& json, const char* propertyName, int& value) const;", loaderHeader, StringComparison.Ordinal);
        Assert.Contains("bool TryParseOptionalBoolean(const std::string& json, const char* propertyName, bool& value) const;", loaderHeader, StringComparison.Ordinal);
        Assert.Contains("void ValidateIdleFields(const RuntimePlayerProfile& profile) const;", loaderHeader, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the loader reads all three documented idle JSON property names.
    /// </summary>
    [Fact]
    public void RuntimePlayerProfileLoader_reads_all_three_idle_property_names() {
        string loaderSource = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.cpp");

        Assert.Contains("\"idleThrottleEnabled\"", loaderSource, StringComparison.Ordinal);
        Assert.Contains("\"idleAfterMilliseconds\"", loaderSource, StringComparison.Ordinal);
        Assert.Contains("\"idleFramesPerSecond\"", loaderSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies TryParseOptionalBoolean accepts only the exact literals <c>true</c> and <c>false</c>: it must
    /// reject anything else by throwing the configuration error rather than defaulting silently.
    /// </summary>
    [Fact]
    public void TryParseOptionalBoolean_accepts_only_exact_true_or_false_literals() {
        string loaderSource = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.cpp");

        int methodStartIndex = loaderSource.IndexOf(
            "bool RuntimePlayerProfileLoader::TryParseOptionalBoolean(",
            StringComparison.Ordinal);
        Assert.True(methodStartIndex >= 0, "TryParseOptionalBoolean must be defined in the loader source.");

        int methodEndIndex = loaderSource.IndexOf(
            "RuntimePlayerProfileLoader::ValidateIdleFields(",
            methodStartIndex,
            StringComparison.Ordinal);
        Assert.True(methodEndIndex > methodStartIndex, "ValidateIdleFields must follow TryParseOptionalBoolean in the loader source.");

        string methodBody = loaderSource.Substring(methodStartIndex, methodEndIndex - methodStartIndex);
        Assert.Contains("\"true\"", methodBody, StringComparison.Ordinal);
        Assert.Contains("\"false\"", methodBody, StringComparison.Ordinal);
        Assert.Contains("throw RuntimePlayerProfileConfigurationError(", methodBody, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies TryParseOptionalInteger and TryParseOptionalBoolean share one FindPropertyValueStartIndex helper
    /// for the token/colon/whitespace lookup, and that ParseRequiredInteger (the resolution-field parser) is left
    /// untouched by it, since resolution fields must keep their old default-path behavior unchanged.
    /// </summary>
    [Fact]
    public void Optional_parsers_share_FindPropertyValueStartIndex_and_leave_ParseRequiredInteger_untouched() {
        string loaderHeader = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.hpp");
        string loaderSource = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.cpp");

        Assert.Contains(
            "bool FindPropertyValueStartIndex(const std::string& json, const char* propertyName, std::size_t& valueStartIndex) const;",
            loaderHeader,
            StringComparison.Ordinal);
        Assert.Contains(
            "bool RuntimePlayerProfileLoader::FindPropertyValueStartIndex(const std::string& json, const char* propertyName, std::size_t& valueStartIndex) const {",
            loaderSource,
            StringComparison.Ordinal);

        int integerMethodStartIndex = loaderSource.IndexOf(
            "bool RuntimePlayerProfileLoader::TryParseOptionalInteger(",
            StringComparison.Ordinal);
        Assert.True(integerMethodStartIndex >= 0, "TryParseOptionalInteger must be defined in the loader source.");
        int integerMethodEndIndex = loaderSource.IndexOf(
            "RuntimePlayerProfileLoader::TryParseOptionalBoolean(",
            integerMethodStartIndex,
            StringComparison.Ordinal);
        Assert.True(integerMethodEndIndex > integerMethodStartIndex, "TryParseOptionalBoolean must follow TryParseOptionalInteger in the loader source.");
        string integerMethodBody = loaderSource.Substring(integerMethodStartIndex, integerMethodEndIndex - integerMethodStartIndex);
        Assert.Contains("FindPropertyValueStartIndex(", integerMethodBody, StringComparison.Ordinal);

        int booleanMethodStartIndex = integerMethodEndIndex;
        int booleanMethodEndIndex = loaderSource.IndexOf(
            "RuntimePlayerProfileLoader::ValidateIdleFields(",
            booleanMethodStartIndex,
            StringComparison.Ordinal);
        Assert.True(booleanMethodEndIndex > booleanMethodStartIndex, "ValidateIdleFields must follow TryParseOptionalBoolean in the loader source.");
        string booleanMethodBody = loaderSource.Substring(booleanMethodStartIndex, booleanMethodEndIndex - booleanMethodStartIndex);
        Assert.Contains("FindPropertyValueStartIndex(", booleanMethodBody, StringComparison.Ordinal);

        int requiredMethodStartIndex = loaderSource.IndexOf(
            "int RuntimePlayerProfileLoader::ParseRequiredInteger(",
            StringComparison.Ordinal);
        Assert.True(requiredMethodStartIndex >= 0, "ParseRequiredInteger must be defined in the loader source.");
        int requiredMethodEndIndex = loaderSource.IndexOf(
            "RuntimePlayerProfileLoader::FindPropertyValueStartIndex(",
            requiredMethodStartIndex,
            StringComparison.Ordinal);
        Assert.True(requiredMethodEndIndex > requiredMethodStartIndex, "FindPropertyValueStartIndex must follow ParseRequiredInteger in the loader source.");
        string requiredMethodBody = loaderSource.Substring(requiredMethodStartIndex, requiredMethodEndIndex - requiredMethodStartIndex);
        Assert.DoesNotContain("FindPropertyValueStartIndex(", requiredMethodBody, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies TryParseOptionalInteger rejects a fractional value (<c>12.5</c>) and trailing garbage after the
    /// digits (<c>15abc</c>) by requiring whitespace, a comma, a closing brace, or the end of the payload right
    /// after the digit run, instead of silently truncating to the leading digits.
    /// </summary>
    [Fact]
    public void TryParseOptionalInteger_rejects_fractional_and_trailing_garbage_values() {
        string loaderSource = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.cpp");

        int methodStartIndex = loaderSource.IndexOf(
            "bool RuntimePlayerProfileLoader::TryParseOptionalInteger(",
            StringComparison.Ordinal);
        Assert.True(methodStartIndex >= 0, "TryParseOptionalInteger must be defined in the loader source.");

        int methodEndIndex = loaderSource.IndexOf(
            "RuntimePlayerProfileLoader::TryParseOptionalBoolean(",
            methodStartIndex,
            StringComparison.Ordinal);
        Assert.True(methodEndIndex > methodStartIndex, "TryParseOptionalBoolean must follow TryParseOptionalInteger in the loader source.");

        string methodBody = loaderSource.Substring(methodStartIndex, methodEndIndex - methodStartIndex);
        Assert.Contains("json[trailingIndex] != ','", methodBody, StringComparison.Ordinal);
        Assert.Contains("json[trailingIndex] != '}'", methodBody, StringComparison.Ordinal);
        Assert.True(
            System.Text.RegularExpressions.Regex.Matches(methodBody, "throw RuntimePlayerProfileConfigurationError\\(").Count >= 3,
            "TryParseOptionalInteger must throw the configuration error for the non-digit case, the trailing-garbage case, and the unparseable case.");
    }

    /// <summary>
    /// Verifies TryParseOptionalBoolean rejects a matched literal immediately followed by anything other than
    /// whitespace, a comma, a closing brace, or the end of the payload (for example <c>true_</c> or
    /// <c>falsey</c>), instead of accepting any non-alphanumeric follow-on character.
    /// </summary>
    [Fact]
    public void TryParseOptionalBoolean_requires_a_clean_boundary_after_the_literal() {
        string loaderSource = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.cpp");

        int methodStartIndex = loaderSource.IndexOf(
            "bool RuntimePlayerProfileLoader::TryParseOptionalBoolean(",
            StringComparison.Ordinal);
        Assert.True(methodStartIndex >= 0, "TryParseOptionalBoolean must be defined in the loader source.");

        int methodEndIndex = loaderSource.IndexOf(
            "RuntimePlayerProfileLoader::ValidateIdleFields(",
            methodStartIndex,
            StringComparison.Ordinal);
        Assert.True(methodEndIndex > methodStartIndex, "ValidateIdleFields must follow TryParseOptionalBoolean in the loader source.");

        string methodBody = loaderSource.Substring(methodStartIndex, methodEndIndex - methodStartIndex);
        Assert.Contains("trueLiteralHasValidBoundary", methodBody, StringComparison.Ordinal);
        Assert.Contains("falseLiteralHasValidBoundary", methodBody, StringComparison.Ordinal);
        Assert.Contains("json[trueLiteralEndIndex] == ','", methodBody, StringComparison.Ordinal);
        Assert.Contains("json[trueLiteralEndIndex] == '}'", methodBody, StringComparison.Ordinal);
        Assert.Contains("json[falseLiteralEndIndex] == ','", methodBody, StringComparison.Ordinal);
        Assert.Contains("json[falseLiteralEndIndex] == '}'", methodBody, StringComparison.Ordinal);
        Assert.DoesNotContain("std::isalnum", methodBody, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies ValidateIdleFields rejects a non-positive idle-after duration and a frame rate outside 1..30 by
    /// throwing the configuration error.
    /// </summary>
    [Fact]
    public void ValidateIdleFields_rejects_out_of_range_values() {
        string loaderSource = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.cpp");

        int methodStartIndex = loaderSource.IndexOf(
            "void RuntimePlayerProfileLoader::ValidateIdleFields(",
            StringComparison.Ordinal);
        Assert.True(methodStartIndex >= 0, "ValidateIdleFields must be defined in the loader source.");

        int methodEndIndex = loaderSource.IndexOf(
            "RuntimePlayerProfileLoader::BuildProfileJson(",
            methodStartIndex,
            StringComparison.Ordinal);
        Assert.True(methodEndIndex > methodStartIndex, "BuildProfileJson must follow ValidateIdleFields in the loader source.");

        string methodBody = loaderSource.Substring(methodStartIndex, methodEndIndex - methodStartIndex);
        Assert.Contains("IdleAfterMilliseconds <= 0", methodBody, StringComparison.Ordinal);
        Assert.Contains("IdleFramesPerSecond < 1", methodBody, StringComparison.Ordinal);
        Assert.Contains("IdleFramesPerSecond > 30", methodBody, StringComparison.Ordinal);
        Assert.Equal(2, System.Text.RegularExpressions.Regex.Matches(methodBody, "throw RuntimePlayerProfileConfigurationError\\(").Count);
    }

    /// <summary>
    /// Verifies BuildProfileJson only emits the idle fields when IdleFieldsPresent is true, which is what keeps a
    /// seeded or repaired profile that never mentioned idle throttling byte-identical to the pre-idle-throttle
    /// format.
    /// </summary>
    [Fact]
    public void BuildProfileJson_guards_idle_output_with_IdleFieldsPresent() {
        string loaderSource = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.cpp");

        int methodStartIndex = loaderSource.IndexOf(
            "std::string RuntimePlayerProfileLoader::BuildProfileJson(",
            StringComparison.Ordinal);
        Assert.True(methodStartIndex >= 0, "BuildProfileJson must be defined in the loader source.");

        string methodBody = loaderSource.Substring(methodStartIndex);
        Assert.Contains("if (profile.IdleFieldsPresent) {", methodBody, StringComparison.Ordinal);
        Assert.Contains("\\\"idleThrottleEnabled\\\"", methodBody, StringComparison.Ordinal);
        Assert.Contains("\\\"idleAfterMilliseconds\\\"", methodBody, StringComparison.Ordinal);
        Assert.Contains("\\\"idleFramesPerSecond\\\"", methodBody, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies LoadOrCreateProfile parses and validates the idle fields before and outside the pre-existing
    /// resolution-repair <c>catch (const std::exception&amp;)</c> block, so a configuration error always escapes
    /// instead of being swallowed and rewritten with defaults like a resolution problem.
    /// </summary>
    [Fact]
    public void LoadOrCreateProfile_validates_idle_fields_before_the_resolution_repair_catch() {
        string loaderSource = ReadRepositoryFile("src", "platform", "windows", "runtime", "runtime_player_profile_loader.cpp");

        int methodStartIndex = loaderSource.IndexOf(
            "RuntimePlayerProfileLoader::LoadOrCreateProfile(",
            StringComparison.Ordinal);
        Assert.True(methodStartIndex >= 0, "LoadOrCreateProfile must be defined in the loader source.");

        int methodEndIndex = loaderSource.IndexOf(
            "RuntimePlayerProfileLoader::ResolveProfilePath(",
            methodStartIndex,
            StringComparison.Ordinal);
        Assert.True(methodEndIndex > methodStartIndex, "ResolveProfilePath must follow LoadOrCreateProfile in the loader source.");

        string methodBody = loaderSource.Substring(methodStartIndex, methodEndIndex - methodStartIndex);

        int idleParseIndex = methodBody.IndexOf("TryParseOptionalBoolean(", StringComparison.Ordinal);
        int idleValidateIndex = methodBody.IndexOf("ValidateIdleFields(", StringComparison.Ordinal);
        int configurationErrorCatchIndex = methodBody.IndexOf("catch (const RuntimePlayerProfileConfigurationError&)", StringComparison.Ordinal);
        int genericCatchIndex = methodBody.IndexOf("catch (const std::exception&)", StringComparison.Ordinal);

        Assert.True(idleParseIndex >= 0, "LoadOrCreateProfile must call TryParseOptionalBoolean directly.");
        Assert.True(idleValidateIndex >= 0, "LoadOrCreateProfile must call ValidateIdleFields directly.");
        Assert.True(configurationErrorCatchIndex >= 0, "LoadOrCreateProfile must re-throw RuntimePlayerProfileConfigurationError before the generic catch.");
        Assert.True(genericCatchIndex >= 0, "LoadOrCreateProfile must keep the generic resolution-repair catch.");

        Assert.True(idleParseIndex < genericCatchIndex, "Idle field parsing must appear before the resolution-repair catch.");
        Assert.True(idleValidateIndex < genericCatchIndex, "Idle field validation must appear before the resolution-repair catch.");
        Assert.True(configurationErrorCatchIndex < genericCatchIndex, "The configuration-error catch must appear before the generic resolution-repair catch so it wins.");
    }

    /// <summary>
    /// Verifies Win32Application::ResolveRuntimePlayerProfile catches RuntimePlayerProfileConfigurationError and
    /// converts it into a Win32ExitRequest with exit code 2, matching how other deliberate startup failures exit.
    /// </summary>
    [Fact]
    public void Win32Application_converts_configuration_error_into_exit_code_two() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        int methodStartIndex = applicationSource.IndexOf(
            "Win32Application::ResolveRuntimePlayerProfile(",
            StringComparison.Ordinal);
        Assert.True(methodStartIndex >= 0, "ResolveRuntimePlayerProfile must be defined in win32_application.cpp.");

        int catchIndex = applicationSource.IndexOf(
            "catch (const RuntimePlayerProfileConfigurationError&",
            methodStartIndex,
            StringComparison.Ordinal);
        Assert.True(catchIndex >= 0, "ResolveRuntimePlayerProfile must catch RuntimePlayerProfileConfigurationError.");

        int exitRequestIndex = applicationSource.IndexOf("Win32ExitRequest(2,", catchIndex, StringComparison.Ordinal);
        Assert.True(exitRequestIndex > catchIndex, "The configuration-error catch must throw Win32ExitRequest(2, ...).");

        Assert.Contains(
            "#include \"platform/windows/runtime/runtime_player_profile_configuration_error.hpp\"",
            applicationSource,
            StringComparison.Ordinal);
    }

    /// <summary>
    /// Reads a source file relative to the Windows native-player repository root.
    /// </summary>
    /// <param name="relativePathSegments">Path segments below the repository root.</param>
    /// <returns>The file text.</returns>
    static string ReadRepositoryFile(params string[] relativePathSegments) {
        string repositoryRootPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", ".."));
        string filePath = Path.Combine(repositoryRootPath, Path.Combine(relativePathSegments));
        if (!File.Exists(filePath)) {
            throw new FileNotFoundException($"Source file was not found under the Windows repository root '{repositoryRootPath}'.", filePath);
        }

        return File.ReadAllText(filePath);
    }
}
