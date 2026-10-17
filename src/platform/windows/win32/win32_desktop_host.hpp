#pragma once

#include <Windows.h>
#include <vector>

namespace helengine::windows {
    /// Resolves Explorer's wallpaper layer and creates only player-owned, noninteractive children in that layer.
    /// Both the classic top-level WorkerW and the raised Windows 11 Progman layout are supported explicitly.
    class Win32DesktopHost {
    public:
        /// Resolves the running Explorer desktop, requesting its wallpaper worker once when necessary.
        static Win32DesktopHost Resolve();

        /// Resolves a specific program manager without sending shell messages; also permits isolated topology tests.
        static Win32DesktopHost ResolveFromProgramManager(HWND programManager);

        /// Enumerates physical monitor rectangles at startup, placing the primary display first.
        static std::vector<RECT> GetMonitorRectangles();

        /// Reads client dimensions in the child window\'s DPI context so renderer surfaces use native pixel sizes.
        static RECT ReadClientRectangle(HWND child);

        /// Reads unvirtualized screen bounds for desktop diagnostics regardless of the caller\'s DPI policy.
        static RECT ReadPhysicalWindowRectangle(HWND child);

        /// Creates an opaque, disabled child directly in the shell parent's DPI context using physical screen bounds.
        /// No Explorer-owned window is reparented, hidden, resized, or destroyed.
        HWND CreateChild(const wchar_t* className, const wchar_t* title, HINSTANCE instance, void* state, const RECT& bounds) const;

        /// Shows the owned child immediately below the icons without activating it or taking keyboard focus.
        void ShowChild(HWND child) const;

        /// Rejects a detached child or a destroyed shell topology; shell restarts are not repaired at runtime.
        void ValidateAttachment(HWND child) const;

    private:
        /// Saves a fully resolved topology; missing required shell handles are rejected by the resolver.
        Win32DesktopHost(HWND parent, HWND icons, HWND worker, bool raisedDesktop);

        /// Finds a wallpaper worker in the same process as the supplied program manager.
        static HWND FindLegacyWorker(HWND programManager, HWND& icons);

        /// Collects one monitor rectangle; reports callback failures to the caller without unwinding through user32.
        static BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC device, LPRECT rectangle, LPARAM state);

        /// Stores the Explorer-owned parent of the player's child window.
        HWND Parent;

        /// Stores the shell icon view that must remain above every desktop player window.
        HWND Icons;

        /// Stores the shell wallpaper worker whose existence identifies the supported topology.
        HWND Worker;

        /// Records whether icons and wallpaper are children of a no-redirection Progman.
        bool RaisedDesktop;
    };
}
