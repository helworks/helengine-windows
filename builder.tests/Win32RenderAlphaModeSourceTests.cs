using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the premultiplied-alpha rendering path of the Windows render bridge: the <c>Win32RenderAlphaMode</c> flag
/// lives in its own header, is applied once at startup from the resolved window mode, and selects the premultiplied
/// clears and blend states only under its Premultiplied branch, while every normal-mode (Straight) clear and
/// <c>AlphaBlendState</c> bind keeps today's exact text and the 3D pass keeps inheriting the 2D blend state.
/// </summary>
public sealed class Win32RenderAlphaModeSourceTests {
    /// <summary>
    /// The exact normal-mode 3D camera clear-color expression the renderer used before overlay mode existed.
    /// </summary>
    const string StraightCameraClearColorExpression =
        "float4 clearColor = clearSettings.get_ClearColorEnabled() ? clearSettings.get_ClearColor() : float4(0.0f, 0.0f, 0.0f, 1.0f);";

    /// <summary>
    /// Matches a Premultiplied/Straight branch around one 2D bind site whose Straight arm is today's exact
    /// <c>AlphaBlendState</c> bind call.
    /// </summary>
    const string TwoDimensionalBindBranchPattern =
        @"const float blendFactor\[\] = \{ 0\.0f, 0\.0f, 0\.0f, 0\.0f \};\s*"
        + @"if \(AlphaMode == Win32RenderAlphaMode::Premultiplied\) \{\s*"
        + @"context->OMSetBlendState\(PremultipliedDestinationBlendState\.Get\(\), blendFactor, 0xFFFFFFFFu\);\s*"
        + @"\} else \{\s*"
        + @"context->OMSetBlendState\(AlphaBlendState\.Get\(\), blendFactor, 0xFFFFFFFFu\);\s*"
        + @"\}";

    /// <summary>
    /// Verifies the alpha-mode flag is one enum in its own header with exactly the Straight and Premultiplied values.
    /// </summary>
    [Fact]
    public void Win32RenderAlphaMode_is_a_single_enum_in_its_own_header() {
        string header = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_alpha_mode.hpp");

        Assert.Contains("enum class Win32RenderAlphaMode {", header, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(header, @"\benum\s+class\s+\w+"));
        Assert.Matches(new Regex(@"Straight,\s*(///[^\n]*\n\s*)*Premultiplied\s*\};"), header);
    }

    /// <summary>
    /// Verifies both bridges expose a one-time alpha-mode configuration that rejects a second call and that rendering
    /// refuses to start before it was applied, instead of silently defaulting.
    /// </summary>
    [Fact]
    public void Render_bridges_configure_the_alpha_mode_exactly_once() {
        string header = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.hpp");
        string source = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        Assert.Contains("#include \"platform/windows/win32/win32_render_alpha_mode.hpp\"", header, StringComparison.Ordinal);
        Assert.Contains("#include \"platform/windows/win32/win32_overlay_background.hpp\"", header, StringComparison.Ordinal);
        Assert.Contains("void ConfigureAlphaMode(Win32RenderAlphaMode alphaMode, Win32OverlayBackground overlayBackground);", header, StringComparison.Ordinal);
        Assert.Contains("void ConfigureAlphaMode(Win32RenderAlphaMode alphaMode);", header, StringComparison.Ordinal);

        string configure3D = ExtractMethodBody(source, "void Win32RenderManager3D::ConfigureAlphaMode(");
        string configure2D = ExtractMethodBody(source, "void Win32RenderManager2D::ConfigureAlphaMode(");
        foreach (string configureBody in new[] { configure3D, configure2D }) {
            Assert.Matches(new Regex(@"if \(IsAlphaModeConfigured\) \{\s*throw std::logic_error\("), configureBody);
            Assert.Contains("IsAlphaModeConfigured = true;", configureBody, StringComparison.Ordinal);
        }
        Assert.Contains("OverlayBackground = overlayBackground;", configure3D, StringComparison.Ordinal);

        string draw3D = ExtractMethodBody(source, "void Win32RenderManager3D::Draw(");
        string render2D = ExtractMethodBody(source, "void Win32RenderManager2D::RenderCamera(");
        Assert.Matches(new Regex(@"if \(!IsAlphaModeConfigured\) \{\s*throw std::logic_error\("), draw3D);
        Assert.Matches(new Regex(@"if \(!IsAlphaModeConfigured\) \{\s*throw std::logic_error\("), render2D);
    }

