#pragma once

namespace helengine::windows {
    class DirectX11Bootstrap;

    /// Owns the minimal clear-and-present path for the first Windows DirectX11 host slice.
    class DirectX11Presenter {
    public:
        /// Creates a presenter bound to one DirectX11 bootstrap.
        explicit DirectX11Presenter(DirectX11Bootstrap& bootstrap);

        /// Releases the presenter without owning the bootstrap resources.
        ~DirectX11Presenter();

        /// Binds the back buffer, clears to black, and presents one frame.
        void RenderFrame();

    private:
        /// Stores the DirectX11 bootstrap used for rendering and presentation.
        DirectX11Bootstrap& Bootstrap;
    };
}
