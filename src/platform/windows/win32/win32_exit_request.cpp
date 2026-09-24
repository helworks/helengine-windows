#include "platform/windows/win32/win32_exit_request.hpp"

namespace helengine::windows {
    /// Creates an exit request carrying the process exit code and a readable reason for the startup log.
    Win32ExitRequest::Win32ExitRequest(int exitCode, const std::string& message)
        : std::runtime_error(message),
          ExitCode(exitCode) {
    }

    /// Gets the process exit code the player should return.
    int Win32ExitRequest::GetExitCode() const {
        return ExitCode;
    }
}
