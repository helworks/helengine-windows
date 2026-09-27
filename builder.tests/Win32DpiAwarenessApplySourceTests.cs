using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies how the Windows player applies the opt-in Per-Monitor v2 DPI awareness: the process context is set once,
/// only when opted in and before the first monitor query or window; the unaware window-creation paths stay byte for
/// byte what they were; an aware normal window is resized after creation so its client area is the profile
/// resolution in physical pixels; and a <c>WM_DPICHANGED</c> keeps the client pixel size of a normal window while
/// leaving the overlay rectangle alone.
/// </summary>
public sealed class Win32DpiAwarenessApplySourceTests {
    /// <summary>
    /// Pins today's <c>CreateNormalWindow</c> definition text, so the unaware normal-mode creation path cannot gain
    /// calls (Review Focus 3).
    /// </summary>
    const string ExpectedCreateNormalWindowBody = """
        void Win32Window::CreateNormalWindow() {
                RECT windowRectangle { 0, 0, Width, Height };
                AdjustWindowRect(&windowRectangle, WS_OVERLAPPEDWINDOW, FALSE);

                Handle = CreateWindowExW(
                    0,
                    L"HelEngineWindowClass",
                    Title.c_str(),
                    WS_OVERLAPPEDWINDOW,
                    CW_USEDEFAULT,
                    CW_USEDEFAULT,
                    windowRectangle.right - windowRectangle.left,
                    windowRectangle.bottom - windowRectangle.top,
                    nullptr,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    this);
        """;

    /// <summary>
    /// Pins today's <c>CreateOverlayWindow</c> definition text, so the overlay creation path cannot gain calls
    /// (Review Focus 3).
    /// </summary>
    const string ExpectedCreateOverlayWindowBody = """
        void Win32Window::CreateOverlayWindow() {
                RECT windowRectangle { 0, 0, Width, Height };
                AdjustWindowRectEx(&windowRectangle, WindowStyle.GetStyle(), FALSE, WindowStyle.GetExStyle());

                Handle = CreateWindowExW(
                    WindowStyle.GetExStyle(),
                    L"HelEngineWindowClass",
                    Title.c_str(),
                    WindowStyle.GetStyle(),
                    Left,
                    Top,
                    windowRectangle.right - windowRectangle.left,
                    windowRectangle.bottom - windowRectangle.top,
                    nullptr,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    this);
        """;

    /// <summary>
    /// Verifies <c>SetProcessDpiAwarenessContext</c> is called exactly once in the player, inside the
    /// <c>IsPerMonitorV2()</c> branch of <c>ApplyDpiAwareness</c>, throwing a <c>std::runtime_error</c> that names the
    /// call and <c>GetLastError()</c> on failure, and that the configuration line is logged only inside that branch so
    /// an unaware startup log gains no line.
    /// </summary>
    [Fact]
    public void ApplyDpiAwareness_sets_the_process_context_only_when_per_monitor_v2() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string applicationHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.hpp");

        Assert.Contains("void ApplyDpiAwareness(const Win32DpiAwarenessSettings& dpiAwarenessSettings) const;", applicationHeader, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(applicationSource, @"SetProcessDpiAwarenessContext\("));

