#pragma once

namespace helengine::windows {
    /// Selects how the Windows render bridge treats the alpha channel of the back buffer. It is applied once at
    /// startup from the resolved window mode and never changes afterwards.
    enum class Win32RenderAlphaMode {
        /// The normal window mode: the back buffer's alpha is ignored by presentation, so clears and blends keep
        /// exactly the straight-alpha behavior the player has always had.
        Straight,

        /// The overlay window mode: DWM composes the back buffer as premultiplied alpha, so clears premultiply their
        /// color (or clear to fully transparent), and both the 2D draws and each camera's 3D pass blend with the
        /// premultiplied-destination src-over state (color SRC_ALPHA / INV_SRC_ALPHA, alpha ONE / INV_SRC_ALPHA),
        /// which writes the material's alpha so opaque content is opaque and translucent content stays translucent.
        Premultiplied
    };
}
