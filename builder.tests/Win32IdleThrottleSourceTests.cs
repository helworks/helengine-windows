using System.Text.RegularExpressions;

namespace helengine.windows.builder.tests;

/// <summary>
/// Verifies the opt-in idle throttle wiring of the Windows player: the window only reports activity when a tracker is
/// attached, the default render loop stays textually unchanged, the idle loop sleeps with
/// <c>MsgWaitForMultipleObjectsEx</c>, and the frame pacer stays a pure, Win32-free decision class.
/// </summary>
public sealed class Win32IdleThrottleSourceTests {
    /// <summary>
    /// Verifies <c>Win32Window::HandleMessage</c> forwards each message to the tracker only when one is attached, and
    /// does so before the existing message switch so every message keeps its original handling and return value.
    /// </summary>
    [Fact]
    public void Win32Window_observes_messages_only_when_a_tracker_is_attached() {
        string windowSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.cpp");
        string windowHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_window.hpp");

        Assert.Contains("void SetActivityTracker(Win32ActivityTracker* tracker);", windowHeader, StringComparison.Ordinal);
        Assert.Contains("Win32ActivityTracker* ActivityTracker;", windowHeader, StringComparison.Ordinal);
        Assert.Contains(", ActivityTracker(nullptr)", windowSource, StringComparison.Ordinal);

        string handleMessageBody = ExtractMethodBody(windowSource, "Win32Window::HandleMessage(");
        int trackerCheckIndex = handleMessageBody.IndexOf("if (ActivityTracker != nullptr) {", StringComparison.Ordinal);
        int switchIndex = handleMessageBody.IndexOf("switch (message)", StringComparison.Ordinal);
        Assert.True(trackerCheckIndex >= 0, "HandleMessage must guard the tracker call with a null check.");
        Assert.True(switchIndex >= 0, "HandleMessage must keep its message switch.");
        Assert.True(trackerCheckIndex < switchIndex, "The tracker must observe the message before the existing switch runs.");
        Assert.Matches(
            new Regex(@"if \(ActivityTracker != nullptr\) \{\s*ActivityTracker->ObserveMessage\(message\);\s*\}"),
            handleMessageBody);
        Assert.Contains("return DefWindowProcW(Handle, message, wParam, lParam);", handleMessageBody, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies <c>Run()</c> only enters the idle-throttled loop when a pacer exists and otherwise keeps today's exact
    /// default loop text, so the disabled player behaves identically.
    /// </summary>
    [Fact]
    public void Win32Application_keeps_the_default_loop_in_the_disabled_branch() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        Assert.Matches(
            new Regex(@"if \(IdleFramePacer\) \{\s*RunIdleThrottledLoop\(\);\s*\} else \{\s*while \(PumpMessages\(\)\) \{\s*RenderFrame\(\);\s*\}\s*\}"),
            applicationSource);
        Assert.Single(Regex.Matches(applicationSource, @"while \(PumpMessages\(\)\) \{"));
    }

    /// <summary>
    /// Verifies the tracker and pacer are created and attached only when the resolved settings enable the idle
    /// throttle, the tracker is attached before the native window is created, the effective configuration is logged
    /// exactly once, and the destructor detaches the tracker so the window never reaches it after it is destroyed.
    /// </summary>
    [Fact]
    public void Win32Application_attaches_the_tracker_only_when_idle_throttle_is_enabled() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");
        string applicationHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.hpp");

        Assert.Contains("std::unique_ptr<Win32ActivityTracker> ActivityTracker;", applicationHeader, StringComparison.Ordinal);
        Assert.Contains("std::unique_ptr<Win32IdleFramePacer> IdleFramePacer;", applicationHeader, StringComparison.Ordinal);
        Assert.Contains("bool CurrentFrameIsIdle;", applicationHeader, StringComparison.Ordinal);
        Assert.Contains("void RunIdleThrottledLoop();", applicationHeader, StringComparison.Ordinal);
        Assert.Contains("bool IsEngineKeepAwake() const;", applicationHeader, StringComparison.Ordinal);
        Assert.Contains("CurrentFrameIsIdle(false)", applicationSource, StringComparison.Ordinal);

        string createMainWindowBody = ExtractMethodBody(applicationSource, "Win32Application::CreateMainWindow(");
        Assert.Equal(2, Regex.Matches(applicationSource, @"SetActivityTracker\(").Count);
        Assert.Single(Regex.Matches(applicationSource, @"SetActivityTracker\(ActivityTracker\.get\(\)\)"));
        string destructorBody = ExtractMethodBody(applicationSource, "Win32Application::~Win32Application(");
        Assert.Matches(
            new Regex(@"if \(ActivityTracker\) \{\s*MainWindow->SetActivityTracker\(nullptr\);\s*\}"),
            destructorBody);
        Assert.Matches(
            new Regex(
                @"RuntimePlayerProfile profile = ResolveRuntimePlayerProfile\(\);\s*"
                + @"Win32IdleThrottleSettings idleThrottleSettings = Win32IdleThrottleSettings::Resolve\(profile, CommandLineOptions\);\s*"
                + @"MainWindow = std::make_unique<Win32Window>\(.*?\);\s*"
                + @"if \(idleThrottleSettings\.IsEnabled\(\)\) \{\s*"
                + @"ActivityTracker = std::make_unique<Win32ActivityTracker>\(\);\s*"
                + @"IdleFramePacer = std::make_unique<Win32IdleFramePacer>\(idleThrottleSettings\);\s*"
                + @"MainWindow->SetActivityTracker\(ActivityTracker\.get\(\)\);\s*"
                + @"std::string idleThrottleMessage = ""Idle throttle configured: "" \+ idleThrottleSettings\.Describe\(\);\s*"
                + @"WriteLifecycleLog\(idleThrottleMessage\.c_str\(\)\);\s*"
                + @"\}\s*"
                + @"MainWindow->Create\(\);",
                RegexOptions.Singleline),
            createMainWindowBody);
        Assert.Single(Regex.Matches(applicationSource, @"idleThrottleSettings\.Describe\(\)"));
    }

