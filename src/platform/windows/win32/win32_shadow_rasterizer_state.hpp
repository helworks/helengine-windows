#pragma once
#include <d3d11.h>
namespace helengine::windows {
    // The HLSL receiver-plane tolerance uses half of the SDK-defined 1/256-pixel snapping quantum.
    static_assert(D3D11_SUBPIXEL_FRACTIONAL_BIT_COUNT==8);
    /// <summary>Describes the legacy and hard ink caster depth policies without changing culling or coverage.</summary>
    class Win32ShadowRasterizerState {
    public:
        /// <summary>Retains legacy bias while hard ink uses its receiver-plane raster error and FP32 roundoff allowance without displacing casters.</summary>
        static D3D11_RASTERIZER_DESC Describe(bool ink) {
            D3D11_RASTERIZER_DESC description{};
            description.FillMode=D3D11_FILL_SOLID;
            description.CullMode=D3D11_CULL_NONE;
            description.FrontCounterClockwise=TRUE;
            description.DepthClipEnable=TRUE;
            description.DepthBias=ink ? 0 : 1000;
            description.SlopeScaledDepthBias=ink ? 0.0f : 1.0f;
            return description;
        }
    };
}
