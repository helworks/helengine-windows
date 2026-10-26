#pragma once

namespace helengine::windows {
    /// Supplies the separately compiled pearl surface shader with bounded angular color and broad key-light highlights.
    inline constexpr const char* PearlSurfaceShader = R"(
float3 PearlSafeDirection(float3 value) {
    return value * rsqrt(max(dot(value,value),0.00000001));
}
float3 PearlCoatedColor(float3 baseColor, float3 normal, float3 viewDirection, float strength) {
    float grazing=1-saturate(abs(dot(normal,viewDirection)));
    float3 pink=float3(1.0,0.48,0.78), cyan=float3(0.35,0.87,1.0);
    float3 coating=lerp(pink,cyan,smoothstep(0.25,0.9,grazing));
    return lerp(baseColor,baseColor*0.45+coating*0.55,strength*(0.18+0.82*grazing));
}
float PearlLightLobe(float3 normal, float3 viewDirection, float3 lightDirection, float roughness, float width) {
    float3 halfway=PearlSafeDirection(viewDirection+lightDirection);
    float exponent=lerp(96.0,4.0,saturate(width*0.8+roughness*0.6));
    float normalization=lerp(1.0,0.55,roughness);
    return pow(saturate(dot(normal,halfway)),exponent)*saturate(dot(normal,lightDirection))*normalization;
}
)";
}