    /// <summary>
    /// Verifies the application derives the alpha mode from the resolved window mode and applies it to both bridges
    /// right after constructing them and before the engine core can render.
    /// </summary>
    [Fact]
    public void Win32Application_applies_the_alpha_mode_once_before_engine_initialization() {
        string source = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        string alphaModeLine =
            "Win32RenderAlphaMode renderAlphaMode = WindowModeSettings->GetWindowMode() == Win32WindowMode::Overlay ? Win32RenderAlphaMode::Premultiplied : Win32RenderAlphaMode::Straight;";
        string configure2DLine = "EngineRenderManager2D->ConfigureAlphaMode(renderAlphaMode);";
        string configure3DLine = "EngineRenderManager3D->ConfigureAlphaMode(renderAlphaMode, WindowModeSettings->GetOverlayBackground());";
        int construct3DIndex = source.IndexOf("EngineRenderManager3D = new Win32RenderManager3D(*Bootstrap, *EngineRenderManager2D);", StringComparison.Ordinal);
        int alphaModeIndex = source.IndexOf(alphaModeLine, StringComparison.Ordinal);
        int configure2DIndex = source.IndexOf(configure2DLine, StringComparison.Ordinal);
        int configure3DIndex = source.IndexOf(configure3DLine, StringComparison.Ordinal);
        int initializeIndex = source.IndexOf("EngineCore->Initialize(EngineRenderManager3D, EngineRenderManager2D", StringComparison.Ordinal);

        Assert.True(construct3DIndex >= 0);
        Assert.True(alphaModeIndex > construct3DIndex);
        Assert.True(configure2DIndex > alphaModeIndex);
        Assert.True(configure3DIndex > alphaModeIndex);
        Assert.True(initializeIndex > configure2DIndex);
        Assert.True(initializeIndex > configure3DIndex);
        Assert.Equal(2, Regex.Matches(source, Regex.Escape("->ConfigureAlphaMode(")).Count);
    }

    /// <summary>
    /// Verifies the first camera's clear keeps today's exact color expression and clear call as the Straight arm and
    /// only the Premultiplied arm clears to the premultiplied color.
    /// </summary>
    [Fact]
    public void Camera_clear_keeps_the_straight_text_and_premultiplies_only_in_premultiplied_mode() {
        string source = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.cpp");
        string renderCamera = ExtractMethodBody(source, "void Win32RenderManager3D::RenderCamera(");

        Assert.Contains(StraightCameraClearColorExpression, renderCamera, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(
                Regex.Escape(StraightCameraClearColorExpression) + @"\s*"
                + @"const float clearColorValues\[\] = \{ clearColor\.X, clearColor\.Y, clearColor\.Z, clearColor\.W \};\s*"
                + @"if \(AlphaMode == Win32RenderAlphaMode::Premultiplied\) \{\s*"
                + @"float4 premultipliedClearColor = ResolvePremultipliedClearColor\(clearColor\);\s*"
                + @"const float premultipliedClearColorValues\[\] = \{ premultipliedClearColor\.X, premultipliedClearColor\.Y, premultipliedClearColor\.Z, premultipliedClearColor\.W \};\s*"
                + @"context->ClearRenderTargetView\(renderTargetView, premultipliedClearColorValues\);\s*"
                + @"\} else \{\s*"
                + @"context->ClearRenderTargetView\(renderTargetView, clearColorValues\);\s*"
                + @"\}"),
            renderCamera);
    }

    /// <summary>
    /// Verifies the fallback clears keep their three exact call sites and today's clear call as the Straight arm, with
    /// the premultiplied color used only in Premultiplied mode.
    /// </summary>
    [Fact]
    public void Fallback_clear_keeps_the_straight_text_and_premultiplies_only_in_premultiplied_mode() {
        string source = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.cpp");
        string draw = ExtractMethodBody(source, "void Win32RenderManager3D::Draw(");
        string clearBackBuffer = ExtractMethodBody(source, "void Win32RenderManager3D::ClearBackBuffer(");

        Assert.Equal(3, Regex.Matches(draw, Regex.Escape("ClearBackBuffer(0.0f, 0.0f, 0.0f, 1.0f);")).Count);
        Assert.Matches(
            new Regex(
                @"const float clearColor\[\] = \{ red, green, blue, alpha \};\s*"
                + @"context->OMSetRenderTargets\(1, &renderTargetView, depthStencilView\);\s*"
                + @"if \(AlphaMode == Win32RenderAlphaMode::Premultiplied\) \{\s*"
                + @"float4 premultipliedClearColor = ResolvePremultipliedClearColor\(float4\(red, green, blue, alpha\)\);\s*"
                + @"const float premultipliedClearColorValues\[\] = \{ premultipliedClearColor\.X, premultipliedClearColor\.Y, premultipliedClearColor\.Z, premultipliedClearColor\.W \};\s*"
                + @"context->ClearRenderTargetView\(renderTargetView, premultipliedClearColorValues\);\s*"
                + @"\} else \{\s*"
                + @"context->ClearRenderTargetView\(renderTargetView, clearColor\);\s*"
                + @"\}"),
            clearBackBuffer);
    }

