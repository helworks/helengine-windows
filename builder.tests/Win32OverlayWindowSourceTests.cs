using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the opt-in DirectComposition overlay window of the Windows player: <c>Win32WindowStyle</c> carries the
/// normal and overlay style sets, <c>Win32Window</c> keeps today's normal-mode creation and show calls byte for byte
/// while the overlay path creates a borderless topmost window that never takes the foreground, and
/// <c>DirectX11Bootstrap</c> keeps its <c>CreateSwapChainForHwnd</c> path while the composition calls live only in the
/// composition branch.
/// </summary>
public sealed class Win32OverlayWindowSourceTests {
    /// <summary>
    /// Verifies the style value type lives in its own files and declares exactly today's normal values and the overlay
    /// values confirmed by the spike (Revision 1 of the design), created click-through with WS_EX_TRANSPARENT so the
    /// overlay fails open until its first hit-test sample (Revision 2).
    /// </summary>
    [Fact]
    public void Win32WindowStyle_declares_normal_and_overlay_style_sets() {
        string styleHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window_style.hpp");
        string styleSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window_style.cpp");

        Assert.Contains("class Win32WindowStyle {", styleHeader, StringComparison.Ordinal);
        Assert.Contains("static Win32WindowStyle Normal();", styleHeader, StringComparison.Ordinal);
        Assert.Contains("static Win32WindowStyle Overlay();", styleHeader, StringComparison.Ordinal);
        Assert.Contains("DWORD Style;", styleHeader, StringComparison.Ordinal);
        Assert.Contains("DWORD ExStyle;", styleHeader, StringComparison.Ordinal);
        Assert.Contains("int ShowCommand;", styleHeader, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(styleHeader, @"\bclass\s+\w+\s*\{"));

        Assert.Contains(
            "return Win32WindowStyle(Win32WindowMode::Normal, WS_OVERLAPPEDWINDOW, 0, SW_SHOWDEFAULT);",
            styleSource, StringComparison.Ordinal);
        Assert.Contains(
            "return Win32WindowStyle(Win32WindowMode::Overlay, WS_POPUP, WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT, SW_SHOWNOACTIVATE);",
            styleSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the normal-mode window creation and show calls are exactly today's text, so a normal-mode window keeps
    /// its style, ex-style, placement and foreground behavior.
    /// </summary>
    [Fact]
    public void Win32Window_keeps_the_normal_mode_creation_and_show_text() {
        string windowSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.cpp");

        string createNormalBody = ExtractMethodBody(windowSource, "void Win32Window::CreateNormalWindow(");
        Assert.Contains("AdjustWindowRect(&windowRectangle, WS_OVERLAPPEDWINDOW, FALSE);", createNormalBody, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(
                @"Handle = CreateWindowExW\(\s*0,\s*L""HelEngineWindowClass"",\s*Title\.c_str\(\),\s*WS_OVERLAPPEDWINDOW,\s*"
                + @"CW_USEDEFAULT,\s*CW_USEDEFAULT,\s*windowRectangle\.right - windowRectangle\.left,\s*"
                + @"windowRectangle\.bottom - windowRectangle\.top,\s*nullptr,\s*nullptr,\s*GetModuleHandleW\(nullptr\),\s*this\);"),
            createNormalBody);
        Assert.DoesNotContain("AdjustWindowRectEx", createNormalBody, StringComparison.Ordinal);

        string showNormalBody = ExtractMethodBody(windowSource, "void Win32Window::ShowNormalWindow(");
        Assert.Matches(
            new Regex(
                @"ShowWindow\(Handle, SW_SHOWDEFAULT\);\s*UpdateWindow\(Handle\);\s*BringWindowToTop\(Handle\);\s*"
                + @"SetActiveWindow\(Handle\);\s*SetForegroundWindow\(Handle\);\s*SetFocus\(Handle\);"),
            showNormalBody);

        string createBody = ExtractMethodBody(windowSource, "void Win32Window::Create(");
        Assert.Matches(
            new Regex(
                @"if \(WindowStyle\.GetWindowMode\(\) == Win32WindowMode::Overlay\) \{\s*CreateOverlayWindow\(\);\s*\} else \{\s*CreateNormalWindow\(\);\s*\}"),
            createBody);

        string showBody = ExtractMethodBody(windowSource, "void Win32Window::Show(");
        Assert.Matches(
            new Regex(
                @"if \(WindowStyle\.GetWindowMode\(\) == Win32WindowMode::Overlay\) \{\s*ShowOverlayWindow\(\);\s*\} else \{\s*ShowNormalWindow\(\);\s*\}"),
            showBody);
    }

    /// <summary>
    /// Verifies the overlay path uses the extended style set with <c>AdjustWindowRectEx</c>, shows without activation,
    /// pins the window topmost without moving or resizing it (<c>SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE</c>, because
    /// the bounds were set at creation), and never calls any foreground or focus function or
    /// <c>SetLayeredWindowAttributes</c>.
    /// </summary>
    [Fact]
    public void Win32Window_overlay_path_never_takes_the_foreground() {
        string windowSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.cpp");
        string windowHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.hpp");

        Assert.Contains(
            "Win32Window(const wchar_t* title, int left, int top, int width, int height, const Win32WindowStyle& windowStyle);",
            windowHeader, StringComparison.Ordinal);

        string createOverlayBody = ExtractMethodBody(windowSource, "void Win32Window::CreateOverlayWindow(");
        Assert.Contains(
            "AdjustWindowRectEx(&windowRectangle, WindowStyle.GetStyle(), FALSE, WindowStyle.GetExStyle());",
            createOverlayBody, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(@"Handle = CreateWindowExW\(\s*WindowStyle\.GetExStyle\(\),\s*L""HelEngineWindowClass"",\s*Title\.c_str\(\),\s*WindowStyle\.GetStyle\(\),\s*Left,\s*Top,"),
            createOverlayBody);

        string showOverlayBody = ExtractMethodBody(windowSource, "void Win32Window::ShowOverlayWindow(");
        Assert.Contains("ShowWindow(Handle, WindowStyle.GetShowCommand());", showOverlayBody, StringComparison.Ordinal);
        Assert.Contains("SetWindowPos(Handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)", showOverlayBody, StringComparison.Ordinal);
        foreach (string foregroundCall in new[] { "BringWindowToTop", "SetActiveWindow", "SetForegroundWindow", "SetFocus", "UpdateWindow" }) {
            Assert.DoesNotContain(foregroundCall, showOverlayBody, StringComparison.Ordinal);
        }

        Assert.Single(Regex.Matches(windowSource, @"SetForegroundWindow\("));
        Assert.DoesNotContain("SetLayeredWindowAttributes", windowSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the bootstrap keeps its window swap-chain path (<c>CreateSwapChainForHwnd</c>, <c>ALPHA_MODE_IGNORE</c>)
    /// and creates the composition swap chain and the DirectComposition device, target, visual and commit only inside
    /// the composition branch, with a premultiplied alpha mode and a topmost target.
    /// </summary>
    [Fact]
    public void DirectX11Bootstrap_creates_composition_objects_only_in_the_composition_branch() {
        string bootstrapSource = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_bootstrap.cpp");
        string bootstrapHeader = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_bootstrap.hpp");

        Assert.Contains("DirectX11Bootstrap(HWND windowHandle, int width, int height, bool useComposition);", bootstrapHeader, StringComparison.Ordinal);
        Assert.Contains("#include <dcomp.h>", bootstrapHeader, StringComparison.Ordinal);

        string createSwapChainBody = ExtractMethodBody(bootstrapSource, "void DirectX11Bootstrap::CreateSwapChain(");
        Assert.Matches(new Regex(@"^[^{]*\{\s*if \(UseComposition\) \{\s*CreateCompositionSwapChain\(\);\s*return;\s*\}"), createSwapChainBody);
        Assert.Contains("swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_IGNORE;", createSwapChainBody, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(@"factory->CreateSwapChainForHwnd\(\s*Device\.Get\(\),\s*WindowHandle,\s*&swapChainDescription,\s*nullptr,\s*nullptr,\s*SwapChain\.GetAddressOf\(\)\)"),
            createSwapChainBody);
        Assert.Contains("factory->MakeWindowAssociation(WindowHandle, DXGI_MWA_NO_ALT_ENTER)", createSwapChainBody, StringComparison.Ordinal);

        string compositionBody = ExtractMethodBody(bootstrapSource, "void DirectX11Bootstrap::CreateCompositionSwapChain(");
        string[] compositionCalls = {
            "DXGI_ALPHA_MODE_PREMULTIPLIED",
            "CreateSwapChainForComposition(",
            "DCompositionCreateDevice(",
            "CreateTargetForHwnd(WindowHandle, TRUE,",
            "CreateVisual(",
            "SetContent(SwapChain.Get())",
            "SetRoot(CompositionVisual.Get())",
            "CompositionDevice->Commit()"
        };
        int previousIndex = -1;
        foreach (string compositionCall in compositionCalls) {
            int callIndex = compositionBody.IndexOf(compositionCall, StringComparison.Ordinal);
            Assert.True(callIndex > previousIndex, $"Composition call {compositionCall} is missing or out of order.");
            previousIndex = callIndex;
            Assert.Single(Regex.Matches(bootstrapSource, Regex.Escape(compositionCall)));
        }

        Assert.Contains("ThrowIfCompositionFailed(", compositionBody, StringComparison.Ordinal);
        Assert.DoesNotContain("ThrowIfFailed(", compositionBody, StringComparison.Ordinal);
        Assert.Contains("DirectX11HResultFormatter::ToHex(result)", bootstrapSource, StringComparison.Ordinal);

        // Members are destroyed in reverse declaration order: the DirectComposition objects must be declared after the
        // device and the swap chain, and the visual after the target after the composition device.
        string[] orderedMembers = {
            "Microsoft::WRL::ComPtr<ID3D11Device> Device;",
            "Microsoft::WRL::ComPtr<IDXGISwapChain1> SwapChain;",
            "Microsoft::WRL::ComPtr<IDCompositionDevice> CompositionDevice;",
            "Microsoft::WRL::ComPtr<IDCompositionTarget> CompositionTarget;",
            "Microsoft::WRL::ComPtr<IDCompositionVisual> CompositionVisual;"
        };
        int previousMemberIndex = -1;
        foreach (string member in orderedMembers) {
            int memberIndex = bootstrapHeader.IndexOf(member, StringComparison.Ordinal);
            Assert.True(memberIndex > previousMemberIndex, $"Member {member} is missing or out of order.");
            previousMemberIndex = memberIndex;
        }
    }

    /// <summary>
    /// Verifies CreateMainWindow picks the style set from the resolved window mode, resolves the overlay bounds from
    /// the primary monitor, and hands the composition flag to the bootstrap only in overlay mode.
    /// </summary>
    [Fact]
    public void Win32Application_selects_overlay_window_and_composition_from_window_mode() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string applicationHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.hpp");

        Assert.Contains("std::unique_ptr<Win32WindowModeSettings> WindowModeSettings;", applicationHeader, StringComparison.Ordinal);

        string createMainWindowBody = ExtractMethodBody(applicationSource, "Win32Application::CreateMainWindow(");
        Assert.Contains("WindowModeSettings = std::make_unique<Win32WindowModeSettings>(windowModeSettings);", createMainWindowBody, StringComparison.Ordinal);
        Assert.Contains(
            "MainWindow = std::make_unique<Win32Window>(L\"HelEngine Windows Host\", CW_USEDEFAULT, CW_USEDEFAULT, profile.ResolutionWidth, profile.ResolutionHeight, Win32WindowStyle::Normal());",
            createMainWindowBody, StringComparison.Ordinal);
        Assert.Contains("Win32WindowStyle::Overlay()", createMainWindowBody, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(applicationSource, @"Win32WindowStyle::Overlay\(\)"));
        Assert.Single(Regex.Matches(applicationSource, @"Win32WindowStyle::Normal\(\)"));

        string overlayRectangleBody = ExtractMethodBody(applicationSource, "RECT Win32Application::ResolveOverlayRectangle(");
        Assert.Contains("MonitorFromPoint(primaryMonitorOrigin, MONITOR_DEFAULTTOPRIMARY)", overlayRectangleBody, StringComparison.Ordinal);
        Assert.Contains("GetMonitorInfoW(", overlayRectangleBody, StringComparison.Ordinal);
        Assert.Contains("Win32OverlayBounds::Monitor", overlayRectangleBody, StringComparison.Ordinal);
        Assert.Contains("profile.ResolutionWidth", overlayRectangleBody, StringComparison.Ordinal);

        string bootstrapBody = ExtractMethodBody(applicationSource, "Win32Application::CreateGraphicsBootstrap(");
        Assert.Matches(
            new Regex(
                @"Bootstrap = std::make_unique<DirectX11Bootstrap>\(\s*MainWindow->GetHandle\(\),\s*MainWindow->GetClientWidth\(\),\s*"
                + @"MainWindow->GetClientHeight\(\),\s*WindowModeSettings->GetWindowMode\(\) == Win32WindowMode::Overlay\);"),
            bootstrapBody);
    }

    /// <summary>
    /// Verifies the native build compiles the style source and links DirectComposition.
    /// </summary>
    [Fact]
    public void CMakeLists_compiles_window_style_and_links_dcomp() {
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("src/platform/windows/win32/win32_window_style.cpp", cmakeSource, StringComparison.Ordinal);
        Match linkBlock = Regex.Match(cmakeSource, @"target_link_libraries\(helengine_windows PRIVATE\r?\n([^)]*)\)");
        Assert.True(linkBlock.Success, "The player target must declare its link libraries.");
        Assert.Matches(new Regex(@"^\s*dcomp\s*$", RegexOptions.Multiline), linkBlock.Groups[1].Value);
    }

    /// <summary>
    /// Verifies one shared formatter writes every HRESULT in hexadecimal and that the DirectX classes use it instead of
    /// keeping their own copies of the formatting.
    /// </summary>
    [Fact]
    public void DirectX11HResultFormatter_is_the_single_hresult_formatting_point() {
        string formatterHeader = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_hresult_formatter.hpp");
        string formatterSource = ReadRepositoryFile("src", "platform", "windows", "directx11", "directx11_hresult_formatter.cpp");

        Assert.Contains("class DirectX11HResultFormatter {", formatterHeader, StringComparison.Ordinal);
        Assert.Contains("static std::string ToHex(HRESULT result);", formatterHeader, StringComparison.Ordinal);
        Assert.Contains("std::setw(8)", formatterSource, StringComparison.Ordinal);
        Assert.Contains("src/platform/windows/directx11/directx11_hresult_formatter.cpp", ReadRepositoryFile("CMakeLists.txt"), StringComparison.Ordinal);

        string[][] callers = {
            new[] { "directx11_bootstrap.cpp", "\" failed for the HelEngine Windows overlay with HRESULT \" << DirectX11HResultFormatter::ToHex(result)" },
            new[] { "directx11_host_fingerprint.cpp", "\"IDXGISwapChain1::Present failed with HRESULT \" << DirectX11HResultFormatter::ToHex(presentResult)" },
            new[] { "directx11_host_fingerprint.cpp", "\" failed with HRESULT \" << DirectX11HResultFormatter::ToHex(result)" },
            new[] { "directx11_back_buffer_capture.cpp", "\" with HRESULT \" << DirectX11HResultFormatter::ToHex(result)" }
        };
        foreach (string[] caller in callers) {
            string callerSource = ReadRepositoryFile("src", "platform", "windows", "directx11", caller[0]);
            Assert.Contains(caller[1], callerSource, StringComparison.Ordinal);
            Assert.DoesNotContain("HRESULT 0x", callerSource, StringComparison.Ordinal);
        }
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