    /// <summary>
    /// Verifies the idle loop sleeps with a message-aware wait inside the existing pacing Tracy zone, pumps messages
    /// before deciding the frame mode, marks the frame mode before rendering, and reads the engine keep-awake getters.
    /// </summary>
    [Fact]
    public void Win32Application_idle_loop_waits_for_messages_and_honors_engine_keep_awake() {
        string applicationSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_application.cpp");

        string idleLoopBody = ExtractMethodBody(applicationSource, "Win32Application::RunIdleThrottledLoop(");
        Assert.Contains("HELENGINE_TRACY_ZONE_N(\"Frame.PacingAndIdle\");", idleLoopBody, StringComparison.Ordinal);
        Assert.Contains("MsgWaitForMultipleObjectsEx(0, nullptr,", idleLoopBody, StringComparison.Ordinal);
        Assert.Contains("QS_ALLINPUT", idleLoopBody, StringComparison.Ordinal);
        Assert.Contains("MWMO_INPUTAVAILABLE", idleLoopBody, StringComparison.Ordinal);
        Assert.Contains("WAIT_FAILED", idleLoopBody, StringComparison.Ordinal);

        int waitIndex = idleLoopBody.IndexOf("MsgWaitForMultipleObjectsEx(", StringComparison.Ordinal);
        int pumpIndex = idleLoopBody.IndexOf("if (!PumpMessages()) {", StringComparison.Ordinal);
        int markIndex = idleLoopBody.IndexOf("CurrentFrameIsIdle = !", StringComparison.Ordinal);
        int renderIndex = idleLoopBody.IndexOf("RenderFrame();", StringComparison.Ordinal);
        Assert.True(waitIndex >= 0 && pumpIndex > waitIndex, "The idle loop must pump messages after the wait.");
        Assert.True(markIndex > pumpIndex, "The idle loop must decide the frame mode after pumping messages.");
        Assert.True(renderIndex > markIndex, "The idle loop must mark the frame mode before rendering.");
        Assert.Contains("GetIdleFrameIntervalMilliseconds()", idleLoopBody, StringComparison.Ordinal);

        int earlyWakeIndex = idleLoopBody.IndexOf("if (!frameDecision.Active && frameDecision.WaitMilliseconds > 0) {", StringComparison.Ordinal);
        Assert.True(earlyWakeIndex > pumpIndex && earlyWakeIndex < markIndex, "A non-activity wake before the idle frame is due must go back to waiting instead of rendering.");
        Assert.Matches(
            new Regex(@"if \(!frameDecision\.Active && frameDecision\.WaitMilliseconds > 0\) \{[^}]*continue;\s*\}"),
            idleLoopBody);

        string keepAwakeBody = ExtractMethodBody(applicationSource, "Win32Application::IsEngineKeepAwake(");
        Assert.Contains("EngineCore->get_PredictedPhysicsStepSeconds() > 0.0", keepAwakeBody, StringComparison.Ordinal);
        Assert.Contains("SceneManager* sceneManager = EngineCore->get_SceneManager();", keepAwakeBody, StringComparison.Ordinal);
        Assert.Contains("sceneManager->get_IsSceneTransitionActive()", keepAwakeBody, StringComparison.Ordinal);
        Assert.Contains("sceneManager->get_LastTracePendingOperationCount() > 0", keepAwakeBody, StringComparison.Ordinal);

        Assert.DoesNotContain("CurrentFrameIsIdle", ExtractMethodBody(applicationSource, "Win32Application::Run("), StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the activity tracker treats exactly the keyboard, mouse and window messages listed in the design as
    /// activity.
    /// </summary>
    [Fact]
    public void Win32ActivityTracker_observes_every_designed_activity_message() {
        string trackerSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_activity_tracker.cpp");
        string trackerHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_activity_tracker.hpp");

        Assert.Contains("void ObserveMessage(UINT message);", trackerHeader, StringComparison.Ordinal);
        Assert.Contains("void MarkActivity();", trackerHeader, StringComparison.Ordinal);
        Assert.Contains("std::chrono::steady_clock::time_point GetLastActivity() const;", trackerHeader, StringComparison.Ordinal);

        string[] activityMessages = [
            "WM_KEYDOWN", "WM_KEYUP", "WM_SYSKEYDOWN", "WM_SYSKEYUP", "WM_CHAR",
            "WM_MOUSEMOVE",
            "WM_LBUTTONDOWN", "WM_LBUTTONUP", "WM_LBUTTONDBLCLK",
            "WM_RBUTTONDOWN", "WM_RBUTTONUP", "WM_RBUTTONDBLCLK",
            "WM_MBUTTONDOWN", "WM_MBUTTONUP", "WM_MBUTTONDBLCLK",
            "WM_XBUTTONDOWN", "WM_XBUTTONUP", "WM_XBUTTONDBLCLK",
            "WM_MOUSEWHEEL", "WM_MOUSEHWHEEL",
            "WM_SIZE", "WM_ACTIVATE", "WM_SETFOCUS", "WM_KILLFOCUS", "WM_PAINT", "WM_DISPLAYCHANGE", "WM_DPICHANGED"
        ];
        foreach (string activityMessage in activityMessages) {
            Assert.Contains("case " + activityMessage + ":", trackerSource, StringComparison.Ordinal);
        }

        Assert.Equal(activityMessages.Length, Regex.Matches(trackerSource, @"case WM_\w+:").Count);
    }

    /// <summary>
    /// Verifies the frame pacer and its decision struct are pure: they compute the idle wait from the configured frame
    /// rate and never include Win32 headers.
    /// </summary>
    [Fact]
    public void Win32IdleFramePacer_is_pure_and_computes_the_idle_interval() {
        string pacerSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_idle_frame_pacer.cpp");
        string pacerHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_idle_frame_pacer.hpp");
        string decisionHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_idle_frame_decision.hpp");

        Assert.Contains("1000 / ", pacerSource, StringComparison.Ordinal);
        Assert.Contains(
            "Win32IdleFrameDecision Decide(long long nowMs, long long lastActivityMs, long long lastFrameStartMs, bool keepAwake) const;",
            pacerHeader, StringComparison.Ordinal);
        Assert.Contains("struct Win32IdleFrameDecision {", decisionHeader, StringComparison.Ordinal);
        Assert.Contains("bool Active;", decisionHeader, StringComparison.Ordinal);
        Assert.Contains("int WaitMilliseconds;", decisionHeader, StringComparison.Ordinal);

        foreach (string pureSource in new[] { pacerSource, pacerHeader, decisionHeader }) {
            Assert.DoesNotContain("windows.h", pureSource, StringComparison.OrdinalIgnoreCase);
        }
    }

    /// <summary>
    /// Verifies the settings value type validates like the profile loader, lets the command line override the profile,
    /// and describes the effective configuration with its source.
    /// </summary>
    [Fact]
    public void Win32IdleThrottleSettings_validates_resolves_and_describes() {
        string settingsSource = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_idle_throttle_settings.cpp");
        string settingsHeader = ReadRepositoryFile("src", "platform", "windows", "win32", "win32_idle_throttle_settings.hpp");

        Assert.Contains(
            "static Win32IdleThrottleSettings Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& options);",
            settingsHeader, StringComparison.Ordinal);
        Assert.Contains("std::string Describe() const;", settingsHeader, StringComparison.Ordinal);
        Assert.Contains("std::invalid_argument", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"idleThrottle=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\" afterMs=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\" fps=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\" source=\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"profile\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"commandLine\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("\"mixed\"", settingsSource, StringComparison.Ordinal);
        Assert.Contains("options.HasIdleThrottle() ? options.GetIdleThrottleEnabled() : profile.IdleThrottleEnabled", settingsSource, StringComparison.Ordinal);
        Assert.Contains("options.HasIdleAfterMilliseconds() ? options.GetIdleAfterMilliseconds() : profile.IdleAfterMilliseconds", settingsSource, StringComparison.Ordinal);
        Assert.Contains("options.HasIdleFramesPerSecond() ? options.GetIdleFramesPerSecond() : profile.IdleFramesPerSecond", settingsSource, StringComparison.Ordinal);
    }

    /// <summary>
    /// Verifies the native build compiles every new idle-throttle source file.
    /// </summary>
    [Fact]
    public void CMakeLists_compiles_idle_throttle_sources() {
        string cmakeSource = ReadRepositoryFile("CMakeLists.txt");

        Assert.Contains("src/platform/windows/win32/win32_activity_tracker.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("src/platform/windows/win32/win32_idle_frame_pacer.cpp", cmakeSource, StringComparison.Ordinal);
        Assert.Contains("src/platform/windows/win32/win32_idle_throttle_settings.cpp", cmakeSource, StringComparison.Ordinal);
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
