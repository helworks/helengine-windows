#include "platform/windows/win32/win32_desktop_host.hpp"

#include <stdexcept>
#include <string>
#include "platform/windows/win32/win32_window_style.hpp"

namespace helengine::windows {
    /// Records the resolved shell handles without taking ownership of them.
    Win32DesktopHost::Win32DesktopHost(HWND parent, HWND icons, HWND worker, bool raisedDesktop)
        : Parent(parent), Icons(icons), Worker(worker), RaisedDesktop(raisedDesktop) {
    }

    /// Validates the Explorer program manager and initializes the wallpaper worker only when it is absent.
    Win32DesktopHost Win32DesktopHost::Resolve() {
        HWND programManager = FindWindowW(L"Progman", nullptr);
        HWND shell = GetShellWindow();
        DWORD programManagerProcess = 0;
        DWORD shellProcess = 0;
        GetWindowThreadProcessId(programManager, &programManagerProcess);
        GetWindowThreadProcessId(shell, &shellProcess);
        if (programManager == nullptr || shell == nullptr || programManagerProcess == 0 || programManagerProcess != shellProcess) {
            throw std::runtime_error("Desktop mode requires a running Explorer program manager.");
        }
        HWND icons = nullptr;
        bool raised = (GetWindowLongPtrW(programManager, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0;
        HWND worker = raised ? FindWindowExW(programManager, nullptr, L"WorkerW", nullptr) : FindLegacyWorker(programManager, icons);
        if (worker == nullptr) {
            DWORD_PTR result = 0;
            if (!SendMessageTimeoutW(programManager, 0x052C, 0xD, 1, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result)) {
                throw std::runtime_error("Explorer did not initialize its desktop wallpaper layer within one second.");
            }
        }
        return ResolveFromProgramManager(programManager);
    }

    /// Resolves the modern sibling arrangement or a classic worker behind the icon-bearing top-level window.
    Win32DesktopHost Win32DesktopHost::ResolveFromProgramManager(HWND programManager) {
        wchar_t className[64] {};
        if (!IsWindow(programManager) || !GetClassNameW(programManager, className, 64) || std::wstring(className) != L"Progman") {
            throw std::invalid_argument("Desktop host resolution requires a live Progman window.");
        }
        bool raised = (GetWindowLongPtrW(programManager, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0;
        HWND icons = nullptr;
        HWND worker = nullptr;
        if (raised) {
            icons = FindWindowExW(programManager, nullptr, L"SHELLDLL_DefView", nullptr);
            worker = FindWindowExW(programManager, nullptr, L"WorkerW", nullptr);
        } else {
            worker = FindLegacyWorker(programManager, icons);
        }
        if (icons == nullptr || worker == nullptr) {
            throw std::runtime_error("Unsupported Explorer desktop topology: both the icon view and wallpaper WorkerW are required.");
        }
        return Win32DesktopHost(raised ? programManager : worker, icons, worker, raised);
    }

    /// Finds the next top-level WorkerW below the icon host, never selecting a window owned by another process.
    HWND Win32DesktopHost::FindLegacyWorker(HWND programManager, HWND& icons) {
        DWORD shellProcess = 0;
        GetWindowThreadProcessId(programManager, &shellProcess);
        for (HWND host = GetTopWindow(nullptr); host != nullptr; host = GetWindow(host, GW_HWNDNEXT)) {
            DWORD process = 0;
            GetWindowThreadProcessId(host, &process);
            if (process != shellProcess) { continue; }
            HWND view = FindWindowExW(host, nullptr, L"SHELLDLL_DefView", nullptr);
            if (view == nullptr) { continue; }
            icons = view;
            for (HWND candidate = GetWindow(host, GW_HWNDNEXT); candidate != nullptr; candidate = GetWindow(candidate, GW_HWNDNEXT)) {
                wchar_t className[64] {};
                GetWindowThreadProcessId(candidate, &process);
                if (process == shellProcess && GetClassNameW(candidate, className, 64) && std::wstring(className) == L"WorkerW"
                    && FindWindowExW(candidate, nullptr, L"SHELLDLL_DefView", nullptr) == nullptr) {
                    return candidate;
                }
            }
            return nullptr;
        }
        return nullptr;
    }

    /// Enumerates in a per-monitor context so profile defaults cannot virtualize monitor coverage.
    std::vector<RECT> Win32DesktopHost::GetMonitorRectangles() {
        DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (previous == nullptr) { throw std::runtime_error("Unable to enumerate desktop monitors in physical pixels."); }
        std::vector<RECT> rectangles;
        SetLastError(ERROR_SUCCESS);
        BOOL collected = EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&rectangles));
        DWORD error = GetLastError();
        if (SetThreadDpiAwarenessContext(previous) == nullptr) {
            throw std::runtime_error("Unable to restore thread DPI awareness after desktop monitor enumeration.");
        }
        if (!collected || rectangles.empty()) {
            throw std::runtime_error("Desktop monitor enumeration failed with Win32 error " + std::to_string(error) + ".");
        }
        return rectangles;
    }

    /// Obtains the full display rectangle, not its taskbar-excluding work area, with the primary display first.
    BOOL CALLBACK Win32DesktopHost::CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM state) {
        MONITORINFO info {};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) { return FALSE; }
        auto& rectangles = *reinterpret_cast<std::vector<RECT>*>(state);
        try {
            if ((info.dwFlags & MONITORINFOF_PRIMARY) != 0) { rectangles.insert(rectangles.begin(), info.rcMonitor); }
            else { rectangles.push_back(info.rcMonitor); }
        } catch (...) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        return TRUE;
    }

