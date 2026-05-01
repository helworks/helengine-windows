#pragma once

#ifdef DrawText
#undef DrawText
#endif

#if __has_include("RenderManager2D.hpp")
#include "IRoundedRectDrawable2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "MaterialAsset.hpp"
#include "ModelAsset.hpp"
#include "RenderManager2D.hpp"
#include "RenderManager3D.hpp"
#include "RuntimeMaterial.hpp"
#include "RuntimeModel.hpp"
#include "RuntimeTexture.hpp"
#include "ShaderAsset.hpp"
#include "TextureAsset.hpp"
#endif

namespace helengine::windows {
#if __has_include("RenderManager2D.hpp")
    /// Provides a minimal native 3D renderer bridge so the generated core can initialize on Windows.
    class Win32RenderManager3D : public RenderManager3D {
    public:
        /// Builds a placeholder runtime model from raw asset metadata.
        RuntimeModel* BuildModelFromRaw(ModelAsset* data) override;

        /// Builds a placeholder runtime material from raw asset metadata.
        RuntimeMaterial* BuildMaterialFromRaw(MaterialAsset* materialAsset, ShaderAsset* shaderAsset) override;
    };

    /// Provides a minimal native 2D renderer bridge so the generated core can initialize on Windows.
    class Win32RenderManager2D : public RenderManager2D {
    public:
        /// Builds a placeholder runtime texture from raw asset metadata.
        RuntimeTexture* BuildTextureFromRaw(TextureAsset* data) override;

        /// Accepts a sprite draw request without issuing backend rendering yet.
        void DrawSprite(ISpriteDrawable2D* sprite) override;

        /// Accepts a text draw request without issuing backend rendering yet.
        void DrawText(ITextDrawable2D* text);

        /// Accepts a text draw request without issuing backend rendering yet when Win32 macros rename the base contract.
        void DrawTextA(ITextDrawable2D* text);

        /// Accepts a rounded-rectangle draw request without issuing backend rendering yet.
        void DrawRoundedRect(IRoundedRectDrawable2D* shape) override;
    };
#endif
}
