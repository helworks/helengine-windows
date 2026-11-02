#pragma once
namespace helengine::windows {
    /// <summary>Defines the inexpensive three-band palette and bounded broad conductor response shared by capture and presentation.</summary>
    inline constexpr const char* SceneInkShader=R"(
cbuffer InkBuffer : register(b13) {float4 InkState;float4 InkAmbient;float4 InkShadowColor;float4 InkReserved;};
float InkBand(float cosine) {return floor(saturate(cosine)*2+0.5)*0.5;}
// Shadow fill follows bounded light chroma while the illuminated bands retain full scene radiance.
float3 InkLightColor(float3 color,float cosine,float visibility) {
    float3 light=max(color,0);float peak=max(light.r,max(light.g,light.b));
    float3 shadow=InkShadowColor.rgb*(light/max(peak,0.000001))*saturate(peak);
    return lerp(shadow,light,InkBand(cosine)*saturate(visibility));
}
float3 InkMetallicHighlight(float3 normal,float3 viewDirection,float3 tint,float roughness,float4 color,float4 direction,float visibility) {
    if(color.w>0.5 || visibility<=0 || dot(direction.xyz,direction.xyz)<0.00000001) return 0;
    float3 lightDirection=normalize(-direction.xyz);float nl=saturate(dot(normal,lightDirection));
    float3 halfVector=lightDirection+viewDirection;if(nl<=0 || dot(halfVector,halfVector)<0.00000001) return 0;
    float lobe=pow(saturate(dot(normal,normalize(halfVector))),lerp(20.0,5.0,saturate(roughness)));
    return color.rgb*lerp(saturate(tint),float3(1,1,1),0.25)*smoothstep(0.45,0.85,lobe)*nl*saturate(visibility)*0.7;
}
)";
}
