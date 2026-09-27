#include "platform/windows/win32/win32_window.hpp"

#include "platform/windows/win32/win32_activity_tracker.hpp"
#include "platform/windows/win32/win32_dpi_window_sizing.hpp"

#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    /// Creates a window wrapper with a title, a requested position and client size, and the style set that decides
    /// how the window is created and shown.
    /// <param name="title">Native window title.</param>
    /// <param name="left">Screen x of the window's top-left corner; only the overlay style uses it, a normal window
    /// keeps the CW_USEDEFAULT placement.</param>
    /// <param name="top">Screen y of the window's top-left corner; only the overlay style uses it.</param>
    /// <param name="width">Requested client width in pixels.</param>
    /// <param name="height">Requested client height in pixels.</param>
    /// <param name="windowStyle">Normal or overlay style set.</param>
    Win32Window::Win32Window(const wchar_t* title, int left, int top, int width, int height, const Win32WindowStyle& windowStyle)
        : Title(title)
        , Left(left)
        , Top(top)
        , Width(width)
        , Height(height)
        , WindowStyle(windowStyle)
        , Handle(nullptr)
        , MouseWheelDelta(0)
        , ActivityTracker(nullptr)
        , DpiAwareness(Win32DpiAwareness::Unaware) {
    }

    /// Releases the native window if it is still alive.
    Win32Window::~Win32Window() {
        if (Handle != nullptr) {
            DestroyWindow(Handle);
            Handle = nullptr;
        }
    }

    /// Registers the window class and creates the native window through the normal or overlay path chosen by the
    /// window style. When the process is Per-Monitor v2 aware and the window is a normal window, the window is then
    /// resized so its client area is the requested size in physical pixels at the window's DPI.
    void Win32Window::Create() {
        RegisterWindowClass();

        // The creation messages (WM_SIZE) overwrite Width and Height with the real client size, so the requested size
        // is kept for the Per-Monitor v2 correction.
        int requestedClientWidth = Width;
        int requestedClientHeight = Height;
        if (WindowStyle.GetWindowMode() == Win32WindowMode::Overlay) {
            CreateOverlayWindow();
        } else {
            CreateNormalWindow();
        }

        if (Handle == nullptr) {
            throw std::runtime_error("CreateWindowExW failed for the HelEngine Windows host.");
        }

        RefreshClientSize();
        if (DpiAwareness == Win32DpiAwareness::PerMonitorV2 && WindowStyle.GetWindowMode() == Win32WindowMode::Normal) {
            CorrectNormalWindowSizeForDpi(requestedClientWidth, requestedClientHeight);
        }
    }

    /// Shows the native window through the normal or overlay path chosen by the window style.
    void Win32Window::Show() const {
        if (WindowStyle.GetWindowMode() == Win32WindowMode::Overlay) {
            ShowOverlayWindow();
        } else {
            ShowNormalWindow();
        }
    }

    /// Creates today's ordinary WS_OVERLAPPEDWINDOW window at the default placement, with exactly the calls the
    /// player has always made.
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
    }

    /// Creates the borderless overlay window at the requested position, sized with AdjustWindowRectEx for the
    /// overlay style set.
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
    }

    /// Resizes a freshly created Per-Monitor v2 normal window so its client area is exactly the requested size in
    /// physical pixels at the window's own DPI: the creation path sizes the frame with AdjustWindowRect, which does not
    /// know the monitor the window landed on. The outer size comes from Win32DpiWindowSizing at the DPI read from the
    /// window; the window is resized only when that size differs from its current outer size, never moved, and the
    /// cached client size is then refreshed. Throws std::runtime_error when the DPI, the window rectangle or the resize
    /// cannot be obtained or applied.
    /// <param name="requestedClientWidth">Client width in physical pixels requested before creation.</param>
    /// <param name="requestedClientHeight">Client height in physical pixels requested before creation.</param>
    void Win32Window::CorrectNormalWindowSizeForDpi(int requestedClientWidth, int requestedClientHeight) {
        UINT windowDpi = GetDpiForWindow(Handle);
        if (windowDpi == 0) {
            throw std::runtime_error("GetDpiForWindow returned no DPI for the HelEngine Windows host window.");
        }

        SIZE outerSize = Win32DpiWindowSizing::OuterSizeForClient(requestedClientWidth, requestedClientHeight, WS_OVERLAPPEDWINDOW, 0, windowDpi);
        RECT windowRectangle {};
        if (!GetWindowRect(Handle, &windowRectangle)) {
            DWORD errorCode = GetLastError();
            std::ostringstream messageBuilder;
            messageBuilder << "GetWindowRect failed for the HelEngine Windows host window with Win32 error " << errorCode << ".";
            throw std::runtime_error(messageBuilder.str());
        }

        LONG currentWidth = windowRectangle.right - windowRectangle.left;
        LONG currentHeight = windowRectangle.bottom - windowRectangle.top;
        if (outerSize.cx != currentWidth || outerSize.cy != currentHeight) {
            if (!SetWindowPos(Handle, nullptr, 0, 0, outerSize.cx, outerSize.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
                DWORD errorCode = GetLastError();
                std::ostringstream messageBuilder;
                messageBuilder << "SetWindowPos failed while sizing the HelEngine Windows host window for its DPI with Win32 error " << errorCode << ".";
                throw std::runtime_error(messageBuilder.str());
            }
            RefreshClientSize();
        }
    }

    /// Shows today's ordinary window and brings it to the foreground with keyboard focus, exactly as the player
    /// has always done.
    void Win32Window::ShowNormalWindow() const {
        ShowWindow(Handle, SW_SHOWDEFAULT);
        UpdateWindow(Handle);
        BringWindowToTop(Handle);
        SetActiveWindow(Handle);
        SetForegroundWindow(Handle);
        SetFocus(Handle);
    }

    /// Shows the overlay window without activating it and pins it topmost with SWP_NOMOVE | SWP_NOSIZE |
    /// SWP_NOACTIVATE: the bounds were already set when the window was created, so this call only changes the
    /// z-order and can never shrink the client area. It never calls a foreground or focus function, so the user's
    /// foreground window keeps focus. Throws std::runtime_error when SetWindowPos fails.
    void Win32Window::ShowOverlayWindow() const {
        ShowWindow(Handle, WindowStyle.GetShowCommand());
        if (!SetWindowPos(Handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) {
            std::ostringstream messageBuilder;
            messageBuilder << "SetWindowPos failed for the HelEngine Windows overlay window with Win32 error " << GetLastError() << ".";
            throw std::runtime_error(messageBuilder.str());
        }
    }

    /// Gets the native window handle.
    HWND Win32Window::GetHandle() const {
        return Handle;
    }

    /// Gets the current client width in pixels.
    int Win32Window::GetClientWidth() const {
        return Width;
    }

    /// Gets the current client height in pixels.
    int Win32Window::GetClientHeight() const {
        return Height;
    }

    /// Returns and clears the accumulated mouse-wheel delta since the last input poll.
    int Win32Window::ConsumeMouseWheelDelta() {
        int mouseWheelDelta = MouseWheelDelta;
        MouseWheelDelta = 0;
        return mouseWheelDelta;
    }

    /// Attaches the activity tracker that observes every message this window receives; the window does not own
    /// it. Only the opt-in idle throttle attaches one, and it must be attached before Create() so the creation
    /// messages are observed too.
    /// <param name="tracker">Tracker to notify about each received message.</param>
    void Win32Window::SetActivityTracker(Win32ActivityTracker* tracker) {
        ActivityTracker = tracker;
    }

    /// Records the DPI awareness the process applied, which decides whether Create() corrects a normal window's size
    /// for its DPI. It must be called before Create(); the default is Win32DpiAwareness::Unaware, which keeps today's
    /// creation calls unchanged.
    /// <param name="dpiAwareness">DPI awareness the process applied before any window was created.</param>
    void Win32Window::SetDpiAwareness(Win32DpiAwareness dpiAwareness) {
        DpiAwareness = dpiAwareness;
    }

    /// Handles window messages for this instance, first reporting each one to the attached activity tracker when
    /// there is one; the message handling and return values do not depend on the tracker. WM_DPICHANGED, which only a
    /// Per-Monitor v2 aware window receives, moves a normal window to the suggested top-left while keeping its client
    /// pixel size, leaves an overlay's rectangle unchanged, and returns 0 in both modes.
    LRESULT Win32Window::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        if (ActivityTracker != nullptr) {
            ActivityTracker->ObserveMessage(message);
        }

        switch (message) {
            case WM_SIZE:
                RefreshClientSize();
                return 0;

            case WM_MOUSEWHEEL:
                MouseWheelDelta += GET_WHEEL_DELTA_WPARAM(wParam);
                return 0;

            case WM_DPICHANGED:
                // The profile resolution is the client size in physical pixels, so a normal window takes the suggested
                // top-left but keeps its client pixel size at the new DPI; an overlay keeps its rectangle.
                if (WindowStyle.GetWindowMode() == Win32WindowMode::Normal) {
                    RECT placement = Win32DpiWindowSizing::PlacementForDpiChange(*reinterpret_cast<RECT*>(lParam), GetClientWidth(), GetClientHeight(), WS_OVERLAPPEDWINDOW, 0, HIWORD(wParam));
                    SetWindowPos(Handle, nullptr, placement.left, placement.top, placement.right - placement.left, placement.bottom - placement.top, SWP_NOZORDER | SWP_NOACTIVATE);
                }
                return 0;

            case WM_DESTROY:
                Handle = nullptr;
                PostQuitMessage(0);
                return 0;
        }

        return DefWindowProcW(Handle, message, wParam, lParam);
    }

    /// Registers the native window class used by the player host.
    void Win32Window::RegisterWindowClass() {
        WNDCLASSEXW windowClass {};
        windowClass.cbSize = sizeof(WNDCLASSEXW);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.lpszClassName = L"HelEngineWindowClass";

        ATOM classAtom = RegisterClassExW(&windowClass);
        if (classAtom == 0) {
            DWORD errorCode = GetLastError();
            if (errorCode != ERROR_CLASS_ALREADY_EXISTS) {
                throw std::runtime_error("RegisterClassExW failed for the HelEngine Windows host.");
            }
        }
    }

    /// Updates the cached client size from the current native window state.
    void Win32Window::RefreshClientSize() {
        RECT clientRectangle {};
        if (GetClientRect(Handle, &clientRectangle)) {
            Width = clientRectangle.right - clientRectangle.left;
            Height = clientRectangle.bottom - clientRectangle.top;
        }
    }

    /// Bridges the Win32 callback signature to the stored window instance.
    LRESULT CALLBACK Win32Window::WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* window = static_cast<Win32Window*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            window->Handle = handle;
        }

        auto* window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (window != nullptr) {
            return window->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(handle, message, wParam, lParam);
    }
}
