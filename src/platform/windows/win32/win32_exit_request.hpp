#pragma once

#include <stdexcept>
#include <string>

namespace helengine::windows {
    /// Signals a deliberate, non-crash player shutdown with a specific process exit code (for example a
    /// requested scene that is missing from the packaged catalog). Win32Application::Run catches it before
    /// generic exceptions and returns GetExitCode() instead of the generic failure code.
    class Win32ExitRequest : public std::runtime_error {
    public:
        /// Creates an exit request carrying the process exit code and a readable reason for the startup log.
        Win32ExitRequest(int exitCode, const std::string& message);

        /// Gets the process exit code the player should return.
        int GetExitCode() const;

    private:
        /// Stores the process exit code the player should return.
        int ExitCode;
    };
}
