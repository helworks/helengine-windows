using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the overlay window's per-pixel click-through: <c>DirectX11HitTestSampler</c> reads one back-buffer pixel
/// through a ring of three 1×1 staging textures without ever stalling, <c>Win32ClickThroughController</c> toggles
/// <c>WS_EX_TRANSPARENT</c> only when the state changes, and <c>Win32Application</c> creates both only in overlay mode,
/// applies the alpha &lt; 8 threshold, logs the <c>HIT_TEST</c> line only for <c>--hit-test-probe</c> runs and feeds
/// cursor movement over the overlay into the idle throttle's activity tracker.
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
    /// non-moving, non-activating <c>SetWindowPos</c>, and returns early when the requested state is already applied.
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
    }

    /// <summary>
    /// Verifies the sampler and the controller are created only in overlay mode and the native build compiles both.
    /// </summary>
    [Fact]
    public void Win32Application_constructs_hit_test_objects_only_in_overlay_mode() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string applicationHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.hpp");
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("std::unique_ptr<DirectX11HitTestSampler> HitTestSampler;", applicationHeader, StringComparison.Ordinal);
        Assert.Contains("std::unique_ptr<Win32ClickThroughController> ClickThroughController;", applicationHeader, StringComparison.Ordinal);

        string bootstrapBody = ExtractMethodBody(applicationSource, "Win32Application::CreateGraphicsBootstrap(");
        Assert.Matches(
            new Regex(
                @"if \(WindowModeSettings->GetWindowMode\(\) == Win32WindowMode::Overlay\) \{\s*"
                + @"HitTestSampler = std::make_unique<DirectX11HitTestSampler>\(\*Bootstrap\);\s*"
                + @"ClickThroughController = std::make_unique<Win32ClickThroughController>\(MainWindow->GetHandle\(\)\);\s*\}"),
            bootstrapBody);
        Assert.Single(Regex.Matches(applicationSource, @"std::make_unique<DirectX11HitTestSampler>"));
        Assert.Single(Regex.Matches(applicationSource, @"std::make_unique<Win32ClickThroughController>"));

        Assert.Contains("src/platform/windows/directx11/directx11_hit_test_sampler.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("src/platform/windows/win32/win32_click_through_controller.cpp", cmakeSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies RenderFrame samples between Draw and Present, samples the probe point instead of the cursor when
    /// probing, applies the alpha &lt; 8 threshold only when not probing, and never toggles styles in probe mode.
    /// </summary>
    [Fact]
    public void Win32Application_samples_between_draw_and_present_with_the_threshold() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string renderFrameBody = ExtractMethodBody(applicationSource, "void Win32Application::RenderFrame(");

        int drawIndex = renderFrameBody.IndexOf("EngineCore->Draw();", StringComparison.Ordinal);
        int captureIndex = renderFrameBody.IndexOf("HitTestSampler->Capture(", StringComparison.Ordinal);
        int readIndex = renderFrameBody.IndexOf("HitTestSampler->TryReadLatestAlpha(", StringComparison.Ordinal);
        int presentIndex = renderFrameBody.IndexOf("Presenter->RenderFrame();", StringComparison.Ordinal);
        Assert.True(drawIndex >= 0, "EngineCore->Draw(); was not found.");
        Assert.True(captureIndex > drawIndex, "The hit-test capture must follow EngineCore->Draw();.");
        Assert.True(readIndex > captureIndex, "The alpha readback must follow the hit-test capture.");
        Assert.True(presentIndex > readIndex, "Presenter->RenderFrame(); must follow the alpha readback.");

        Assert.Contains("if (HitTestSampler) {", renderFrameBody, StringComparison.Ordinal);
        Assert.Contains("samplePoint.x = CommandLineOptions.GetHitTestProbeX();", renderFrameBody, StringComparison.Ordinal);
        Assert.Contains("samplePoint.y = CommandLineOptions.GetHitTestProbeY();", renderFrameBody, StringComparison.Ordinal);
        Assert.Contains("GetCursorPos(&samplePoint)", renderFrameBody, StringComparison.Ordinal);
        Assert.Contains("ScreenToClient(MainWindow->GetHandle(), &samplePoint)", renderFrameBody, StringComparison.Ordinal);

        Assert.Single(Regex.Matches(applicationSource, @"ClickThroughController->Apply\("));
        Assert.Contains("ClickThroughController->Apply(sampledAlpha < 8);", renderFrameBody, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(
                @"if \(CommandLineOptions\.HasHitTestProbe\(\)\) \{\s*HitTestProbeAlpha = sampledAlpha;\s*HitTestProbeAlphaAvailable = true;\s*\} else \{\s*"
                + @"ClickThroughController->Apply\(sampledAlpha < 8\);\s*\}"),
            renderFrameBody);
    }

    /// <summary>
    /// Verifies the HIT_TEST line is written only for probe runs, after the fingerprint of the last frame, and that a
    /// probe whose readback never completed logs <c>alpha=pending</c> and exits with code 3 instead of guessing.
    /// </summary>
    [Fact]
    public void Win32Application_logs_hit_test_only_for_probe_runs() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string renderFrameBody = ExtractMethodBody(applicationSource, "void Win32Application::RenderFrame(");
        string probeResultBody = ExtractMethodBody(applicationSource, "void Win32Application::WriteHitTestProbeResult(");

        Assert.Single(Regex.Matches(applicationSource, Regex.Escape("\"HIT_TEST x=\"")));
        Assert.Single(Regex.Matches(applicationSource, @"WriteHitTestProbeResult\(\);"));
        Assert.Matches(
            new Regex(@"if \(RenderedFrameCount >= CommandLineOptions\.GetFrameLimit\(\)\) \{[^}]*WriteLifecycleLog\(fingerprintLine\.c_str\(\)\);\s*WriteHitTestProbeResult\(\);\s*PostQuitMessage\(0\);"),
            renderFrameBody);

        Assert.Matches(new Regex(@"^[^{]*\{\s*if \(!CommandLineOptions\.HasHitTestProbe\(\)\) \{\s*return;\s*\}"), probeResultBody);
        Assert.Contains("\"HIT_TEST x=\" << CommandLineOptions.GetHitTestProbeX() << \" y=\" << CommandLineOptions.GetHitTestProbeY()", probeResultBody, StringComparison.Ordinal);
        Assert.Contains("\" alpha=pending\"", probeResultBody, StringComparison.Ordinal);
        Assert.Contains("\" alpha=\" << HitTestProbeAlpha << \" clickThrough=\" << (HitTestProbeAlpha < 8 ? \"on\" : \"off\")", probeResultBody, StringComparison.Ordinal);
        Assert.Matches(new Regex(@"if \(!HitTestProbeAlphaAvailable\) \{[^}]*throw Win32ExitRequest\(3,"), probeResultBody);
    }

    /// <summary>
    /// Verifies the idle-throttled loop samples the cursor only in overlay mode and marks activity only when the
    /// cursor moved while inside the window rectangle.
    /// </summary>
    [Fact]
    public void Win32Application_idle_loop_samples_the_cursor_only_in_overlay_mode() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string idleLoopBody = ExtractMethodBody(applicationSource, "void Win32Application::RunIdleThrottledLoop(");

        int pumpIndex = idleLoopBody.IndexOf("if (!PumpMessages()) {", StringComparison.Ordinal);
        int overlayIndex = idleLoopBody.IndexOf("if (WindowModeSettings->GetWindowMode() == Win32WindowMode::Overlay) {", StringComparison.Ordinal);
        int cursorIndex = idleLoopBody.IndexOf("GetCursorPos(&cursorPosition)", StringComparison.Ordinal);
        int markIndex = idleLoopBody.IndexOf("ActivityTracker->MarkActivity();", StringComparison.Ordinal);
        int frameDecisionIndex = idleLoopBody.IndexOf("Win32IdleFrameDecision frameDecision", StringComparison.Ordinal);
        Assert.True(pumpIndex >= 0, "The message pump was not found.");
        Assert.True(overlayIndex > pumpIndex, "The overlay cursor sampling must follow the message pump.");
        Assert.True(cursorIndex > overlayIndex, "GetCursorPos must be inside the overlay-mode block.");
        Assert.True(markIndex > cursorIndex, "MarkActivity must follow the cursor sample.");
        Assert.True(frameDecisionIndex > markIndex, "The frame decision must see the cursor activity.");
        Assert.Single(Regex.Matches(idleLoopBody, Regex.Escape("GetCursorPos(")));
        Assert.Contains("PtInRect(&windowRectangle, cursorPosition)", idleLoopBody, StringComparison.Ordinal);
        Assert.Contains("GetWindowRect(MainWindow->GetHandle(), &windowRectangle)", idleLoopBody, StringComparison.Ordinal);
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
