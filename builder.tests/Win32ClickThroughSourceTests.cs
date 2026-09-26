using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the overlay window's per-pixel click-through: <c>DirectX11HitTestSampler</c> reads one back-buffer pixel
/// through a ring of three 1×1 staging textures without ever stalling, <c>Win32ClickThroughController</c> toggles
/// <c>WS_EX_TRANSPARENT</c> only when the state changes, <c>Win32OverlayHitTestController</c> owns both, applies the
/// alpha &lt; 8 threshold, remembers the probe alpha and watches the cursor for the idle throttle, and
/// <c>Win32Application</c> creates that controller only in overlay mode, calls it between Draw and Present, logs the
/// <c>HIT_TEST</c> line only for <c>--hit-test-probe</c> runs and feeds cursor movement over the overlay into the idle
/// throttle's activity tracker.
/// </summary>
public sealed class Win32ClickThroughSourceTests {
    /// <summary>
    /// Verifies the sampler keeps a ring of exactly three 1×1 BGRA staging textures and copies one pixel with
    /// <c>CopySubresourceRegion</c> only when the point is inside the client area.
    /// </summary>
    [Fact]
    public void DirectX11HitTestSampler_copies_one_pixel_into_a_ring_of_three_staging_textures() {
        string samplerHeader = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_hit_test_sampler.hpp");
        string samplerSource = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_hit_test_sampler.cpp");

        Assert.Contains("class DirectX11HitTestSampler {", samplerHeader, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(samplerHeader, @"\bclass\s+\w+\s*\{"));
        Assert.Contains("void Capture(int x, int y);", samplerHeader, StringComparison.Ordinal);
        Assert.Contains("bool TryReadLatestAlpha(int& alpha);", samplerHeader, StringComparison.Ordinal);
        Assert.Contains("static constexpr int RingSize = 3;", samplerHeader, StringComparison.Ordinal);
        Assert.Contains("std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, RingSize> StagingTextures;", samplerHeader, StringComparison.Ordinal);

        string constructorBody = ExtractMethodBody(samplerSource, "DirectX11HitTestSampler::DirectX11HitTestSampler(");
        Assert.Contains("stagingDesc.Width = 1;", constructorBody, StringComparison.Ordinal);
        Assert.Contains("stagingDesc.Height = 1;", constructorBody, StringComparison.Ordinal);
        Assert.Contains("stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;", constructorBody, StringComparison.Ordinal);
        Assert.Contains("stagingDesc.Usage = D3D11_USAGE_STAGING;", constructorBody, StringComparison.Ordinal);
        Assert.Contains("stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;", constructorBody, StringComparison.Ordinal);

        string captureBody = ExtractMethodBody(samplerSource, "void DirectX11HitTestSampler::Capture(");
        Assert.Matches(
            new Regex(@"if \(x < 0 \|\| y < 0 \|\| x >= Bootstrap\.GetWidth\(\) \|\| y >= Bootstrap\.GetHeight\(\)\) \{\s*return;\s*\}"),
            captureBody);
        int boundsIndex = captureBody.IndexOf("x >= Bootstrap.GetWidth()", StringComparison.Ordinal);
        int copyIndex = captureBody.IndexOf("CopySubresourceRegion(", StringComparison.Ordinal);
        Assert.True(copyIndex > boundsIndex, "CopySubresourceRegion must follow the client-area check.");
        Assert.Contains("NextSlot = (NextSlot + 1) % RingSize;", captureBody, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the readback drains the ring: it maps the pending slots from oldest to newest in a loop, each with
    /// <c>D3D11_MAP_FLAG_DO_NOT_WAIT</c>, stops at the first sample still in flight instead of stalling, returns the
    /// newest alpha it read (so a backlog cannot add permanent latency), and throws on every other failure with the
    /// shared HRESULT formatter.
    /// </summary>
    [Fact]
    public void DirectX11HitTestSampler_drains_completed_slots_to_the_newest_without_waiting() {
        string samplerSource = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_hit_test_sampler.cpp");

        string readBody = ExtractMethodBody(samplerSource, "bool DirectX11HitTestSampler::TryReadLatestAlpha(");
        int loopIndex = readBody.IndexOf("while (PendingCount > 0) {", StringComparison.Ordinal);
        int oldestSlotIndex = readBody.IndexOf("int oldestSlot = (NextSlot - PendingCount + RingSize) % RingSize;", StringComparison.Ordinal);
        int mapIndex = readBody.IndexOf("D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)", StringComparison.Ordinal);
        int decrementIndex = readBody.IndexOf("PendingCount--;", StringComparison.Ordinal);
        Assert.True(loopIndex >= 0, "TryReadLatestAlpha must loop over the pending slots.");
        Assert.True(oldestSlotIndex > loopIndex, "The oldest pending slot must be picked inside the loop.");
        Assert.True(mapIndex > oldestSlotIndex, "Each slot must be mapped with D3D11_MAP_FLAG_DO_NOT_WAIT inside the loop.");
        Assert.True(decrementIndex > mapIndex, "Each read slot must leave the pending count inside the loop.");
        Assert.Single(Regex.Matches(readBody, Regex.Escape("->Map(")));
        Assert.Matches(new Regex(@"if \(mapResult == DXGI_ERROR_WAS_STILL_DRAWING\) \{\s*break;\s*\}"), readBody);
        Assert.Contains("DirectX11HResultFormatter::ToHex(mapResult)", readBody, StringComparison.Ordinal);
        Assert.Contains("sampleRead = true;", readBody, StringComparison.Ordinal);
        Assert.Contains("return sampleRead;", readBody, StringComparison.Ordinal);
        Assert.DoesNotContain("D3D11_MAP_READ, 0,", samplerSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the controller toggles <c>WS_EX_TRANSPARENT</c> with <c>SetWindowLongPtrW</c> and a frame-changed,
    /// non-moving, non-activating <c>SetWindowPos</c>, returns early when the requested state is already applied, and
    /// starts in the click-through-on state the overlay window is created with, so the first opaque sample turns it off.
    /// </summary>
    [Fact]
    public void Win32ClickThroughController_toggles_transparent_only_on_change() {
        string controllerHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_click_through_controller.hpp");
        string controllerSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_click_through_controller.cpp");

        Assert.Contains("class Win32ClickThroughController {", controllerHeader, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(controllerHeader, @"\bclass\s+\w+\s*\{"));
        Assert.Contains("void Apply(bool clickThrough);", controllerHeader, StringComparison.Ordinal);
        Assert.Contains("bool IsClickThrough() const;", controllerHeader, StringComparison.Ordinal);

        string applyBody = ExtractMethodBody(controllerSource, "void Win32ClickThroughController::Apply(");
        Assert.Matches(new Regex(@"^[^{]*\{\s*if \(clickThrough == ClickThrough\) \{\s*return;\s*\}"), applyBody);
        Assert.Contains("WS_EX_TRANSPARENT", applyBody, StringComparison.Ordinal);
        Assert.Contains("SetWindowLongPtrW(WindowHandle, GWL_EXSTYLE,", applyBody, StringComparison.Ordinal);
        Assert.Contains(
            "SetWindowPos(WindowHandle, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)",
            applyBody, StringComparison.Ordinal);
        Assert.Contains("ClickThrough = clickThrough;", applyBody, StringComparison.Ordinal);

        string constructorBody = ExtractMethodBody(controllerSource, "Win32ClickThroughController::Win32ClickThroughController(");
        Assert.Contains("ClickThrough(true)", constructorBody, StringComparison.Ordinal);
        Assert.DoesNotContain("ClickThrough(false)", constructorBody, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the overlay hit-test logic lives in its own controller class, which owns the sampler and the
    /// click-through controller, keeps the alpha &lt; 8 threshold as a named constant and is compiled by the native build.
    /// </summary>
    [Fact]
    public void Win32OverlayHitTestController_owns_the_sampler_the_toggle_and_the_threshold() {
        string hitTestHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_overlay_hit_test_controller.hpp");
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("class Win32OverlayHitTestController {", hitTestHeader, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(hitTestHeader, @"\bclass\s+\w+\s*\{"));
        Assert.Contains(
            "Win32OverlayHitTestController(DirectX11Bootstrap& bootstrap, HWND windowHandle, const Win32CommandLineOptions& commandLineOptions);",
            hitTestHeader, StringComparison.Ordinal);
        Assert.Contains("void SampleFrame();", hitTestHeader, StringComparison.Ordinal);
        Assert.Contains("bool ObserveCursorForActivity();", hitTestHeader, StringComparison.Ordinal);
        Assert.Contains("bool HasProbeAlpha() const;", hitTestHeader, StringComparison.Ordinal);
        Assert.Contains("std::string DescribeProbeResult() const;", hitTestHeader, StringComparison.Ordinal);
        Assert.Contains("static constexpr int ClickThroughAlphaThreshold = 8;", hitTestHeader, StringComparison.Ordinal);
        Assert.Contains("DirectX11HitTestSampler HitTestSampler;", hitTestHeader, StringComparison.Ordinal);
        Assert.Contains("Win32ClickThroughController ClickThroughController;", hitTestHeader, StringComparison.Ordinal);
        Assert.Contains("POINT LastCursorPosition;", hitTestHeader, StringComparison.Ordinal);

        Assert.Contains("src/platform/windows/directx11/directx11_hit_test_sampler.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("src/platform/windows/win32/win32_click_through_controller.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("src/platform/windows/win32/win32_overlay_hit_test_controller.cpp", cmakeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the hit-test controller is created only in overlay mode, after the bootstrap it samples, and that the
    /// application no longer holds the sampler or the click-through controller itself.
    /// </summary>
    [Fact]
    public void Win32Application_constructs_hit_test_objects_only_in_overlay_mode() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string applicationHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.hpp");
        string hitTestSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_overlay_hit_test_controller.cpp");

        Assert.Contains("std::unique_ptr<Win32OverlayHitTestController> OverlayHitTestController;", applicationHeader, StringComparison.Ordinal);
        Assert.DoesNotContain("std::unique_ptr<DirectX11HitTestSampler>", applicationHeader, StringComparison.Ordinal);
        Assert.DoesNotContain("std::unique_ptr<Win32ClickThroughController>", applicationHeader, StringComparison.Ordinal);
        Assert.DoesNotContain("HitTestProbeAlpha", applicationHeader, StringComparison.Ordinal);

        string bootstrapBody = ExtractMethodBody(applicationSource, "Win32Application::CreateGraphicsBootstrap(");
        Assert.Matches(
            new Regex(
                @"if \(WindowModeSettings->GetWindowMode\(\) == Win32WindowMode::Overlay\) \{\s*"
                + @"OverlayHitTestController = std::make_unique<Win32OverlayHitTestController>\(\*Bootstrap, MainWindow->GetHandle\(\), CommandLineOptions\);\s*\}"),
            bootstrapBody);
        Assert.Single(Regex.Matches(applicationSource, @"std::make_unique<Win32OverlayHitTestController>"));
        Assert.DoesNotContain("DirectX11HitTestSampler", applicationSource, StringComparison.Ordinal);
        Assert.DoesNotContain("Win32ClickThroughController", applicationSource, StringComparison.Ordinal);

        string constructorBody = ExtractMethodBody(hitTestSource, "Win32OverlayHitTestController::Win32OverlayHitTestController(");
        Assert.Contains("HitTestSampler(bootstrap)", constructorBody, StringComparison.Ordinal);
        Assert.Contains("ClickThroughController(windowHandle)", constructorBody, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies RenderFrame hands the hit test to the controller between Draw and Present, and that the controller
    /// samples the probe point instead of the cursor when probing, captures before it reads back, and applies the alpha
    /// &lt; 8 threshold through the real click-through toggle in both paths: after the readback in probe mode, where it
    /// then reads <c>GWL_EXSTYLE</c> with a cleared last error and throws on failure, and for the cursor otherwise. No
    /// other code calls <c>Apply</c>.
    /// </summary>
    [Fact]
    public void Win32Application_samples_between_draw_and_present_with_the_threshold() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string hitTestSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_overlay_hit_test_controller.cpp");
        string renderFrameBody = ExtractMethodBody(applicationSource, "void Win32Application::RenderFrame(");

        int drawIndex = renderFrameBody.IndexOf("EngineCore->Draw();", StringComparison.Ordinal);
        int sampleIndex = renderFrameBody.IndexOf("OverlayHitTestController->SampleFrame();", StringComparison.Ordinal);
        int presentIndex = renderFrameBody.IndexOf("Presenter->RenderFrame();", StringComparison.Ordinal);
        Assert.True(drawIndex >= 0, "EngineCore->Draw(); was not found.");
        Assert.True(sampleIndex > drawIndex, "The hit-test sample must follow EngineCore->Draw();.");
        Assert.True(presentIndex > sampleIndex, "Presenter->RenderFrame(); must follow the hit-test sample.");
        Assert.Matches(new Regex(@"if \(OverlayHitTestController\) \{\s*frameStage = ""hit_test"";\s*OverlayHitTestController->SampleFrame\(\);\s*\}"), renderFrameBody);
        Assert.DoesNotContain("GetCursorPos(", renderFrameBody, StringComparison.Ordinal);
        Assert.DoesNotContain("Apply(", applicationSource, StringComparison.Ordinal);

        string sampleBody = ExtractMethodBody(hitTestSource, "void Win32OverlayHitTestController::SampleFrame(");
        int captureIndex = sampleBody.IndexOf("HitTestSampler.Capture(samplePoint.x, samplePoint.y);", StringComparison.Ordinal);
        int readIndex = sampleBody.IndexOf("HitTestSampler.TryReadLatestAlpha(sampledAlpha)", StringComparison.Ordinal);
        Assert.True(captureIndex >= 0, "SampleFrame must capture the sample point.");
        Assert.True(readIndex > captureIndex, "The alpha readback must follow the hit-test capture.");
        Assert.Contains("samplePoint.x = CommandLineOptions.GetHitTestProbeX();", sampleBody, StringComparison.Ordinal);
        Assert.Contains("samplePoint.y = CommandLineOptions.GetHitTestProbeY();", sampleBody, StringComparison.Ordinal);
        Assert.Contains("GetCursorPos(&samplePoint)", sampleBody, StringComparison.Ordinal);
        Assert.Contains("ScreenToClient(WindowHandle, &samplePoint)", sampleBody, StringComparison.Ordinal);

        Assert.Equal(2, Regex.Matches(hitTestSource, @"ClickThroughController\.Apply\(").Count);
        Assert.Equal(2, Regex.Matches(sampleBody, Regex.Escape("ClickThroughController.Apply(sampledAlpha < ClickThroughAlphaThreshold);")).Count);
        Assert.Matches(
            new Regex(
                @"if \(HitTestSampler\.TryReadLatestAlpha\(sampledAlpha\)\) \{\s*if \(CommandLineOptions\.HasHitTestProbe\(\)\) \{\s*"
                + @"ClickThroughController\.Apply\(sampledAlpha < ClickThroughAlphaThreshold\);\s*(?://[^\n]*\s*)*SetLastError\(0\);\s*"
                + @"LONG_PTR probeExStyle = GetWindowLongPtrW\(WindowHandle, GWL_EXSTYLE\);\s*"
                + @"if \(probeExStyle == 0 && GetLastError\(\) != 0\) \{\s*throw std::runtime_error\([^;]*;\s*\}\s*"
                + @"ProbeAlpha = sampledAlpha;\s*ProbeExStyle = static_cast<std::uint32_t>\(probeExStyle\);\s*ProbeAlphaAvailable = true;\s*"
                + @"\} else \{\s*ClickThroughController\.Apply\(sampledAlpha < ClickThroughAlphaThreshold\);\s*\}\s*\}"),
            sampleBody);
        Assert.Single(Regex.Matches(sampleBody, @"GetWindowLongPtrW\(WindowHandle,"));
    }

    /// <summary>
    /// Verifies the HIT_TEST line is written only for probe runs, after the fingerprint of the last frame, that the
    /// controller composes it from the remembered probe alpha, the real click-through state and the extended style read
    /// after the toggle (formatted as eight upper-case hexadecimal digits), and that a probe whose readback
    /// never completed logs <c>alpha=pending</c> and exits with code 3 instead of guessing.
    /// </summary>
    [Fact]
    public void Win32Application_logs_hit_test_only_for_probe_runs() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string hitTestSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_overlay_hit_test_controller.cpp");
        string renderFrameBody = ExtractMethodBody(applicationSource, "void Win32Application::RenderFrame(");
        string probeResultBody = ExtractMethodBody(applicationSource, "void Win32Application::WriteHitTestProbeResult(");
        string describeBody = ExtractMethodBody(hitTestSource, "std::string Win32OverlayHitTestController::DescribeProbeResult(");
        string hitTestHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_overlay_hit_test_controller.hpp");

        Assert.Single(Regex.Matches(hitTestSource, Regex.Escape("\"HIT_TEST x=\"")));
        Assert.DoesNotContain("\"HIT_TEST x=\"", applicationSource, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(applicationSource, @"WriteHitTestProbeResult\(\);"));
        Assert.Matches(
            new Regex(@"if \(RenderedFrameCount >= CommandLineOptions\.GetFrameLimit\(\)\) \{[^}]*WriteLifecycleLog\(fingerprintLine\.c_str\(\)\);\s*WriteHitTestProbeResult\(\);\s*PostQuitMessage\(0\);"),
            renderFrameBody);

        Assert.Matches(new Regex(@"^[^{]*\{\s*if \(!CommandLineOptions\.HasHitTestProbe\(\)\) \{\s*return;\s*\}"), probeResultBody);
        Assert.Matches(
            new Regex(
                @"std::string hitTestLine = OverlayHitTestController->DescribeProbeResult\(\);\s*WriteLifecycleLog\(hitTestLine\.c_str\(\)\);\s*"
                + @"if \(!OverlayHitTestController->HasProbeAlpha\(\)\) \{\s*throw Win32ExitRequest\(3,"),
            probeResultBody);

        Assert.Contains("\"HIT_TEST x=\" << CommandLineOptions.GetHitTestProbeX() << \" y=\" << CommandLineOptions.GetHitTestProbeY()", describeBody, StringComparison.Ordinal);
        Assert.Matches(new Regex(@"if \(!ProbeAlphaAvailable\) \{\s*hitTestBuilder << "" alpha=pending"";"), describeBody);
        Assert.Contains("\" alpha=\" << ProbeAlpha << \" clickThrough=\" << (ClickThroughController.IsClickThrough() ? \"on\" : \"off\")", describeBody, StringComparison.Ordinal);
        Assert.Contains("<< \" exStyle=0x\" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << ProbeExStyle", describeBody, StringComparison.Ordinal);
        int clickThroughIndex = describeBody.IndexOf("\" clickThrough=\"", StringComparison.Ordinal);
        int exStyleIndex = describeBody.IndexOf("\" exStyle=0x\"", StringComparison.Ordinal);
        Assert.True(exStyleIndex > clickThroughIndex, "exStyle must follow clickThrough in the HIT_TEST line.");
        Assert.Contains("std::uint32_t ProbeExStyle;", hitTestHeader, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the idle-throttled loop asks the controller about the cursor only in overlay mode, after the message
    /// pump and before the frame decision, and that the controller reports activity only when the cursor moved while
    /// inside the window rectangle.
    /// </summary>
    [Fact]
    public void Win32Application_idle_loop_samples_the_cursor_only_in_overlay_mode() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string hitTestSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_overlay_hit_test_controller.cpp");
        string idleLoopBody = ExtractMethodBody(applicationSource, "void Win32Application::RunIdleThrottledLoop(");

        int pumpIndex = idleLoopBody.IndexOf("if (!PumpMessages()) {", StringComparison.Ordinal);
        int overlayIndex = idleLoopBody.IndexOf("if (WindowModeSettings->GetWindowMode() == Win32WindowMode::Overlay) {", StringComparison.Ordinal);
        int observeIndex = idleLoopBody.IndexOf("OverlayHitTestController->ObserveCursorForActivity()", StringComparison.Ordinal);
        int markIndex = idleLoopBody.IndexOf("ActivityTracker->MarkActivity();", StringComparison.Ordinal);
        int frameDecisionIndex = idleLoopBody.IndexOf("Win32IdleFrameDecision frameDecision", StringComparison.Ordinal);
        Assert.True(pumpIndex >= 0, "The message pump was not found.");
        Assert.True(overlayIndex > pumpIndex, "The overlay cursor sampling must follow the message pump.");
        Assert.True(observeIndex > overlayIndex, "The cursor observation must be inside the overlay-mode block.");
        Assert.True(markIndex > observeIndex, "MarkActivity must follow the cursor observation.");
        Assert.True(frameDecisionIndex > markIndex, "The frame decision must see the cursor activity.");
        Assert.DoesNotContain("GetCursorPos(", idleLoopBody, StringComparison.Ordinal);

        string observeBody = ExtractMethodBody(hitTestSource, "bool Win32OverlayHitTestController::ObserveCursorForActivity(");
        int cursorIndex = observeBody.IndexOf("GetCursorPos(&cursorPosition)", StringComparison.Ordinal);
        int rememberIndex = observeBody.IndexOf("LastCursorPosition = cursorPosition;", StringComparison.Ordinal);
        int windowRectIndex = observeBody.IndexOf("GetWindowRect(WindowHandle, &windowRectangle)", StringComparison.Ordinal);
        int insideIndex = observeBody.IndexOf("PtInRect(&windowRectangle, cursorPosition)", StringComparison.Ordinal);
        Assert.True(cursorIndex >= 0, "ObserveCursorForActivity must sample the cursor.");
        Assert.True(rememberIndex > cursorIndex, "The cursor position must be remembered after it is sampled.");
        Assert.True(windowRectIndex > rememberIndex, "The window rectangle must be read only after a sampled move.");
        Assert.True(insideIndex > windowRectIndex, "The inside-the-window test must use the window rectangle.");
        Assert.Single(Regex.Matches(observeBody, Regex.Escape("GetCursorPos(")));
        Assert.Contains("bool cursorMoved = cursorPosition.x != LastCursorPosition.x || cursorPosition.y != LastCursorPosition.y;", observeBody, StringComparison.Ordinal);
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
