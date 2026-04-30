#include "platform/windows/win32/win32_window.hpp"

#include <stdexcept>

namespace helengine::windows {
    /// Creates a window wrapper with a title and requested client size.
    Win32Window::Win32Window(const wchar_t* title, int width, int height)
        : Title(title)
        , Width(width)
        , Height(height)
        , Handle(nullptr) {
    }

    /// Releases the native window if it is still alive.
    Win32Window::~Win32Window() {
        if (Handle != nullptr) {
            DestroyWindow(Handle);
            Handle = nullptr;
        }
    }

    /// Registers the window class and creates the native window.
    void Win32Window::Create() {
        RegisterWindowClass();

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

        if (Handle == nullptr) {
            throw std::runtime_error("CreateWindowExW failed for the HelEngine Windows host.");
        }

        RefreshClientSize();
    }

    /// Shows the native window using the default show mode.
    void Win32Window::Show() const {
        ShowWindow(Handle, SW_SHOWDEFAULT);
        UpdateWindow(Handle);
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

    /// Handles window messages for this instance.
    LRESULT Win32Window::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_SIZE:
                RefreshClientSize();
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
