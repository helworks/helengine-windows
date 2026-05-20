#include "platform/windows/input/windows_gamepad_backend_mux.hpp"

#include <iostream>
#include <memory>
#include <string>

#if defined(HELENGINE_WINDOWS_ENABLE_DIRECTINPUT) && HELENGINE_WINDOWS_ENABLE_DIRECTINPUT
#include "platform/windows/input/windows_directinput_backend.hpp"
#endif
#include "platform/windows/input/windows_gamepad_dedupe_service.hpp"
#if defined(HELENGINE_WINDOWS_ENABLE_STEAMINPUT) && HELENGINE_WINDOWS_ENABLE_STEAMINPUT
#include "platform/windows/input/windows_steam_input_backend.hpp"
#endif
#if defined(HELENGINE_WINDOWS_ENABLE_XINPUT) && HELENGINE_WINDOWS_ENABLE_XINPUT
#include "platform/windows/input/windows_xinput_backend.hpp"
#endif

#if __has_include("runtime/runtime_input_backends_manifest.hpp")
#include "runtime/runtime_input_backends_manifest.hpp"
#define HELENGINE_WINDOWS_HAS_RUNTIME_INPUT_BACKENDS_MANIFEST 1
#else
#define HELENGINE_WINDOWS_HAS_RUNTIME_INPUT_BACKENDS_MANIFEST 0
#endif

namespace helengine::windows {
    namespace {
        /// Returns whether XInput support was embedded in the current player build.
        bool ShouldEmbedXInput() {
#if HELENGINE_WINDOWS_HAS_RUNTIME_INPUT_BACKENDS_MANIFEST
            return he_runtime_embed_xinput();
#else
            return HELENGINE_WINDOWS_ENABLE_XINPUT != 0;
#endif
        }

        /// Returns whether DirectInput support was embedded in the current player build.
        bool ShouldEmbedDirectInput() {
#if HELENGINE_WINDOWS_HAS_RUNTIME_INPUT_BACKENDS_MANIFEST
            return he_runtime_embed_directinput();
#else
            return HELENGINE_WINDOWS_ENABLE_DIRECTINPUT != 0;
#endif
        }

        /// Returns whether SteamInput support was embedded in the current player build.
        bool ShouldEmbedSteamInput() {
#if HELENGINE_WINDOWS_HAS_RUNTIME_INPUT_BACKENDS_MANIFEST
            return he_runtime_embed_steaminput();
#else
            return HELENGINE_WINDOWS_ENABLE_STEAMINPUT != 0;
#endif
        }
    }

    /// Creates one gamepad backend multiplexer bound to the host window.
    WindowsGamepadBackendMux::WindowsGamepadBackendMux(Win32Window* window)
        : Window(window)
        , Backends()
        , Initialized(false) {
    }

    /// Captures one merged and deduplicated gamepad frame.
    void WindowsGamepadBackendMux::CaptureFrame(List<InputGamepadState>& mergedGamepads) {
        EnsureInitialized();

        List<WindowsGamepadDeviceState> candidates;
        for (std::size_t index = 0; index < Backends.size(); index++) {
            Backends[index]->CaptureFrame(candidates);
        }

        WindowsGamepadDedupeService::MergeAndDedupe(candidates, mergedGamepads);
    }

    /// Initializes the enabled backend set the first time gamepad capture is requested.
    void WindowsGamepadBackendMux::EnsureInitialized() {
        if (Initialized) {
            return;
        }

        Initialized = true;
#if defined(HELENGINE_WINDOWS_ENABLE_STEAMINPUT) && HELENGINE_WINDOWS_ENABLE_STEAMINPUT
        if (ShouldEmbedSteamInput()) {
            TryAddBackend(std::make_unique<WindowsSteamInputBackend>());
        }
#endif

#if defined(HELENGINE_WINDOWS_ENABLE_XINPUT) && HELENGINE_WINDOWS_ENABLE_XINPUT
        if (ShouldEmbedXInput()) {
            TryAddBackend(std::make_unique<WindowsXInputBackend>());
        }
#endif

#if defined(HELENGINE_WINDOWS_ENABLE_DIRECTINPUT) && HELENGINE_WINDOWS_ENABLE_DIRECTINPUT
        if (ShouldEmbedDirectInput()) {
            HWND windowHandle = Window == nullptr ? nullptr : Window->GetHandle();
            TryAddBackend(std::make_unique<WindowsDirectInputBackend>(windowHandle));
        }
#endif
    }

    /// Adds one backend when it initialized successfully and logs failures otherwise.
    void WindowsGamepadBackendMux::TryAddBackend(std::unique_ptr<IWindowsGamepadBackend> backend) {
        if (backend == nullptr) {
            return;
        }

        std::string failureMessage;
        if (!backend->TryInitialize(failureMessage)) {
            std::cout << "[Input] Disabled backend due to initialization failure: " << failureMessage << std::endl;
            return;
        }

        Backends.push_back(std::move(backend));
    }
}