        string applyBody = ExtractMethodBody(applicationSource, "void Win32Application::ApplyDpiAwareness(");
        Assert.Matches(
            new Regex(
                @"if \(dpiAwarenessSettings\.IsPerMonitorV2\(\)\) \{\s*"
                + @"if \(!SetProcessDpiAwarenessContext\(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2\)\) \{\s*"
                + @"DWORD errorCode = GetLastError\(\);\s*[^}]*SetProcessDpiAwarenessContext failed[^}]*errorCode[^}]*"
                + @"throw std::runtime_error\([^}]*\}\s*"
                + @"std::string dpiAwarenessMessage = ""DPI awareness configured: "" \+ dpiAwarenessSettings\.Describe\(\);\s*"
                + @"WriteLifecycleLog\(dpiAwarenessMessage\.c_str\(\)\);\s*\}\s*$"),
            applyBody);
    }

    /// <summary>
    /// Verifies invalid DPI-awareness configuration is converted into a <c>Win32ExitRequest</c> with exit code 2, like
    /// the window-mode settings.
    /// </summary>
    [Fact]
    public void ResolveDpiAwarenessSettings_converts_invalid_configuration_into_exit_code_2() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string applicationHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.hpp");

        Assert.Contains("Win32DpiAwarenessSettings ResolveDpiAwarenessSettings(const RuntimePlayerProfile& profile) const;", applicationHeader, StringComparison.Ordinal);
        string resolveBody = ExtractMethodBody(applicationSource, "Win32DpiAwarenessSettings Win32Application::ResolveDpiAwarenessSettings(");
        Assert.Matches(
            new Regex(
                @"try \{\s*return Win32DpiAwarenessSettings::Resolve\(profile, CommandLineOptions\);\s*\} "
                + @"catch \(const std::invalid_argument& configurationError\) \{\s*(?://[^\n]*\s*)*"
                + @"throw Win32ExitRequest\(2, configurationError\.what\(\)\);\s*\}"),
            resolveBody);
    }

    /// <summary>
    /// Verifies <c>CreateMainWindow</c> resolves and applies the awareness right after the profile and window-mode
    /// settings, before <c>ResolveOverlayRectangle</c> queries the monitor and before the first <c>Win32Window</c> is
    /// made (Review Focus 2), and hands the value to the window before <c>Create()</c>.
    /// </summary>
    [Fact]
    public void CreateMainWindow_applies_awareness_before_the_first_monitor_query_or_window() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string createMainWindowBody = ExtractMethodBody(applicationSource, "void Win32Application::CreateMainWindow(");

        Assert.Matches(
            new Regex(
                @"RuntimePlayerProfile profile = ResolveRuntimePlayerProfile\(\);\s*"
                + @"Win32WindowModeSettings windowModeSettings = ResolveWindowModeSettings\(profile\);\s*"
                + @"Win32DpiAwarenessSettings dpiAwarenessSettings = ResolveDpiAwarenessSettings\(profile\);\s*"
                + @"ApplyDpiAwareness\(dpiAwarenessSettings\);"),
            createMainWindowBody);

        int applyIndex = createMainWindowBody.IndexOf("ApplyDpiAwareness(", StringComparison.Ordinal);
        int overlayRectangleIndex = createMainWindowBody.IndexOf("ResolveOverlayRectangle(", StringComparison.Ordinal);
        int firstWindowIndex = createMainWindowBody.IndexOf("std::make_unique<Win32Window>", StringComparison.Ordinal);
        int setDpiAwarenessIndex = createMainWindowBody.IndexOf("MainWindow->SetDpiAwareness(dpiAwarenessSettings.GetDpiAwareness());", StringComparison.Ordinal);
        int createIndex = createMainWindowBody.IndexOf("MainWindow->Create();", StringComparison.Ordinal);
        Assert.True(applyIndex >= 0, "CreateMainWindow must apply the DPI awareness.");
        Assert.True(overlayRectangleIndex > applyIndex, "ApplyDpiAwareness must run before ResolveOverlayRectangle queries the monitor.");
        Assert.True(firstWindowIndex > applyIndex, "ApplyDpiAwareness must run before the first Win32Window is made.");
        Assert.True(setDpiAwarenessIndex > firstWindowIndex && createIndex > setDpiAwarenessIndex, "The window must receive the awareness after it is made and before Create().");
    }

    /// <summary>
    /// Verifies the normal and overlay creation bodies equal their pre-change text exactly (Review Focus 3).
    /// </summary>
    [Fact]
    public void Win32Window_creation_bodies_are_unchanged() {
        string windowSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.cpp");

        Assert.Equal(NormalizeLineEndings(ExpectedCreateNormalWindowBody), ExtractMethodBody(windowSource, "void Win32Window::CreateNormalWindow("));
        Assert.Equal(NormalizeLineEndings(ExpectedCreateOverlayWindowBody), ExtractMethodBody(windowSource, "void Win32Window::CreateOverlayWindow("));
    }

    /// <summary>
    /// Verifies the window stores the awareness (default unaware) through <c>SetDpiAwareness</c>, and that
    /// <c>Create()</c> runs the post-create correction only for a Per-Monitor v2 normal window with the client size
    /// requested before creation, while the correction reads the window's DPI with <c>GetDpiForWindow</c>, resizes
    /// only when the outer size differs, never moves the window (<c>SWP_NOMOVE</c>) and refreshes the cached client
    /// size.
    /// </summary>
    [Fact]
    public void Win32Window_corrects_the_normal_window_size_only_when_per_monitor_v2() {
        string windowSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.cpp");
        string windowHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.hpp");

        Assert.Contains("void SetDpiAwareness(Win32DpiAwareness dpiAwareness);", windowHeader, StringComparison.Ordinal);
        Assert.Contains("Win32DpiAwareness DpiAwareness;", windowHeader, StringComparison.Ordinal);
        Assert.Contains("void CorrectNormalWindowSizeForDpi(int requestedClientWidth, int requestedClientHeight);", windowHeader, StringComparison.Ordinal);
        Assert.Contains(", DpiAwareness(Win32DpiAwareness::Unaware)", windowSource, StringComparison.Ordinal);

        string createBody = ExtractMethodBody(windowSource, "void Win32Window::Create(");
        Assert.Matches(
            new Regex(
                @"int requestedClientWidth = Width;\s*int requestedClientHeight = Height;\s*"
                + @"if \(WindowStyle\.GetWindowMode\(\) == Win32WindowMode::Overlay\) \{\s*CreateOverlayWindow\(\);\s*\} else \{\s*CreateNormalWindow\(\);\s*\}"),
            createBody);
        Assert.Matches(
            new Regex(
                @"RefreshClientSize\(\);\s*"
                + @"if \(DpiAwareness == Win32DpiAwareness::PerMonitorV2 && WindowStyle\.GetWindowMode\(\) == Win32WindowMode::Normal\) \{\s*"
                + @"CorrectNormalWindowSizeForDpi\(requestedClientWidth, requestedClientHeight\);\s*\}\s*$"),
            createBody);
        Assert.Single(Regex.Matches(windowSource, @"CorrectNormalWindowSizeForDpi\(requestedClientWidth, requestedClientHeight\);"));

        string correctBody = ExtractMethodBody(windowSource, "void Win32Window::CorrectNormalWindowSizeForDpi(");
        Assert.Contains("UINT windowDpi = GetDpiForWindow(Handle);", correctBody, StringComparison.Ordinal);
        Assert.Contains(
            "SIZE outerSize = Win32DpiWindowSizing::OuterSizeForClient(requestedClientWidth, requestedClientHeight, WS_OVERLAPPEDWINDOW, 0, windowDpi);",
            correctBody, StringComparison.Ordinal);
        Assert.Contains("GetWindowRect(Handle, &windowRectangle)", correctBody, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(
                @"if \(outerSize\.cx != currentWidth \|\| outerSize\.cy != currentHeight\) \{\s*"
                + @"if \(!SetWindowPos\(Handle, nullptr, 0, 0, outerSize\.cx, outerSize\.cy, SWP_NOMOVE \| SWP_NOZORDER \| SWP_NOACTIVATE\)\) \{"
                + @"[^}]*throw std::runtime_error\([^}]*\}\s*RefreshClientSize\(\);\s*\}"),
            correctBody);

        Assert.Single(Regex.Matches(windowSource, @"GetDpiForWindow\("));
    }

    /// <summary>
    /// Verifies the <c>WM_DPICHANGED</c> case for a normal window: a minimized window (<c>IsIconic</c>) returns 0
    /// before any resize, because its cached client size is 0x0; a maximized window (<c>IsZoomed</c>) takes the
    /// suggested rectangle from <c>lParam</c> as given so it still fills its monitor; any other normal window is placed
    /// at the suggested top-left with the outer size for its current client size at the new DPI from
    /// <c>HIWORD(wParam)</c> (Review Focus 4). A failed <c>SetWindowPos</c> throws naming the call and
    /// <c>GetLastError()</c>, an overlay keeps its rectangle, and the message returns 0 in every case.
    /// </summary>
    [Fact]
    public void Win32Window_keeps_the_client_pixel_size_on_WM_DPICHANGED() {
        string windowSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.cpp");
        string handleMessageBody = ExtractMethodBody(windowSource, "LRESULT Win32Window::HandleMessage(");

        Assert.Matches(
            new Regex(
                @"case WM_DPICHANGED:\s*(?://[^
]*\s*)*"
                + @"if \(WindowStyle\.GetWindowMode\(\) == Win32WindowMode::Normal\) \{\s*"
                + @"if \(IsIconic\(Handle\)\) \{\s*(?://[^
]*\s*)*return 0;\s*\}\s*"
                + @"const RECT& suggestedRectangle = \*reinterpret_cast<RECT\*>\(lParam\);\s*"
                + @"RECT placement = suggestedRectangle;\s*(?://[^
]*\s*)*"
                + @"if \(!IsZoomed\(Handle\)\) \{\s*"
                + @"placement = Win32DpiWindowSizing::PlacementForDpiChange\(suggestedRectangle, GetClientWidth\(\), GetClientHeight\(\), WS_OVERLAPPEDWINDOW, 0, HIWORD\(wParam\)\);\s*\}\s*"
                + @"if \(!SetWindowPos\(Handle, nullptr, placement\.left, placement\.top, placement\.right - placement\.left, placement\.bottom - placement\.top, SWP_NOZORDER \| SWP_NOACTIVATE\)\) \{\s*"
                + @"DWORD errorCode = GetLastError\(\);[^}]*SetWindowPos failed[^}]*errorCode[^}]*throw std::runtime_error\([^}]*\}\s*"
                + @"\}\s*return 0;"),
            handleMessageBody);
        Assert.Single(Regex.Matches(windowSource, @"case WM_DPICHANGED:"));

        int trackerIndex = handleMessageBody.IndexOf("ActivityTracker->ObserveMessage(message);", StringComparison.Ordinal);
        int switchIndex = handleMessageBody.IndexOf("switch (message) {", StringComparison.Ordinal);
        Assert.True(trackerIndex >= 0 && switchIndex > trackerIndex, "The activity tracker must still observe every message, WM_DPICHANGED included, before it is handled.");
    }

    /// <summary>
    /// Verifies the <c>WM_GETDPISCALEDSIZE</c> case: only a Per-Monitor v2 normal window that is neither maximized
    /// nor minimized writes the outer size for its current client size at the new DPI from <c>wParam</c> into the
    /// <c>SIZE</c> that <c>lParam</c> points to and returns TRUE, so the <c>WM_DPICHANGED</c> suggested rectangle
    /// already has the right size during a drag; every other case, the overlay included, returns FALSE and lets
    /// Windows scale linearly.
    /// </summary>
    [Fact]
    public void Win32Window_answers_WM_GETDPISCALEDSIZE_with_the_client_pixel_size() {
        string windowSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.cpp");
        string handleMessageBody = ExtractMethodBody(windowSource, "LRESULT Win32Window::HandleMessage(");

        Assert.Matches(
            new Regex(
                @"case WM_GETDPISCALEDSIZE:\s*(?://[^
]*\s*)*"
                + @"if \(WindowStyle\.GetWindowMode\(\) == Win32WindowMode::Normal && DpiAwareness == Win32DpiAwareness::PerMonitorV2 && !IsZoomed\(Handle\) && !IsIconic\(Handle\)\) \{\s*"
                + @"SIZE\* scaledSize = reinterpret_cast<SIZE\*>\(lParam\);\s*"
                + @"\*scaledSize = Win32DpiWindowSizing::OuterSizeForClient\(GetClientWidth\(\), GetClientHeight\(\), WS_OVERLAPPEDWINDOW, 0, static_cast<UINT>\(wParam\)\);\s*"
                + @"return TRUE;\s*\}\s*"
                + @"return FALSE;"),
            handleMessageBody);
        Assert.Single(Regex.Matches(windowSource, @"case WM_GETDPISCALEDSIZE:"));
        Assert.DoesNotMatch(new Regex(@"case WM_GETDPISCALEDSIZE:[^}]*try \{"), handleMessageBody);
    }

    /// <summary>
    /// Verifies no C++ exception unwinds through user32 frames: <c>WindowProcedure</c> catches anything
    /// <c>HandleMessage</c> throws, keeps it with <c>std::current_exception()</c> in a pending-exception member and
    /// returns 0, and <c>RethrowPendingException</c> rethrows and clears it.
    /// </summary>
    [Fact]
    public void Win32Window_marshals_message_handler_exceptions_out_of_the_window_procedure() {
        string windowSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.cpp");
        string windowHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.hpp");

        Assert.Contains("std::exception_ptr PendingException;", windowHeader, StringComparison.Ordinal);
        Assert.Contains("void RethrowPendingException();", windowHeader, StringComparison.Ordinal);
        Assert.Contains(", PendingException(nullptr)", windowSource, StringComparison.Ordinal);

        string windowProcedureBody = ExtractMethodBody(windowSource, "LRESULT CALLBACK Win32Window::WindowProcedure(");
        Assert.Matches(
            new Regex(
                @"if \(window != nullptr\) \{\s*try \{\s*return window->HandleMessage\(message, wParam, lParam\);\s*\} "
                + @"catch \(\.\.\.\) \{\s*(?://[^
]*\s*)*"
                + @"if \(window->PendingException == nullptr\) \{\s*window->PendingException = std::current_exception\(\);\s*\}\s*"
                + @"return 0;\s*\}\s*\}"),
            windowProcedureBody);
        Assert.Single(Regex.Matches(windowSource, @"window->HandleMessage\("));

        string rethrowBody = ExtractMethodBody(windowSource, "void Win32Window::RethrowPendingException(");
        Assert.Matches(
            new Regex(
                @"if \(PendingException != nullptr\) \{\s*std::exception_ptr pendingException = PendingException;\s*"
                + @"PendingException = nullptr;\s*std::rethrow_exception\(pendingException\);\s*\}"),
            rethrowBody);

        string createBody = ExtractMethodBody(windowSource, "void Win32Window::Create(");
        Assert.Matches(
            new Regex(@"CreateNormalWindow\(\);\s*\}\s*RethrowPendingException\(\);\s*if \(Handle == nullptr\) \{"),
            createBody);
    }

    /// <summary>
    /// Verifies every <c>DispatchMessageW</c> in the player is followed straight away by
    /// <c>MainWindow->RethrowPendingException()</c>, so a handler failure reaches <c>Run()</c>'s fatal handler through
    /// ordinary C++ frames, and that the pump gains no other call.
    /// </summary>
    [Fact]
    public void Win32Application_rethrows_pending_window_exceptions_after_each_dispatch() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        int dispatchCount = Regex.Matches(applicationSource, @"DispatchMessageW\(").Count;
        Assert.True(dispatchCount >= 1, "The player must dispatch window messages.");
        Assert.Equal(dispatchCount, Regex.Matches(applicationSource, @"DispatchMessageW\(&message\);\s*MainWindow->RethrowPendingException\(\);").Count);

        string pumpBody = ExtractMethodBody(applicationSource, "bool Win32Application::PumpMessages(");
        Assert.Matches(
            new Regex(
                @"while \(PeekMessageW\(&message, nullptr, 0, 0, PM_REMOVE\)\) \{\s*"
                + @"if \(message\.message == WM_QUIT\) \{\s*ExitCode = static_cast<int>\(message\.wParam\);\s*return false;\s*\}\s*"
                + @"TranslateMessage\(&message\);\s*DispatchMessageW\(&message\);\s*MainWindow->RethrowPendingException\(\);\s*\}\s*"
                + @"return true;\s*$"),
            pumpBody);
    }

    /// <summary>
    /// Verifies the sizing helper is its own static-only class: <c>OuterSizeForClient</c> sizes the client rectangle
    /// with <c>AdjustWindowRectExForDpi</c> and throws on failure, <c>PlacementForDpiChange</c> keeps the suggested
    /// top-left and uses that outer size, and the native build compiles it.
    /// </summary>
    [Fact]
    public void Win32DpiWindowSizing_computes_outer_sizes_with_AdjustWindowRectExForDpi() {
        string sizingHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_dpi_window_sizing.hpp");
        string sizingSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_dpi_window_sizing.cpp");

        Assert.Contains("class Win32DpiWindowSizing {", sizingHeader, StringComparison.Ordinal);
        Assert.Single(Regex.Matches(sizingHeader, @"\bclass\s+\w+\s*\{"));
        Assert.Contains(
            "static SIZE OuterSizeForClient(int clientWidth, int clientHeight, DWORD style, DWORD exStyle, UINT dpi);",
            sizingHeader, StringComparison.Ordinal);
        Assert.Contains(
            "static RECT PlacementForDpiChange(const RECT& suggestedRectangle, int clientWidth, int clientHeight, DWORD style, DWORD exStyle, UINT newDpi);",
            sizingHeader, StringComparison.Ordinal);

        string outerSizeBody = ExtractMethodBody(sizingSource, "SIZE Win32DpiWindowSizing::OuterSizeForClient(");
        Assert.Contains("RECT windowRectangle { 0, 0, clientWidth, clientHeight };", outerSizeBody, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(
                @"if \(!AdjustWindowRectExForDpi\(&windowRectangle, style, FALSE, exStyle, dpi\)\) \{\s*"
                + @"DWORD errorCode = GetLastError\(\);[^}]*AdjustWindowRectExForDpi failed[^}]*errorCode[^}]*throw std::runtime_error\("),
            outerSizeBody);

        string placementBody = ExtractMethodBody(sizingSource, "RECT Win32DpiWindowSizing::PlacementForDpiChange(");
        Assert.Contains("SIZE outerSize = OuterSizeForClient(clientWidth, clientHeight, style, exStyle, newDpi);", placementBody, StringComparison.Ordinal);
        Assert.Matches(
            new Regex(
                @"suggestedRectangle\.left,\s*suggestedRectangle\.top,\s*suggestedRectangle\.left \+ outerSize\.cx,\s*suggestedRectangle\.top \+ outerSize\.cy"),
            placementBody);

        Assert.Contains("src/platform/windows/win32/win32_dpi_window_sizing.cpp", ReadRepositoryFile("CMakeLists.txt"), StringComparison.Ordinal);
    }

    /// <summary>
    /// Converts CRLF line endings to LF so literal expectations compare equal regardless of the checkout's line
    /// endings.
    /// </summary>
    /// <param name="text">Text that may contain CRLF line endings.</param>
    /// <returns>The text with LF line endings only.</returns>
    static string NormalizeLineEndings(string text) {
        return text.Replace("\r\n", "\n", StringComparison.Ordinal);
    }

    /// <summary>
    /// Returns the text of one C++ method definition from its qualified name up to the closing brace that ends a
    /// namespace-level member definition.
    /// </summary>
    /// <param name="source">Full C++ source text; CRLF line endings are normalized to LF first.</param>
    /// <param name="qualifiedNamePrefix">Qualified method name including the opening parenthesis.</param>
    /// <returns>The method definition text.</returns>
    static string ExtractMethodBody(string source, string qualifiedNamePrefix) {
        string normalizedSource = NormalizeLineEndings(source);
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
