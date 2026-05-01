#include "platform/windows/win32/win32_render_bridge.hpp"

namespace helengine::windows {
#if __has_include("RenderManager2D.hpp")
    /// Builds a placeholder runtime model from raw asset metadata.
    RuntimeModel* Win32RenderManager3D::BuildModelFromRaw(ModelAsset* data) {
        RuntimeModel* runtimeModel = new RuntimeModel();
        if (data != nullptr) {
            runtimeModel->set_Id(data->get_Id());
        }

        return runtimeModel;
    }

    /// Builds a placeholder runtime material from raw asset metadata.
    RuntimeMaterial* Win32RenderManager3D::BuildMaterialFromRaw(MaterialAsset* materialAsset, ShaderAsset* shaderAsset) {
        RuntimeMaterial* runtimeMaterial = new RuntimeMaterial();
        if (materialAsset != nullptr) {
            runtimeMaterial->set_Id(materialAsset->get_Id());
        } else if (shaderAsset != nullptr) {
            runtimeMaterial->set_Id(shaderAsset->get_Id());
        }

        return runtimeMaterial;
    }

    /// Builds a placeholder runtime texture from raw asset metadata.
    RuntimeTexture* Win32RenderManager2D::BuildTextureFromRaw(TextureAsset* data) {
        RuntimeTexture* runtimeTexture = new RuntimeTexture();
        if (data != nullptr) {
            runtimeTexture->set_Id(data->get_Id());
            runtimeTexture->set_Width(data->Width);
            runtimeTexture->set_Height(data->Height);
        }

        return runtimeTexture;
    }

    /// Accepts a sprite draw request without issuing backend rendering yet.
    void Win32RenderManager2D::DrawSprite(ISpriteDrawable2D* sprite) {
    }

    /// Accepts a text draw request without issuing backend rendering yet.
    void Win32RenderManager2D::DrawText(ITextDrawable2D* text) {
    }

    /// Accepts a text draw request without issuing backend rendering yet when Win32 macros rename the base contract.
    void Win32RenderManager2D::DrawTextA(ITextDrawable2D* text) {
        DrawText(text);
    }

    /// Accepts a rounded-rectangle draw request without issuing backend rendering yet.
    void Win32RenderManager2D::DrawRoundedRect(IRoundedRectDrawable2D* shape) {
    }
#endif
}