    /// <summary>
    /// Verifies the premultiplied clear color is (0,0,0,0) for the transparent background and (r*a, g*a, b*a, a) of
    /// the requested color for the camera background.
    /// </summary>
    [Fact]
    public void Premultiplied_clear_color_is_transparent_or_the_premultiplied_camera_color() {
        string header = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.hpp");
        string source = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.cpp");

        Assert.Contains("float4 ResolvePremultipliedClearColor(float4 color) const;", header, StringComparison.Ordinal);
        string resolve = ExtractMethodBody(source, "float4 Win32RenderManager3D::ResolvePremultipliedClearColor(");
        Assert.Matches(
            new Regex(@"if \(OverlayBackground == Win32OverlayBackground::Transparent\) \{\s*return float4\(0\.0f, 0\.0f, 0\.0f, 0\.0f\);\s*\}"),
            resolve);
        Assert.Contains("double alpha = static_cast<double>(color.W);", resolve, StringComparison.Ordinal);
        Assert.Contains("static_cast<float>(static_cast<double>(color.X) * alpha)", resolve, StringComparison.Ordinal);
        Assert.Contains("static_cast<float>(static_cast<double>(color.Y) * alpha)", resolve, StringComparison.Ordinal);
        Assert.Contains("static_cast<float>(static_cast<double>(color.Z) * alpha)", resolve, StringComparison.Ordinal);
        Assert.Contains("color.W);", resolve, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the 2D premultiplied-destination blend state is color SRC_ALPHA/INV_SRC_ALPHA and alpha
    /// ONE/INV_SRC_ALPHA, while today's AlphaBlendState description keeps its exact values.
    /// </summary>
    [Fact]
    public void Two_dimensional_blend_states_have_the_expected_descriptions() {
        string header = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.hpp");
        string source = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.cpp");
        string ensure2D = ExtractMethodBody(source, "void Win32RenderManager2D::EnsurePipelineState(");

        Assert.Contains("Microsoft::WRL::ComPtr<ID3D11BlendState> PremultipliedDestinationBlendState;", header, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(
                @"blendDescription\.RenderTarget\[0\]\.BlendEnable = TRUE;\s*"
                + @"blendDescription\.RenderTarget\[0\]\.SrcBlend = D3D11_BLEND_SRC_ALPHA;\s*"
                + @"blendDescription\.RenderTarget\[0\]\.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;\s*"
                + @"blendDescription\.RenderTarget\[0\]\.BlendOp = D3D11_BLEND_OP_ADD;\s*"
                + @"blendDescription\.RenderTarget\[0\]\.SrcBlendAlpha = D3D11_BLEND_ONE;\s*"
                + @"blendDescription\.RenderTarget\[0\]\.DestBlendAlpha = D3D11_BLEND_ZERO;\s*"
                + @"blendDescription\.RenderTarget\[0\]\.BlendOpAlpha = D3D11_BLEND_OP_ADD;\s*"
                + @"blendDescription\.RenderTarget\[0\]\.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;\s*"
                + @"ThrowIfFailed\(\s*device->CreateBlendState\(&blendDescription, AlphaBlendState\.GetAddressOf\(\)\),"),
            ensure2D);
        Assert.Matches(
            new Regex(
                @"premultipliedDestinationBlendDescription\.RenderTarget\[0\]\.BlendEnable = TRUE;\s*"
                + @"premultipliedDestinationBlendDescription\.RenderTarget\[0\]\.SrcBlend = D3D11_BLEND_SRC_ALPHA;\s*"
                + @"premultipliedDestinationBlendDescription\.RenderTarget\[0\]\.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;\s*"
                + @"premultipliedDestinationBlendDescription\.RenderTarget\[0\]\.BlendOp = D3D11_BLEND_OP_ADD;\s*"
                + @"premultipliedDestinationBlendDescription\.RenderTarget\[0\]\.SrcBlendAlpha = D3D11_BLEND_ONE;\s*"
                + @"premultipliedDestinationBlendDescription\.RenderTarget\[0\]\.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;\s*"
                + @"premultipliedDestinationBlendDescription\.RenderTarget\[0\]\.BlendOpAlpha = D3D11_BLEND_OP_ADD;\s*"
                + @"premultipliedDestinationBlendDescription\.RenderTarget\[0\]\.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;\s*"
                + @"ThrowIfFailed\(\s*device->CreateBlendState\(&premultipliedDestinationBlendDescription, PremultipliedDestinationBlendState\.GetAddressOf\(\)\),"),
            ensure2D);
    }

    /// <summary>
    /// Verifies both 2D bind sites (textured quads including text, and rounded rects) keep today's AlphaBlendState bind
    /// as the Straight arm and select the premultiplied-destination state only in Premultiplied mode, and that the only
    /// other site binding the premultiplied state is the 3D-pass bind helper.
    /// </summary>
    [Fact]
    public void Two_dimensional_bind_sites_select_the_premultiplied_state_only_in_premultiplied_mode() {
        string source = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.cpp");
        string quad = ExtractMethodBody(source, "void Win32RenderManager2D::PrepareTexturedQuadDraw(");
        string roundedRect = ExtractMethodBody(source, "void Win32RenderManager2D::PrepareRoundedRectDraw(");

        Assert.Matches(new Regex(TwoDimensionalBindBranchPattern), quad);
        Assert.Matches(new Regex(TwoDimensionalBindBranchPattern), roundedRect);
        Assert.Equal(2, Regex.Matches(source, Regex.Escape("context->OMSetBlendState(AlphaBlendState.Get(), blendFactor, 0xFFFFFFFFu);")).Count);
        Assert.Equal(3, Regex.Matches(source, Regex.Escape("OMSetBlendState(PremultipliedDestinationBlendState.Get()")).Count);
    }

    /// <summary>
    /// Verifies each camera's 3D pass binds the same premultiplied-destination src-over state as the 2D draws, only
    /// under the Premultiplied branch (no else arm, so the Straight 3D pass keeps its inherited state), between the
    /// per-camera state setup and the queue visit, and that the former overlay-opaque state no longer exists.
    /// </summary>
    [Fact]
    public void Three_dimensional_pass_binds_the_premultiplied_destination_state_only_in_premultiplied_mode() {
        string header = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.hpp");
        string source = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_render_bridge.cpp");
        string renderCamera = ExtractMethodBody(source, "void Win32RenderManager3D::RenderCamera(");
        string bindHelper = ExtractMethodBody(source, "void Win32RenderManager2D::BindPremultipliedDestinationBlendState(");

        Assert.Contains("void BindPremultipliedDestinationBlendState();", header, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(
                @"if \(AlphaMode != Win32RenderAlphaMode::Premultiplied\) \{\s*throw std::logic_error\([^;]*;\s*\}\s*"
                + @"EnsurePipelineState\(\);\s*"
                + @"Bootstrap\.GetDeviceContext\(\)->OMSetBlendState\(PremultipliedDestinationBlendState\.Get\(\), nullptr, 0xFFFFFFFFu\);"),
            bindHelper);

        Regex bind = new Regex(
            @"if \(AlphaMode == Win32RenderAlphaMode::Premultiplied\) \{\s*"
            + @"RenderManager2DBridge->BindPremultipliedDestinationBlendState\(\);\s*"
            + @"\}\s*\n");
        Match bindMatch = bind.Match(renderCamera);
        Assert.True(bindMatch.Success, "The Premultiplied-only premultiplied-destination bind was not found in RenderCamera.");
        Assert.DoesNotContain("} else {", renderCamera.Substring(bindMatch.Index, bindMatch.Length + 16), StringComparison.Ordinal);
        int visitIndex = renderCamera.IndexOf("renderQueue->VisitOrdered(this);", StringComparison.Ordinal);
        int prepareShadowIndex = renderCamera.IndexOf("PrepareShadowState(camera, visibleLights);", StringComparison.Ordinal);
        Assert.True(bindMatch.Index > prepareShadowIndex);
        Assert.True(bindMatch.Index < visitIndex);
        Assert.DoesNotContain("OMSetBlendState(", renderCamera, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(source, Regex.Escape("RenderManager2DBridge->BindPremultipliedDestinationBlendState();")));
        Assert.DoesNotContain("OverlayOpaque", header, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("OverlayOpaque", source, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("D3D11_BLEND_BLEND_FACTOR", source, StringComparison.Ordinal);
    }

    /// <summary>
    /// Returns the text of one C++ method definition from its qualified name up to the closing brace that ends a
    /// namespace-level member definition.
    /// </summary>
    /// <param name="source">Full C++ source text; CRLF line endings are normalized to LF first.</param>
    /// <param name="qualifiedNamePrefix">Qualified method name including the opening parenthesis.</param>
    /// <returns>The method definition text.</returns>
    static string ExtractMethodBody(string source, string qualifiedNamePrefix) {
        string normalizedSource = source.Replace("\r\n", "\n", StringComparison.Ordinal);
        int startIndex = normalizedSource.IndexOf(qualifiedNamePrefix, StringComparison.Ordinal);
        Assert.True(startIndex >= 0, $"Method definition '{qualifiedNamePrefix}' was not found.");
        int endIndex = normalizedSource.IndexOf("\n    }\n", startIndex, StringComparison.Ordinal);
        Assert.True(endIndex >= 0, $"End of method definition '{qualifiedNamePrefix}' was not found.");
        return normalizedSource.Substring(startIndex, endIndex - startIndex);
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
