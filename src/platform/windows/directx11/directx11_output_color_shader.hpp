#pragma once
#include <string>

namespace helengine::windows {
    /// Defines linear scRGB presentation independently from scene exposure and Windows UI reference white.
    inline constexpr const char* DirectX11OutputColorShader = R"(
float3 OutputSrgbToLinear(float3 color) {
    color=max(color,0);
    return float3(color.r<=0.04045 ? color.r/12.92 : pow((color.r+0.055)/1.055,2.4),
        color.g<=0.04045 ? color.g/12.92 : pow((color.g+0.055)/1.055,2.4),
        color.b<=0.04045 ? color.b/12.92 : pow((color.b+0.055)/1.055,2.4));
}
// One scene unit is 80 nits. A continuous hue-preserving shoulder bounds highlights below 400 nits.
float3 OutputSceneScRgb(float3 radiance) {
    float3 color=max(radiance,0);
    float peak=max(color.r,max(color.g,color.b));
    float mapped=peak<=1 ? peak : 1+4*(peak-1)/(peak+3);
    return color*(mapped/max(peak,0.000001));
}
// Generated ACES 2.0 output already contains sRGB encoding. Sample straight scene-linear RGB once.
Texture3D<float4> OutputAcesSdrLut : register(t29);
SamplerState OutputAcesSampler : register(s11);
float3 OutputSceneSdr(float3 radiance) {
    float3 color=max(radiance,0);
    // Beyond the lattice domain, preserve hue while limiting the peak to the last supported exposure.
    float peak=max(color.r,max(color.g,color.b));
    color*=min(1.0,1023.984375/max(peak,0.000001));
    float3 shaped=saturate(log2(1+color*64)/16);
    return OutputAcesSdrLut.SampleLevel(OutputAcesSampler,(shaped*128+0.5)/129,0).rgb;
}
float3 OutputUiScRgb(float3 color,float whiteScale) {return OutputSrgbToLinear(color)*whiteScale;}
)";
    /// Supplies pass-local native UI/forward encoding at b13; toon ink binds its own parameters for geometry.
    inline const std::string DirectX11OutputUiShader = std::string(DirectX11OutputColorShader)+R"(
cbuffer NativeOutputBuffer : register(b13) {float4 NativeOutput;};
float3 OutputUiColor(float3 color) {return NativeOutput.x>0.5 ? OutputUiScRgb(color,NativeOutput.y) : color;}
)";
}
