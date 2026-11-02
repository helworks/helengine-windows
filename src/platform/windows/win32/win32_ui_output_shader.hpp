#pragma once
#include "platform/windows/directx11/directx11_output_color_shader.hpp"

namespace helengine::windows {
    /// Implements actual native sprite/font output conversion with alpha and coverage preserved.
    inline const std::string NativeUiQuadShader = DirectX11OutputUiShader+R"(
Texture2D DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);

struct PSInput {
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float4 color=DiffuseTexture.Sample(DiffuseSampler, input.UV) * input.Color;
    return float4(OutputUiColor(color.rgb),color.a);
}

float4 FontGrayscalePS(PSInput input) : SV_TARGET {
    return float4(OutputUiColor(input.Color.rgb), DiffuseTexture.Sample(DiffuseSampler, input.UV).a * input.Color.a);
}

struct FontBlendOutput {
    float4 Foreground : SV_Target0;
    float4 Coverage : SV_Target1;
};

FontBlendOutput FontClearTypePS(PSInput input) {
    float4 mask = DiffuseTexture.Sample(DiffuseSampler, input.UV) * input.Color.a;
    FontBlendOutput output;
    output.Foreground = float4(OutputUiColor(input.Color.rgb) * mask.rgb, mask.a);
    output.Coverage = mask;
    return output;
}
)";
    /// Implements actual native rounded-rectangle output conversion with alpha and coverage preserved.
    inline const std::string NativeUiRoundedRectShader = DirectX11OutputUiShader+R"(
cbuffer RoundedRectBuffer : register(b0) {
    float4 DestRect;
    float4 Params1;
    float4 FillColor;
    float4 BorderColor;
};

float sdRoundRectMasked(float2 p, float2 halfSize, float radius, uint cornerMask) {
    radius = min(radius, min(halfSize.x, halfSize.y));
    float2 ap = abs(p);
    float2 d = ap - halfSize;
    float baseDist = length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);

    if (radius <= 0.0f) {
        return baseDist;
    }

    float2 inner = halfSize - radius;
    if (ap.x <= inner.x || ap.y <= inner.y) {
        return baseDist;
    }

    uint cornerBit;
    if (p.x < 0.0f && p.y >= 0.0f) {
        cornerBit = 1u;
    } else if (p.x >= 0.0f && p.y >= 0.0f) {
        cornerBit = 2u;
    } else if (p.x < 0.0f && p.y < 0.0f) {
        cornerBit = 4u;
    } else {
        cornerBit = 8u;
    }

    if ((cornerMask & cornerBit) == 0u) {
        return baseDist;
    }

    return length(ap - inner) - radius;
}

float4 PSMain(float4 position : SV_POSITION, float2 localPosition : TEXCOORD0) : SV_TARGET {
    float radius = Params1.x;
    float border = max(Params1.y, 0.0f);
    float aa = max(Params1.z, 0.5f);
    uint cornerMask = (uint)(Params1.w + 0.5f);
    float2 halfSize = 0.5f * DestRect.zw;

    float dOuter = sdRoundRectMasked(localPosition, halfSize, radius, cornerMask);
    float alphaFill = 1.0f - smoothstep(-aa, aa, dOuter);

    float innerRadius = max(radius - border, 0.0f);
    float2 innerHalf = float2(max(halfSize.x - border, 0.0f), max(halfSize.y - border, 0.0f));
    float dInner = sdRoundRectMasked(localPosition, innerHalf, innerRadius, cornerMask);
    float alphaInner = 1.0f - smoothstep(-aa, aa, dInner);
    float alphaBorder = saturate(alphaFill - alphaInner);

    if(NativeOutput.x>0.5) {
        float fillAlpha=FillColor.a*alphaFill;
        float borderAlpha=BorderColor.a*alphaBorder;
        float alpha=borderAlpha+fillAlpha*(1-borderAlpha);
        float3 premultiplied=OutputUiColor(BorderColor.rgb)*borderAlpha+OutputUiColor(FillColor.rgb)*fillAlpha*(1-borderAlpha);
        return float4(alpha>0 ? premultiplied/alpha : 0,alpha);
    }

    float4 colFill = FillColor * alphaFill;
    float4 colBorder = BorderColor * alphaBorder;
    float alpha = colFill.a + colBorder.a * (1.0f - colFill.a);
    float3 rgb = float3(0.0f, 0.0f, 0.0f);
    if (alpha > 0.0001f) {
        rgb = (colBorder.rgb * colBorder.a + colFill.rgb * colFill.a * (1.0f - colBorder.a)) / alpha;
    }

    return float4(OutputUiColor(rgb), alpha);
}
)";
}