    /// Queries client dimensions using the window's awareness, restoring the caller even when the query fails.
    RECT Win32DesktopHost::ReadClientRectangle(HWND child) {
        DPI_AWARENESS_CONTEXT context = GetWindowDpiAwarenessContext(child);
        DPI_AWARENESS_CONTEXT previous = context == nullptr ? nullptr : SetThreadDpiAwarenessContext(context);
        if (previous == nullptr) { throw std::runtime_error("Unable to read desktop client dimensions in the window's DPI context."); }
        RECT bounds {};
        BOOL read = GetClientRect(child, &bounds);
        DWORD error = GetLastError();
        if (SetThreadDpiAwarenessContext(previous) == nullptr) { throw std::runtime_error("Unable to restore DPI awareness after a desktop client query."); }
        if (!read) { throw std::runtime_error("Desktop client query failed with Win32 error " + std::to_string(error) + "."); }
        return bounds;
    }

    /// Queries physical screen coordinates explicitly, independent of the process's default awareness.
    RECT Win32DesktopHost::ReadPhysicalWindowRectangle(HWND child) {
        DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (previous == nullptr) { throw std::runtime_error("Unable to read physical desktop bounds."); }
        RECT bounds {};
        BOOL read = GetWindowRect(child, &bounds);
        DWORD error = GetLastError();
        if (SetThreadDpiAwarenessContext(previous) == nullptr) { throw std::runtime_error("Unable to restore DPI awareness after a physical desktop query."); }
        if (!read) { throw std::runtime_error("Desktop bounds query failed with Win32 error " + std::to_string(error) + "."); }
        return bounds;
    }

    /// Creates a disabled layered child using the shell parent's own DPI context, avoiding cross-process SetParent.
    HWND Win32DesktopHost::CreateChild(const wchar_t* className, const wchar_t* title, HINSTANCE instance, void* state, const RECT& bounds) const {
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top || !IsWindow(Parent)) {
            throw std::invalid_argument("Desktop windows require positive physical bounds and a live shell parent.");
        }
        DPI_AWARENESS_CONTEXT context = GetWindowDpiAwarenessContext(Parent);
        DPI_AWARENESS_CONTEXT previous = context == nullptr ? nullptr : SetThreadDpiAwarenessContext(context);
        if (previous == nullptr) { throw std::runtime_error("Unable to use the desktop parent's DPI context."); }
        POINT corners[2] { { bounds.left, bounds.top }, { bounds.right, bounds.bottom } };
        SetLastError(ERROR_SUCCESS);
        // Per-monitor aware parents already use physical coordinates, including points outside a hidden test parent.
        BOOL converted = GetAwarenessFromDpiAwarenessContext(context) == DPI_AWARENESS_PER_MONITOR_AWARE
            || (PhysicalToLogicalPointForPerMonitorDPI(Parent, &corners[0])
                && PhysicalToLogicalPointForPerMonitorDPI(Parent, &corners[1]));
        DWORD error = GetLastError();
        int mapped = 0;
        if (converted) {
            SetLastError(ERROR_SUCCESS);
            mapped = MapWindowPoints(nullptr, Parent, corners, 2);
            error = GetLastError();
        }
        HWND child = nullptr;
        if (converted && (mapped != 0 || error == ERROR_SUCCESS)) {
            Win32WindowStyle style = Win32WindowStyle::Desktop();
            child = CreateWindowExW(style.GetExStyle(), className, title, style.GetStyle(), corners[0].x, corners[0].y,
                corners[1].x - corners[0].x, corners[1].y - corners[0].y, Parent, nullptr, instance, state);
            error = GetLastError();
        }
        if (SetThreadDpiAwarenessContext(previous) == nullptr) {
            if (child != nullptr) { DestroyWindow(child); }
            throw std::runtime_error("Unable to restore thread DPI awareness after desktop window creation.");
        }
        if (child == nullptr) {
            throw std::runtime_error("Desktop child creation failed with Win32 error " + std::to_string(error) + ".");
        }
        if (!SetLayeredWindowAttributes(child, 0, 255, LWA_ALPHA)) {
            error = GetLastError();
            DestroyWindow(child);
            throw std::runtime_error("Desktop opacity initialization failed with Win32 error " + std::to_string(error) + ".");
        }
        return child;
    }

    /// Places the player below the icon sibling and above Explorer's own wallpaper, with no activation.
    void Win32DesktopHost::ShowChild(HWND child) const {
        ValidateAttachment(child);
        HWND insertAfter = RaisedDesktop ? Icons : HWND_TOP;
        if (!SetWindowPos(child, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
            throw std::runtime_error("Desktop layer placement failed with Win32 error " + std::to_string(GetLastError()) + ".");
        }
    }

    /// Stops rendering when Explorer loses the supported parent rather than drawing a detached foreground window.
    void Win32DesktopHost::ValidateAttachment(HWND child) const {
        if (!IsWindow(Parent) || !IsWindow(Icons) || !IsWindow(Worker) || !IsWindow(child) || GetParent(child) != Parent) {
            throw std::runtime_error("Explorer desktop attachment was lost; restart the player after the shell is ready.");
        }
    }
}
