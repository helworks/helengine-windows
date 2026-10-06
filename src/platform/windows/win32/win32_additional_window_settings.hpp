#pragma once

#include <string>
#include "platform/windows/win32/win32_window_mode.hpp"

namespace helengine::windows {
    /// Defines one explicitly configured secondary view of the shared engine scene.
    class Win32AdditionalWindowSettings {
    public:
        /// Parses tag,mode,left,top,width,height and rejects malformed or unsafe window tags.
        static Win32AdditionalWindowSettings Parse(const std::string& value);
        /// Gets the unique ASCII tag used by diagnostics and capture filenames.
        const std::string& GetTag() const;
        /// Gets whether the view uses ordinary or transparent overlay presentation.
        Win32WindowMode GetMode() const;
        /// Gets the requested screen-space left coordinate in pixels.
        int GetLeft() const;
        /// Gets the requested screen-space top coordinate in pixels.
        int GetTop() const;
        /// Gets the requested positive client width in pixels.
        int GetWidth() const;
        /// Gets the requested positive client height in pixels.
        int GetHeight() const;
    private:
        /// Constructs a fully validated configuration; callers use Parse.
        Win32AdditionalWindowSettings(std::string tag, Win32WindowMode mode, int left, int top, int width, int height);
        /// Parses an exact signed decimal integer and enforces positive dimensions when requested.
        static int ParseInteger(const std::string& value, bool dimension);
        /// Stores the stable diagnostic tag.
        std::string Tag;
        /// Stores the native presentation mode.
        Win32WindowMode Mode;
        /// Stores the requested left coordinate.
        int Left;
        /// Stores the requested top coordinate.
        int Top;
        /// Stores the requested client width.
        int Width;
        /// Stores the requested client height.
        int Height;
    };
}
