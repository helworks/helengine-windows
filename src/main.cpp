#include <Windows.h>

#include "platform/windows/win32/win32_application.hpp"

/// Boots the packaged Windows player through the GUI subsystem entrypoint.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    helengine::windows::Win32Application application;
    return application.Run();
}
