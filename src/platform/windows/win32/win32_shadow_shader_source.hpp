#pragma once
namespace helengine::windows {
    /// <summary>Transforms authored caster vertices through the actual world/light projection.</summary>
    inline constexpr const char* ShadowVertexShaderSource = R"(
cbuffer ShadowTransformBuffer : register(b0) {
    float4x4 WorldViewProjection;
};

struct VSInput {
    float3 Position : POSITION;
};

struct VSOutput {
    float4 Position : SV_POSITION;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.Position = mul(float4(input.Position, 1.0f), WorldViewProjection);
    return output;
}
)";
    /// <summary>Writes depth without introducing material color into the directional shadow pass.</summary>
    inline constexpr const char* ShadowPixelShaderSource = R"(
struct VSOutput {
    float4 Position : SV_POSITION;
};

void PSMain(VSOutput input) {
}
)";
}
