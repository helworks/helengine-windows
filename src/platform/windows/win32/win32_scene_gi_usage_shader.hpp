#pragma once
namespace helengine::windows {
    /// <summary>Matches normal camera geometry and alpha coverage, with depth-equal reader tracking through the production sampler.</summary>
    inline constexpr const char* SceneGiUsageShader=R"(
cbuffer TransformBuffer:register(b0) {float4x4 World;float4x4 WorldViewProjection;float4x4 WorldNormal;};
cbuffer BaseColorBuffer:register(b3) {float4 BaseColor;};
Texture2D DiffuseTexture:register(t0);SamplerState DiffuseSampler:register(s0);
struct ReaderInput {float3 Position:POSITION;float3 Normal:NORMAL;float2 UV:TEXCOORD0;};
struct ReaderOutput {float4 Position:SV_POSITION;float3 WorldPosition:TEXCOORD0;float3 Normal:TEXCOORD1;float2 UV:TEXCOORD2;};
ReaderOutput ReaderVS(ReaderInput input) {
    ReaderOutput output;output.Position=mul(float4(input.Position,1),WorldViewProjection);
    output.WorldPosition=mul(float4(input.Position,1),World).xyz;output.Normal=mul(float4(input.Normal,0),WorldNormal).xyz;output.UV=input.UV;return output;
}
void ReaderDepthPS(ReaderOutput input) {clip(DiffuseTexture.Sample(DiffuseSampler,input.UV).a*BaseColor.a-0.001);}
[earlydepthstencil]
void ReaderUsagePS(ReaderOutput input) {
    clip(DiffuseTexture.Sample(DiffuseSampler,input.UV).a*BaseColor.a-0.001);
    SampleSceneGi(input.WorldPosition,normalize(input.Normal));
}
)";
}
