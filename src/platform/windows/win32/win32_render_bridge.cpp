#include "platform/windows/win32/win32_render_bridge.hpp"

#include <d3dcompiler.h>

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "MaterialLayoutBuilder.hpp"
#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/runtime/runtime_render_diagnostics.hpp"

#if __has_include("CameraRenderSettings.hpp")
#include "BuiltInMaterialIds.hpp"
#if __has_include("BoxCollider3DComponent.hpp")
#include "BoxCollider3DComponent.hpp"
#endif
#include "CameraRenderSettings.hpp"
#include "Entity.hpp"
#include "DirectionalLightComponent.hpp"
#include "FontInfo.hpp"
#if __has_include("FontAssetBinarySerializer.hpp")
#include "FontAssetBinarySerializer.hpp"
#endif
#include "IRenderQueue2D.hpp"
#include "IRenderQueue3D.hpp"
#include "LightComponent.hpp"
#include "LightDirectionUtility.hpp"
#include "MaterialLayoutBinding.hpp"
#if __has_include("MaterialConstantBufferAsset.hpp")
#include "MaterialConstantBufferAsset.hpp"
#endif
#include "PointLightComponent.hpp"
#if __has_include("RuntimeDiagnosticsService.hpp")
#include "RuntimeDiagnosticsService.hpp"
#endif
#include "ObjectManager.hpp"
#include "RuntimeSceneLoadService.hpp"
#if __has_include("RigidBody3DComponent.hpp")
#include "RigidBody3DComponent.hpp"
#endif
#if __has_include("RuntimeSceneAssetReferenceResolver.hpp")
#include "RuntimeSceneAssetReferenceResolver.hpp"
#endif
#if __has_include("ShaderBinaryAsset.hpp")
#include "ShaderBinaryAsset.hpp"
#endif
#include "ShaderProgramAsset.hpp"
#include "runtime/native_exceptions.hpp"
#if __has_include("ShaderRuntimeMaterialLoader.hpp")
#include "ShaderRuntimeMaterialLoader.hpp"
#endif
#if __has_include("ShaderRuntimeMaterialAccess.hpp")
#include "ShaderRuntimeMaterialAccess.hpp"
#endif
#if __has_include("ShaderVertexElementAsset.hpp")
#include "ShaderVertexElementAsset.hpp"
#endif
#include "SpotLightComponent.hpp"
#if __has_include("StandardMaterialTextureBindingDefaults.hpp")
#include "StandardMaterialTextureBindingDefaults.hpp"
#endif
#if __has_include("TextureUtils.hpp")
#include "TextureUtils.hpp"
#endif
#endif

#if __has_include("RenderCommandListBuilder2D.hpp")
#include "RenderCommand2DType.hpp"
#include "RenderCommandList2D.hpp"
#include "RenderCommandListBuilder2D.hpp"
#endif

#if __has_include("BoxCollider3DComponent.hpp") && __has_include("RigidBody3DComponent.hpp")
#define HE_WINDOWS_HAS_RUNTIME_PHYSICS_DEBUG_TYPES 1
#else
#define HE_WINDOWS_HAS_RUNTIME_PHYSICS_DEBUG_TYPES 0
#endif

namespace helengine::windows {
#if __has_include("RenderManager2D.hpp")
    namespace {
        /// Tracks whether one diagnostic render snapshot has already been written.
        bool HasWrittenRenderSnapshot = false;

        /// Counts logged 3D draw visits for the first snapshot frame.
        int Logged3DVisitCount = 0;

        /// Maximum number of 3D draw visits captured in the first-frame snapshot.
        constexpr int MaxLogged3DVisitCount = 8;

        /// Tracks whether one native 2D summary has already been written.
        bool HasWritten2DSummary = false;

        /// Tracks whether one native 2D draw call has already been written.
        bool HasWritten2DDraw = false;

        /// Tracks whether the directional-shadow plaza tower face-view probe has already been written.
        bool HasWrittenPlazaTowerViewDebug = false;

        /// Tracks whether the ground-cube probe ground transform has already been written.
        bool HasWrittenGroundCubeProbeGroundDebug = false;

        /// Tracks whether the ground-cube probe cube transform has already been written.
        bool HasWrittenGroundCubeProbeCubeDebug = false;

        /// Counts probe-ground submissions written to diagnostics so the trace stays bounded.
        int GroundCubeProbeGroundDebugCount = 0;

        /// Counts probe-cube submissions written to diagnostics so the trace stays bounded.
        int GroundCubeProbeCubeDebugCount = 0;

        /// Stores one tracked dynamic unit-cube probe entry.
        struct ProbeCubeDebugSlot {
            Entity* EntityValue = nullptr;
            int FrameCount = 0;
        };

        /// Maximum number of dynamic unit cubes tracked by the probe diagnostics.
        constexpr int MaxProbeCubeDebugSlotCount = 4;

        /// Maximum number of frames logged for each tracked dynamic unit cube.
        constexpr int MaxProbeCubeDebugFrameCount = 120;

        /// Tracks the specific runtime entity instances chosen as the probed dynamic unit cubes.
        std::array<ProbeCubeDebugSlot, MaxProbeCubeDebugSlotCount> ProbeCubeDebugSlots {};

        /// Counts 2D visitor dispatches for the first logged frame.
        int Logged2DVisitCount = 0;

        /// Counts 2D rounded-rect draws for the first logged frame.
        int Logged2DRectCount = 0;

        /// Counts 2D text draws for the first logged frame.
        int Logged2DTextCount = 0;

        /// Counts 2D sprite draws for the first logged frame.
        int Logged2DSpriteCount = 0;

        /// Counts text draws that returned before issuing any glyph quads.
        int Logged2DTextEarlyReturnCount = 0;

        /// Tracks generated identifiers for embedded runtime textures that do not carry an authored asset id.
        uint64_t GeneratedTextureResourceId = 0;

        /// Caches uploaded textures so both the texture loader and material binder resolve the same GPU resources.
        std::unordered_map<std::string, std::unique_ptr<Win32TextureResource>> TextureResources;

        /// Tracks runtime texture ids owned by engine-wide helper caches rather than by a scene.
        std::unordered_set<std::string> EngineOwnedTextureResourceIds;

        /// Packs one mesh vertex into the fixed Windows DirectX11 bridge layout.
        struct Win32VertexPositionNormalUV {
            DirectX::XMFLOAT3 Position;
            DirectX::XMFLOAT3 Normal;
            DirectX::XMFLOAT2 UV;
        };

        /// Stores the transform constants consumed by the fixed Windows DirectX11 bridge shader.
        struct Win32TransformConstants {
            DirectX::XMFLOAT4X4 World;
            DirectX::XMFLOAT4X4 WorldViewProjection;
            DirectX::XMFLOAT4X4 WorldNormal;
            DirectX::XMFLOAT4 CameraPosition;
            DirectX::XMFLOAT4 MaterialFlags;
            DirectX::XMFLOAT4 LightDirection;
            DirectX::XMFLOAT4 LightColor;
            DirectX::XMFLOAT4 AmbientColor;
            DirectX::XMFLOAT4 SpecularColor;
            DirectX::XMFLOAT4 MaterialParameters;
        };

        /// Stores one packed forward-light slot matching the built-in Windows forward shader contract.
        struct Win32ForwardLightSlotConstants {
            DirectX::XMFLOAT4 ColorAndType;
            DirectX::XMFLOAT4 DirectionAndShadow;
            DirectX::XMFLOAT4 PositionAndRange;
            DirectX::XMFLOAT4 SpotAngles;
        };

        /// Stores the packed forward-light constant buffer consumed by built-in scene shaders.
        struct Win32ForwardLightConstants {
            DirectX::XMFLOAT4 AmbientLightColor;
            DirectX::XMFLOAT4 LightMetadata;
            Win32ForwardLightSlotConstants Light0;
            Win32ForwardLightSlotConstants Light1;
            Win32ForwardLightSlotConstants Light2;
            Win32ForwardLightSlotConstants Light3;
        };

        static_assert(offsetof(Win32ForwardLightConstants, AmbientLightColor) == 0, "Ambient-light color must remain the first forward-light field.");
        static_assert(offsetof(Win32ForwardLightConstants, LightMetadata) == sizeof(DirectX::XMFLOAT4), "Forward-light metadata must remain immediately after the ambient-light color.");

        /// Stores one packed shadow slot matching the built-in Windows forward shader contract.
        struct Win32ShadowLightSlotConstants {
            DirectX::XMFLOAT4 AtlasRect;
            DirectX::XMFLOAT4 Metadata;
            DirectX::XMFLOAT4X4 WorldToShadowClip;
        };

        /// Stores the packed shadow constant buffer consumed by built-in scene shaders.
        struct Win32ShadowConstants {
            DirectX::XMFLOAT4 ShadowMetadata;
            Win32ShadowLightSlotConstants Light0;
            Win32ShadowLightSlotConstants Light1;
            Win32ShadowLightSlotConstants Light2;
            Win32ShadowLightSlotConstants Light3;
        };

        /// Stores the transform constants consumed by the directional shadow depth pass.
        struct Win32ShadowTransformConstants {
            DirectX::XMFLOAT4X4 WorldViewProjection;
        };

        /// Stores the fixed resolution used by the first native directional shadow map.
        constexpr UINT DirectionalShadowMapResolution = 1024;

        /// Minimum directional shadow distance used by the native player so zero or invalid authored shadow distances still produce a valid shadow volume.
        constexpr float MinimumDirectionalShadowDistance = 1.0f;

        /// Stores the fraction of the authored shadow distance used to focus directional shadow coverage ahead of the camera.
        constexpr float DirectionalShadowFocusDistanceFactor = 0.5f;

        /// Constant depth bias applied while rendering directional shadow casters so simple rotating meshes do not collapse into self-shadowing.
        constexpr int ShadowDepthBias = 1000;

        /// Slope-scaled depth bias applied while rendering directional shadow casters so grazing-angle receivers remain lit consistently.
        constexpr float ShadowSlopeScaledDepthBias = 1.0f;

        /// Built-in generated model id used by the infinite-thin ground plane primitive.
        constexpr const char* BuiltInPlaneModelId = "engine:model:plane";

        /// Number of vertices reserved for the reusable 2D dynamic quad buffer.
        constexpr UINT QuadVertexBufferVertexCapacity = 4096U;

        /// Engine-managed shadow-atlas texture binding name used by the built-in forward shader.
        constexpr const char* ShadowAtlasTextureBindingName = "shadowAtlasTexture";

        /// Engine-managed point-shadow texture binding names used by the built-in forward shader.
        constexpr const char* PointShadowTextureBindingName0 = "pointShadowTexture0";
        constexpr const char* PointShadowTextureBindingName1 = "pointShadowTexture1";
        constexpr const char* PointShadowTextureBindingName2 = "pointShadowTexture2";
        constexpr const char* PointShadowTextureBindingName3 = "pointShadowTexture3";

        bool LoggedGeneratedCubeMeshData = false;

        /// Vertex shader used by the first native 3D Windows pass.
        constexpr const char* VertexShaderSource = R"(
cbuffer TransformBuffer : register(b0) {
    float4x4 World;
    float4x4 WorldViewProjection;
    float4x4 WorldNormal;
    float4 CameraPosition;
    float4 MaterialFlags;
    float4 LightDirection;
    float4 LightColor;
    float4 AmbientColor;
    float4 SpecularColor;
    float4 MaterialParameters;
};

struct VSInput {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct PSInput {
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    float4 worldNormal = mul(float4(input.Normal, 0.0f), WorldNormal);
    output.Position = mul(float4(input.Position, 1.0f), WorldViewProjection);
    output.WorldPosition = worldPosition.xyz;
    output.WorldNormal = normalize(worldNormal.xyz);
    return output;
}
)";

        /// Pixel shader used by the first native 3D Windows pass.
        constexpr const char* PixelShaderSource = R"(
cbuffer TransformBuffer : register(b0) {
    float4x4 World;
    float4x4 WorldViewProjection;
    float4x4 WorldNormal;
    float4 CameraPosition;
    float4 MaterialFlags;
    float4 Padding0;
    float4 Padding1;
    float4 Padding2;
    float4 Padding3;
};

cbuffer ForwardLightBuffer : register(b1) {
    float4 LightMetadata;
    float4 Light0ColorAndType;
    float4 Light0DirectionAndShadow;
    float4 Light0PositionAndRange;
    float4 Light0SpotAngles;
    float4 Light1ColorAndType;
    float4 Light1DirectionAndShadow;
    float4 Light1PositionAndRange;
    float4 Light1SpotAngles;
    float4 Light2ColorAndType;
    float4 Light2DirectionAndShadow;
    float4 Light2PositionAndRange;
    float4 Light2SpotAngles;
    float4 Light3ColorAndType;
    float4 Light3DirectionAndShadow;
    float4 Light3PositionAndRange;
    float4 Light3SpotAngles;
};

cbuffer ShadowBuffer : register(b2) {
    float4 ShadowMetadata;
    float4 ShadowLight0AtlasRect;
    float4 ShadowLight0Metadata;
    float4x4 ShadowLight0WorldToShadowClip;
    float4 ShadowLight1AtlasRect;
    float4 ShadowLight1Metadata;
    float4x4 ShadowLight1WorldToShadowClip;
    float4 ShadowLight2AtlasRect;
    float4 ShadowLight2Metadata;
    float4x4 ShadowLight2WorldToShadowClip;
    float4 ShadowLight3AtlasRect;
    float4 ShadowLight3Metadata;
    float4x4 ShadowLight3WorldToShadowClip;
};

cbuffer BaseColorBuffer : register(b3) {
    float4 baseColor;
};

Texture2D ShadowAtlasTexture : register(t1);
SamplerState ShadowAtlasSampler : register(s1);

struct PSInput {
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
};

float EvaluateShadow(float4 atlasRect, float4 shadowMetadata, float4x4 worldToShadowClip, float3 worldPosition) {
    if (shadowMetadata.x <= 0.5f || shadowMetadata.z >= 1.5f || ShadowMetadata.x <= 0.5f) {
        return 1.0f;
    }

    float4 shadowClip = mul(float4(worldPosition, 1.0f), worldToShadowClip);
    if (abs(shadowClip.w) <= 0.0001f) {
        return 1.0f;
    }

    float3 shadowNdc = shadowClip.xyz / shadowClip.w;
    float2 shadowUv = float2((shadowNdc.x * 0.5f) + 0.5f, (-shadowNdc.y * 0.5f) + 0.5f);
    if (shadowUv.x < 0.0f || shadowUv.x > 1.0f || shadowUv.y < 0.0f || shadowUv.y > 1.0f || shadowNdc.z < 0.0f || shadowNdc.z > 1.0f) {
        return 1.0f;
    }

    float2 atlasUv = atlasRect.xy + (shadowUv * atlasRect.zw);
    float sampledDepth = ShadowAtlasTexture.Sample(ShadowAtlasSampler, atlasUv).r;
    float shadowBias = 0.01f;
    float visibility = (shadowNdc.z - shadowBias) <= sampledDepth ? 1.0f : 0.0f;
    return lerp(1.0f, visibility, shadowMetadata.y);
}

float3 EvaluateForwardLight(
    float4 colorAndType,
    float4 directionAndShadow,
    float4 positionAndRange,
    float4 spotAngles,
    float4 shadowAtlasRect,
    float4 shadowSlotMetadata,
    float4x4 worldToShadowClip,
    float3 surfaceColor,
    float3 worldPosition,
    float3 worldNormal,
    float3 viewDirection) {
    int lightType = (int)(colorAndType.w + 0.5f);
    float3 radiance = colorAndType.xyz;
    float3 lightDirection = float3(0.0f, 0.0f, 0.0f);
    float attenuation = 1.0f;

    if (lightType == 0) {
        lightDirection = normalize(-directionAndShadow.xyz);
    } else {
        float3 toLight = positionAndRange.xyz - worldPosition;
        float distanceToLight = length(toLight);
        if (distanceToLight <= 0.0001f || positionAndRange.w <= 0.0f) {
            return float3(0.0f, 0.0f, 0.0f);
        }

        lightDirection = toLight / distanceToLight;
        float normalizedDistance = saturate(distanceToLight / positionAndRange.w);
        float rangeAttenuation = 1.0f - (normalizedDistance * normalizedDistance);
        attenuation = rangeAttenuation * rangeAttenuation;

        if (lightType == 2) {
            float3 lightForward = normalize(directionAndShadow.xyz);
            float3 lightToSurface = normalize(worldPosition - positionAndRange.xyz);
            float cone = dot(lightForward, lightToSurface);
            float coneRange = max(spotAngles.x - spotAngles.y, 0.0001f);
            float spotAttenuation = saturate((cone - spotAngles.y) / coneRange);
            attenuation *= spotAttenuation * spotAttenuation;
        }
    }

    if (attenuation <= 0.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    attenuation *= EvaluateShadow(shadowAtlasRect, shadowSlotMetadata, worldToShadowClip, worldPosition);
    float diffuse = saturate(dot(worldNormal, lightDirection));
    if (diffuse <= 0.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 halfVector = normalize(lightDirection + viewDirection);
    float specular = pow(saturate(dot(worldNormal, halfVector)), 32.0f);
    float3 diffuseColor = surfaceColor * radiance * diffuse * attenuation;
    float3 specularColor = radiance * specular * 0.35f * attenuation;
    return diffuseColor + specularColor;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float4 sampledBaseColor = baseColor;
    float3 surfaceColor = sampledBaseColor.rgb;
    float3 ambientColor = float3(0.12f, 0.13f, 0.15f);
    float3 normal = normalize(input.WorldNormal);
    float3 viewDirection = normalize(CameraPosition.xyz - input.WorldPosition);
    float3 color = surfaceColor * ambientColor;
    int activeLightCount = (int)(LightMetadata.x + 0.5f);

    if (activeLightCount > 0) {
        color += EvaluateForwardLight(Light0ColorAndType, Light0DirectionAndShadow, Light0PositionAndRange, Light0SpotAngles, ShadowLight0AtlasRect, ShadowLight0Metadata, ShadowLight0WorldToShadowClip, surfaceColor, input.WorldPosition, normal, viewDirection);
    }

    if (activeLightCount > 1) {
        color += EvaluateForwardLight(Light1ColorAndType, Light1DirectionAndShadow, Light1PositionAndRange, Light1SpotAngles, ShadowLight1AtlasRect, ShadowLight1Metadata, ShadowLight1WorldToShadowClip, surfaceColor, input.WorldPosition, normal, viewDirection);
    }

    if (activeLightCount > 2) {
        color += EvaluateForwardLight(Light2ColorAndType, Light2DirectionAndShadow, Light2PositionAndRange, Light2SpotAngles, ShadowLight2AtlasRect, ShadowLight2Metadata, ShadowLight2WorldToShadowClip, surfaceColor, input.WorldPosition, normal, viewDirection);
    }

    if (activeLightCount > 3) {
        color += EvaluateForwardLight(Light3ColorAndType, Light3DirectionAndShadow, Light3PositionAndRange, Light3SpotAngles, ShadowLight3AtlasRect, ShadowLight3Metadata, ShadowLight3WorldToShadowClip, surfaceColor, input.WorldPosition, normal, viewDirection);
    }

    return float4(saturate(color), sampledBaseColor.a);
}
)";

        /// Vertex shader used by the directional shadow depth pass.
        constexpr const char* ShadowVertexShaderSource = R"(
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

        /// Pixel shader used by the directional shadow depth pass.
        constexpr const char* ShadowPixelShaderSource = R"(
struct VSOutput {
    float4 Position : SV_POSITION;
};

void PSMain(VSOutput input) {
}
)";

        /// Vertex shader used by the fullscreen diagnostic pass.
        constexpr const char* DiagnosticVertexShaderSource = R"(
struct VSInput {
    float3 Position : POSITION;
};

struct VSOutput {
    float4 Position : SV_POSITION;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.Position = float4(input.Position, 1.0f);
    return output;
}
)";

        /// Pixel shader used by the fullscreen diagnostic pass.
        constexpr const char* DiagnosticPixelShaderSource = R"(
float4 PSMain() : SV_TARGET {
    return float4(0.05f, 0.7f, 0.95f, 1.0f);
}
)";

        /// Packs one native 2D quad vertex in clip space.
        struct Win32QuadVertex {
            DirectX::XMFLOAT3 Position;
            DirectX::XMFLOAT2 UV;
            DirectX::XMFLOAT4 Color;
        };

        /// Stores the constants consumed by the native rounded-rect SDF shader path.
        struct alignas(16) Win32RoundedRectShaderConstants {
            DirectX::XMFLOAT4 DestRect;
            DirectX::XMFLOAT4 Params1;
            DirectX::XMFLOAT4 FillColor;
            DirectX::XMFLOAT4 BorderColor;
        };

        static_assert((sizeof(Win32RoundedRectShaderConstants) % 16) == 0, "Rounded-rect constant buffer size must stay 16-byte aligned.");

        /// Vertex shader used by the native 2D quad pass.
        constexpr const char* QuadVertexShaderSource = R"(
struct VSInput {
    float3 Position : POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

struct PSInput {
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.Position = float4(input.Position, 1.0f);
    output.UV = input.UV;
    output.Color = input.Color;
    return output;
}
)";

        /// Pixel shader used by the native 2D quad pass.
        constexpr const char* QuadPixelShaderSource = R"(
Texture2D DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);

struct PSInput {
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    return DiffuseTexture.Sample(DiffuseSampler, input.UV) * input.Color;
}
)";

        /// Vertex shader used by the native rounded-rect SDF pass.
        constexpr const char* RoundedRectVertexShaderSource = R"(
cbuffer RoundedRectBuffer : register(b0) {
    float4 DestRect;
    float4 Params1;
    float4 FillColor;
    float4 BorderColor;
};

struct VSInput {
    float3 Position : POSITION;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR0;
};

struct PSInput {
    float4 Position : SV_POSITION;
    float2 LocalPosition : TEXCOORD0;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    output.Position = float4(input.Position, 1.0f);
    output.LocalPosition = (input.UV - 0.5f) * DestRect.zw;
    return output;
}
)";

        /// Pixel shader used by the native rounded-rect SDF pass.
        constexpr const char* RoundedRectPixelShaderSource = R"(
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

    float4 colFill = FillColor * alphaFill;
    float4 colBorder = BorderColor * alphaBorder;
    float alpha = colFill.a + colBorder.a * (1.0f - colFill.a);
    float3 rgb = float3(0.0f, 0.0f, 0.0f);
    if (alpha > 0.0001f) {
        rgb = (colBorder.rgb * colBorder.a + colFill.rgb * colFill.a * (1.0f - colBorder.a)) / alpha;
    }

    return float4(rgb, alpha);
}
)";

        /// Throws when one native DirectX operation fails.
        void ThrowIfFailed(HRESULT result, const char* message) {
            if (FAILED(result)) {
                throw std::runtime_error(message);
            }
        }

        /// Copies one HelEngine matrix into the native shader constant-buffer layout.
        DirectX::XMFLOAT4X4 StoreMatrix(const ::float4x4& value) {
            return DirectX::XMFLOAT4X4(
                value.M11, value.M12, value.M13, value.M14,
                value.M21, value.M22, value.M23, value.M24,
                value.M31, value.M32, value.M33, value.M34,
                value.M41, value.M42, value.M43, value.M44);
        }

        /// Converts one packed HelEngine color into normalized DirectX shader color channels.
        DirectX::XMFLOAT4 ConvertColor(byte4 color) {
            return DirectX::XMFLOAT4(
                static_cast<float>(color.X) / 255.0f,
                static_cast<float>(color.Y) / 255.0f,
                static_cast<float>(color.Z) / 255.0f,
                static_cast<float>(color.W) / 255.0f);
        }

        /// Resolves one diagnostics log path beside the packaged executable.
        std::filesystem::path ResolveRenderDiagnosticsLogPath() {
            wchar_t buffer[MAX_PATH];
            DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (length == 0) {
                return std::filesystem::path("helengine_windows.render.log");
            }

            return std::filesystem::path(buffer).parent_path() / "helengine_windows.render.log";
        }

        /// Resolves one first-frame render snapshot path beside the packaged executable.
        std::filesystem::path ResolveRenderSnapshotLogPath() {
            wchar_t buffer[MAX_PATH];
            DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (length == 0) {
                return std::filesystem::path("win32-render-snapshot.log");
            }

            return std::filesystem::path(buffer).parent_path() / "win32-render-snapshot.log";
        }

        /// Appends one one-line message to the first-frame Windows render snapshot log.
        void AppendRenderSnapshotLine(const std::string& line) {
            std::ofstream stream(ResolveRenderSnapshotLogPath(), std::ios::app);
            if (!stream.is_open()) {
                return;
            }

            stream << line << '\n';
        }

        /// Appends one line to the packaged Windows render diagnostics log.
        void AppendRenderDiagnosticsLine(const std::string& line) {
            std::ofstream stream(ResolveRenderDiagnosticsLogPath(), std::ios::app);
            if (!stream.is_open()) {
                return;
            }

            stream << line << '\n';
        }

        /// Returns whether one scalar is within the supplied tolerance of the expected value.
        bool IsApproximately(float value, float expectedValue, float tolerance) {
            return std::abs(value - expectedValue) <= tolerance;
        }

        /// Returns whether one entity transform matches the authored central directional-shadow plaza tower.
        bool IsDirectionalShadowCentralTower(Entity* entity) { 
            if (entity == nullptr) { 
                return false; 
            } 

            const float3 position = entity->get_Position();
            const float3 scale = entity->get_Scale();
            return IsApproximately(position.X, 0.0f, 0.5f)
                && IsApproximately(position.Y, 9.0f, 0.5f)
                && IsApproximately(position.Z, -12.0f, 0.5f)
                && IsApproximately(scale.X, 7.0f, 0.5f) 
                && IsApproximately(scale.Y, 18.0f, 0.5f) 
                && IsApproximately(scale.Z, 7.0f, 0.5f); 
        } 

        /// Returns whether one entity matches the authored ground cube used by the ground-cube probe diagnostic scene.
        bool IsGroundCubeProbeGround(Entity* entity) {
            if (entity == nullptr) {
                return false;
            }

            const float3 position = entity->get_Position();
            const float3 scale = entity->get_Scale();
            return IsApproximately(position.X, 0.0f, 0.1f)
                && IsApproximately(position.Y, -0.5f, 0.1f)
                && IsApproximately(position.Z, 0.0f, 0.1f)
                && IsApproximately(scale.X, 15.0f, 0.1f)
                && IsApproximately(scale.Y, 1.0f, 0.1f)
                && IsApproximately(scale.Z, 15.0f, 0.1f);
        }

        /// Returns whether one entity matches the authored elevated cube used by the ground-cube probe diagnostic scene.
        bool IsGroundCubeProbeCube(Entity* entity) {
            if (entity == nullptr) {
                return false;
            }

            const float3 position = entity->get_Position();
            const float3 scale = entity->get_Scale();
            return position.Y > 0.0f
                && IsApproximately(position.Z, 0.0f, 0.1f)
                && IsApproximately(scale.X, 1.0f, 0.1f)
                && IsApproximately(scale.Y, 1.0f, 0.1f)
                && IsApproximately(scale.Z, 1.0f, 0.1f);
        }

        /// Finds one tracked probe-cube slot for the supplied entity.
        int FindProbeCubeDebugSlot(Entity* entity) {
            if (entity == nullptr) {
                return -1;
            }

            for (int index = 0; index < MaxProbeCubeDebugSlotCount; index++) {
                if (ProbeCubeDebugSlots[static_cast<std::size_t>(index)].EntityValue == entity) {
                    return index;
                }
            }

            return -1;
        }

        /// Finds or reserves one tracked probe-cube slot for the supplied entity.
        int GetOrCreateProbeCubeDebugSlot(Entity* entity) {
            if (entity == nullptr) {
                return -1;
            }

            int existingIndex = FindProbeCubeDebugSlot(entity);
            if (existingIndex >= 0) {
                return existingIndex;
            }

            for (int index = 0; index < MaxProbeCubeDebugSlotCount; index++) {
                ProbeCubeDebugSlot& slot = ProbeCubeDebugSlots[static_cast<std::size_t>(index)];
                if (slot.EntityValue == nullptr) {
                    slot.EntityValue = entity;
                    slot.FrameCount = 0;
                    return index;
                }
            }

            return -1;
        }
 
        /// Builds one stable generated identifier for a runtime texture that was created from embedded raw data. 
        std::string BuildGeneratedTextureResourceId() { 
            GeneratedTextureResourceId++; 
            return "__generated_runtime_texture_" + std::to_string(GeneratedTextureResourceId);
        }

        /// Builds one richer diagnostics detail string for a runtime texture upload.
        std::string BuildTextureDiagnosticsDetail(TextureAsset* data, const std::string& textureId) {
            if (data == nullptr) {
                return "source=unknown";
            }

            std::ostringstream builder;
            builder << "width=" << data->Width
                << " height=" << data->Height
                << " runtime_asset_id=" << data->get_RuntimeAssetId();

#if __has_include("RuntimeSceneLoadService.hpp")
            if (Core::get_Instance() != nullptr && Core::get_Instance()->get_SceneLoadService() != nullptr) {
                RuntimeSceneLoadService* sceneLoadService = Core::get_Instance()->get_SceneLoadService();
                builder << " scene_load_stage=" << sceneLoadService->get_LastTraceStage();
                builder << " root_entity_index=" << sceneLoadService->get_LastTraceRootEntityIndex();
                builder << " entity_depth=" << sceneLoadService->get_LastTraceEntityDepth();
                builder << " component_type=" << sceneLoadService->get_LastTraceComponentTypeId();
            }
#endif

            if (!textureId.empty() && textureId.rfind("__generated_runtime_texture_", 0) != 0) {
                builder << " source=authored";
                return builder.str();
            }

            builder << " source=generated";
#if __has_include("FontAssetBinarySerializer.hpp")
            const std::string fontDeserializeStage = FontAssetBinarySerializer::get_LastDeserializeStage();
            if (fontDeserializeStage == "BuildRuntimeTexture") {
                builder << " generated_kind=font_atlas";
                builder << " font_deserialize_stage=" << fontDeserializeStage;
#if __has_include("RuntimeSceneAssetReferenceResolver.hpp")
                if (Core::get_Instance() != nullptr && Core::get_Instance()->get_SceneAssetReferenceResolver() != nullptr) {
                    RuntimeSceneAssetReferenceResolver* referenceResolver = Core::get_Instance()->get_SceneAssetReferenceResolver();
                    builder << " text_font_relative_path=" << referenceResolver->get_LastTextFontRelativePath();
                    builder << " text_font_load_stage=" << referenceResolver->get_LastTextLoadStage();
                }
#endif
                return builder.str();
            }

            if (!fontDeserializeStage.empty()) {
                builder << " font_deserialize_stage=" << fontDeserializeStage;
            }
#endif
            if (data->IsEngineOwned) {
                builder << " generated_kind=engine_owned";
                return builder.str();
            }

            builder << " generated_kind=unlabeled";
            return builder.str();
        }

        /// Creates a descriptor for one default mesh vertex input layout.
        std::vector<D3D11_INPUT_ELEMENT_DESC> BuildDefaultInputElements() {
            std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
            elements.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
            elements.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 });
            elements.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 });
            return elements;
        }
    }

    /// Creates the native renderer bridge for one DirectX11 bootstrap.
    Win32RenderManager3D::Win32RenderManager3D(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap)
        , CurrentViewProjection(::float4x4::get_Identity())
        , CurrentShadowViewProjection(::float4x4::get_Identity())
        , CurrentCameraPosition(0.0f, 0.0f, 0.0f) {
    }

    /// Returns the number of uploaded texture resources currently cached by the Windows bridge.
    std::size_t Win32RenderManager3D::GetTextureResourceCount() const {
        return TextureResources.size();
    }

    /// Returns the number of compiled material shader resources currently cached by the Windows bridge.
    std::size_t Win32RenderManager3D::GetMaterialShaderResourceCount() const {
        return MaterialShaderResources.size();
    }

    /// Returns the number of authored material constant buffers currently cached by the Windows bridge.
    std::size_t Win32RenderManager3D::GetMaterialConstantBufferCount() const {
        return MaterialConstantBuffers.size();
    }

    /// Returns the number of uploaded runtime models currently retaining native vertex or index buffers.
    std::size_t Win32RenderManager3D::GetModelBufferCount() const {
        return LiveModelBufferCount;
    }

    /// Returns the total native bytes currently retained by uploaded model vertex buffers.
    std::size_t Win32RenderManager3D::GetModelVertexBufferBytes() const {
        return LiveModelVertexBufferBytes;
    }

    /// Returns the total native bytes currently retained by uploaded model index buffers.
    std::size_t Win32RenderManager3D::GetModelIndexBufferBytes() const {
        return LiveModelIndexBufferBytes;
    }

    /// Returns the total native bytes currently retained by authored material constant buffers.
    std::size_t Win32RenderManager3D::GetMaterialConstantBufferBytes() const {
        return LiveMaterialConstantBufferBytes;
    }

    /// Builds a GPU-ready runtime model from raw mesh asset metadata.
    RuntimeModel* Win32RenderManager3D::BuildModelFromRaw(ModelAsset* data) {
        Win32RuntimeModel* runtimeModel = new Win32RuntimeModel();
        if (data != nullptr) {
            std::string modelId = data->get_Id();
            runtimeModel->set_Id(modelId);

            const int32_t positionCount = data->Positions != nullptr ? data->Positions->Length : 0;
            const int32_t normalCount = data->Normals != nullptr ? data->Normals->Length : 0;
            const int32_t texCoordCount = data->TexCoords != nullptr ? data->TexCoords->Length : 0;
            const int32_t vertexCount = std::min(positionCount, std::min(normalCount, texCoordCount));
            runtimeModel->SourceVertexBytes =
                static_cast<std::size_t>(positionCount) * sizeof(float3)
                + static_cast<std::size_t>(normalCount) * sizeof(float3)
                + static_cast<std::size_t>(texCoordCount) * sizeof(float2);
            runtimeModel->SourceIndexBytes =
                data->Indices32 != nullptr && data->Indices32->Length > 0
                    ? static_cast<std::size_t>(data->Indices32->Length) * sizeof(uint32_t)
                    : (data->Indices16 != nullptr && data->Indices16->Length > 0
                        ? static_cast<std::size_t>(data->Indices16->Length) * sizeof(uint16_t)
                        : 0);
            if (vertexCount > 0) {
                std::vector<Win32VertexPositionNormalUV> vertices(static_cast<std::size_t>(vertexCount));
                for (int32_t index = 0; index < vertexCount; index++) {
                    const float3& position = (*data->Positions)[index];
                    const float3& normal = (*data->Normals)[index];
                    const float2& texCoord = (*data->TexCoords)[index];

                    vertices[static_cast<std::size_t>(index)] = Win32VertexPositionNormalUV {
                        DirectX::XMFLOAT3(position.X, position.Y, position.Z),
                        DirectX::XMFLOAT3(normal.X, normal.Y, normal.Z),
                        DirectX::XMFLOAT2(texCoord.X, texCoord.Y)
                    };
                }

                D3D11_BUFFER_DESC vertexBufferDescription {};
                vertexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(Win32VertexPositionNormalUV) * vertices.size());
                vertexBufferDescription.Usage = D3D11_USAGE_DEFAULT;
                vertexBufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;

                D3D11_SUBRESOURCE_DATA vertexData {};
                vertexData.pSysMem = vertices.data();

                ThrowIfFailed(
                    Bootstrap.GetDevice()->CreateBuffer(&vertexBufferDescription, &vertexData, runtimeModel->VertexBuffer.GetAddressOf()),
                    "ID3D11Device::CreateBuffer failed for the Windows mesh vertex buffer.");
                runtimeModel->VertexCount = static_cast<UINT>(vertices.size());
                runtimeModel->VertexBufferBytes = vertexBufferDescription.ByteWidth;
            }

            if (data->Indices32 != nullptr && data->Indices32->Length > 0) {
                D3D11_BUFFER_DESC indexBufferDescription {};
                indexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * data->Indices32->Length);
                indexBufferDescription.Usage = D3D11_USAGE_DEFAULT;
                indexBufferDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;

                D3D11_SUBRESOURCE_DATA indexData {};
                indexData.pSysMem = data->Indices32->Data;

                ThrowIfFailed(
                    Bootstrap.GetDevice()->CreateBuffer(&indexBufferDescription, &indexData, runtimeModel->IndexBuffer.GetAddressOf()),
                    "ID3D11Device::CreateBuffer failed for the Windows mesh 32-bit index buffer.");
                runtimeModel->IndexCount = static_cast<UINT>(data->Indices32->Length);
                runtimeModel->IndexFormat = DXGI_FORMAT_R32_UINT;
                runtimeModel->IndexBufferBytes = indexBufferDescription.ByteWidth;
            } else if (data->Indices16 != nullptr && data->Indices16->Length > 0) {
                D3D11_BUFFER_DESC indexBufferDescription {};
                indexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(uint16_t) * data->Indices16->Length);
                indexBufferDescription.Usage = D3D11_USAGE_DEFAULT;
                indexBufferDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;

                D3D11_SUBRESOURCE_DATA indexData {};
                indexData.pSysMem = data->Indices16->Data;

                ThrowIfFailed(
                    Bootstrap.GetDevice()->CreateBuffer(&indexBufferDescription, &indexData, runtimeModel->IndexBuffer.GetAddressOf()),
                    "ID3D11Device::CreateBuffer failed for the Windows mesh 16-bit index buffer.");
                runtimeModel->IndexCount = static_cast<UINT>(data->Indices16->Length);
                runtimeModel->IndexFormat = DXGI_FORMAT_R16_UINT;
                runtimeModel->IndexBufferBytes = indexBufferDescription.ByteWidth;
            }

            if (runtimeModel->VertexBufferBytes > 0 || runtimeModel->IndexBufferBytes > 0) {
                LiveModelBufferCount++;
                LiveModelVertexBufferBytes += runtimeModel->VertexBufferBytes;
                LiveModelIndexBufferBytes += runtimeModel->IndexBufferBytes;
            }

            RuntimeRenderDiagnostics::RecordAssetBuild(
                "model",
                modelId,
                "positions=" + std::to_string(positionCount)
                    + " normals=" + std::to_string(normalCount)
                    + " texcoords=" + std::to_string(texCoordCount)
                    + " source_vertex_bytes=" + std::to_string(runtimeModel->SourceVertexBytes)
                    + " source_index_bytes=" + std::to_string(runtimeModel->SourceIndexBytes)
                    + " vertex_buffer_bytes=" + std::to_string(runtimeModel->VertexBufferBytes)
                    + " index_buffer_bytes=" + std::to_string(runtimeModel->IndexBufferBytes)
                    + " live_model_buffers=" + std::to_string(LiveModelBufferCount)
                    + " live_model_vertex_buffer_bytes=" + std::to_string(LiveModelVertexBufferBytes)
                    + " live_model_index_buffer_bytes=" + std::to_string(LiveModelIndexBufferBytes),
                LiveModelBufferCount);

            if (!LoggedGeneratedCubeMeshData
                && modelId == "engine:model:cube"
                && data->Positions != nullptr
                && data->Normals != nullptr
                && data->Indices16 != nullptr
                && data->Positions->Length >= 20
                && data->Normals->Length >= 20
                && data->Indices16->Length >= 30) {
                LoggedGeneratedCubeMeshData = true;
                const float3& topVertex0 = (*data->Positions)[16];
                const float3& topVertex1 = (*data->Positions)[17];
                const float3& topVertex2 = (*data->Positions)[18];
                const float3& topVertex3 = (*data->Positions)[19];
                const float3& topNormal0 = (*data->Normals)[16];
                const float3& topNormal1 = (*data->Normals)[17];
                const float3& topNormal2 = (*data->Normals)[18];
                const float3& topNormal3 = (*data->Normals)[19];
                RuntimeRenderDiagnostics::WriteHostEvent(
                    "generated-cube-top-face",
                    "v16=" + std::to_string(topVertex0.X) + "," + std::to_string(topVertex0.Y) + "," + std::to_string(topVertex0.Z)
                        + " v17=" + std::to_string(topVertex1.X) + "," + std::to_string(topVertex1.Y) + "," + std::to_string(topVertex1.Z)
                        + " v18=" + std::to_string(topVertex2.X) + "," + std::to_string(topVertex2.Y) + "," + std::to_string(topVertex2.Z)
                        + " v19=" + std::to_string(topVertex3.X) + "," + std::to_string(topVertex3.Y) + "," + std::to_string(topVertex3.Z)
                        + " n16=" + std::to_string(topNormal0.X) + "," + std::to_string(topNormal0.Y) + "," + std::to_string(topNormal0.Z)
                        + " n17=" + std::to_string(topNormal1.X) + "," + std::to_string(topNormal1.Y) + "," + std::to_string(topNormal1.Z)
                        + " n18=" + std::to_string(topNormal2.X) + "," + std::to_string(topNormal2.Y) + "," + std::to_string(topNormal2.Z)
                        + " n19=" + std::to_string(topNormal3.X) + "," + std::to_string(topNormal3.Y) + "," + std::to_string(topNormal3.Z)
                        + " i24=" + std::to_string((*data->Indices16)[24])
                        + " i25=" + std::to_string((*data->Indices16)[25])
                        + " i26=" + std::to_string((*data->Indices16)[26])
                        + " i27=" + std::to_string((*data->Indices16)[27])
                        + " i28=" + std::to_string((*data->Indices16)[28])
                        + " i29=" + std::to_string((*data->Indices16)[29]));
            }
        }

        return runtimeModel;
    }

    RuntimeMaterial* Win32RenderManager3D::BuildMaterialFromRawAsset(ContentManager* assetContentManager, std::string contentRootPath, std::string materialAssetPath) {
        return ShaderRuntimeMaterialLoader::BuildMaterialFromRawAsset(this, assetContentManager, contentRootPath, materialAssetPath);
    }

    /// Builds a runtime material placeholder that keeps the packaged material identity.
    RuntimeMaterial* Win32RenderManager3D::BuildMaterialFromRaw(ShaderMaterialAsset* materialAsset, ShaderAsset* shaderAsset) {
        if (materialAsset == nullptr) {
            throw new ArgumentNullException("materialAsset");
        }

        if (shaderAsset == nullptr) {
            throw new ArgumentNullException("shaderAsset");
        }

        if (String::IsNullOrWhiteSpace(materialAsset->ShaderAssetId)) {
            throw new InvalidOperationException("Material assets must reference a shader asset id.");
        }

        if (!String::Equals(materialAsset->ShaderAssetId, shaderAsset->get_Id(), StringComparison::Ordinal)) {
            throw new InvalidOperationException("Material asset shader id does not match the provided shader asset.");
        }

        std::string materialId = materialAsset->get_Id();
        if (materialId.empty()) {
            materialId = shaderAsset->get_Id();
        }

        ShaderRuntimeMaterial* shaderRuntimeMaterial = new ShaderRuntimeMaterial();
        RuntimeMaterial* runtimeMaterial = shaderRuntimeMaterial;
        runtimeMaterial->set_Id(materialId);
        MaterialLayout* layout = MaterialLayoutBuilder::Build(materialAsset, shaderAsset);
        shaderRuntimeMaterial->SetLayout(layout);
        runtimeMaterial->SetRenderState(materialAsset->RenderState);
        shaderRuntimeMaterial->ApplyConstantBufferDefaults(materialAsset->ConstantBuffers);
#if __has_include("StandardMaterialTextureBindingDefaults.hpp")
        StandardMaterialTextureBindingDefaults::Apply(shaderRuntimeMaterial);
#endif

        std::size_t authoredConstantBufferCount = 0;
        std::size_t authoredConstantBufferBytes = 0;
        if (materialAsset->ConstantBuffers != nullptr) {
            authoredConstantBufferCount = static_cast<std::size_t>(materialAsset->ConstantBuffers->Length);
            for (int32_t constantBufferIndex = 0; constantBufferIndex < materialAsset->ConstantBuffers->Length; constantBufferIndex++) {
                MaterialConstantBufferAsset* constantBufferAsset = (*materialAsset->ConstantBuffers)[constantBufferIndex];
                if (constantBufferAsset == nullptr || constantBufferAsset->Data == nullptr) {
                    continue;
                }

                authoredConstantBufferBytes += static_cast<std::size_t>(constantBufferAsset->Data->Length);
            }
        }

        MaterialShaderResources[materialId] = std::make_unique<Win32ShaderResource>(BuildShaderResource(materialAsset, shaderAsset));
        RuntimeRenderDiagnostics::RecordAssetBuild(
            "material",
            materialId,
            "shader_asset_id=" + shaderAsset->get_Id()
                + " authored_constant_buffer_count=" + std::to_string(authoredConstantBufferCount)
                + " authored_constant_buffer_bytes=" + std::to_string(authoredConstantBufferBytes)
                + " live_material_shader_resources=" + std::to_string(MaterialShaderResources.size())
                + " live_material_constant_buffers=" + std::to_string(MaterialConstantBuffers.size())
                + " live_material_constant_buffer_bytes=" + std::to_string(LiveMaterialConstantBufferBytes),
            MaterialShaderResources.size());
        return runtimeMaterial;
    }

    /// Returns the shader compile target consumed by the Windows DirectX11 bridge.
    ShaderCompileTarget Win32RenderManager3D::get_ShaderCompileTarget() {
        return ShaderCompileTarget::DirectX11;
    }

    /// Invalidates cached shader resources that were built from one shader asset.
    void Win32RenderManager3D::InvalidateShaderResources(std::string shaderAssetId, ShaderAsset* shaderAsset) {
        if (String::IsNullOrWhiteSpace(shaderAssetId)) {
            throw new ArgumentException("Shader asset id must be provided.", "shaderAssetId");
        }

        if (shaderAsset == nullptr) {
            throw new ArgumentNullException("shaderAsset");
        }

        MaterialShaderResources.clear();
    }

    /// Resolves the shader-backed runtime material required by the native DirectX11 material binding path.
    ShaderRuntimeMaterial* Win32RenderManager3D::RequireShaderRuntimeMaterial(RuntimeMaterial* material) {
        if (material == nullptr) {
            throw new ArgumentNullException("material");
        }

        return ShaderRuntimeMaterialAccess::Require(material);
    }

    /// Releases one runtime model previously created by the Windows renderer.
    void Win32RenderManager3D::ReleaseModel(RuntimeModel* model) {
        if (model == nullptr) {
            throw std::invalid_argument("Runtime model must be provided for release.");
        }

        const std::string modelId = model->get_Id();
        Win32RuntimeModel* win32Model = static_cast<Win32RuntimeModel*>(model);
        RuntimeRenderDiagnostics::RecordAssetReleaseRequested(
            "model",
            modelId,
            "renderer=windows vertex_buffer_bytes=" + std::to_string(win32Model != nullptr ? win32Model->VertexBufferBytes : 0)
                + " index_buffer_bytes=" + std::to_string(win32Model != nullptr ? win32Model->IndexBufferBytes : 0)
                + " source_vertex_bytes=" + std::to_string(win32Model != nullptr ? win32Model->SourceVertexBytes : 0)
                + " source_index_bytes=" + std::to_string(win32Model != nullptr ? win32Model->SourceIndexBytes : 0));
        if (win32Model != nullptr) {
            if (win32Model->VertexBufferBytes > 0 || win32Model->IndexBufferBytes > 0) {
                if (LiveModelBufferCount > 0) {
                    LiveModelBufferCount--;
                }
                if (LiveModelVertexBufferBytes >= win32Model->VertexBufferBytes) {
                    LiveModelVertexBufferBytes -= win32Model->VertexBufferBytes;
                } else {
                    LiveModelVertexBufferBytes = 0;
                }
                if (LiveModelIndexBufferBytes >= win32Model->IndexBufferBytes) {
                    LiveModelIndexBufferBytes -= win32Model->IndexBufferBytes;
                } else {
                    LiveModelIndexBufferBytes = 0;
                }
            }
            win32Model->VertexBuffer.Reset();
            win32Model->IndexBuffer.Reset();
            win32Model->VertexCount = 0;
            win32Model->IndexCount = 0;
            win32Model->IndexFormat = DXGI_FORMAT_UNKNOWN;
            win32Model->VertexBufferBytes = 0;
            win32Model->IndexBufferBytes = 0;
        }

        RuntimeRenderDiagnostics::RecordAssetReleaseCompleted(
            "model",
            modelId,
            "renderer=windows live_model_buffers=" + std::to_string(LiveModelBufferCount)
                + " live_model_vertex_buffer_bytes=" + std::to_string(LiveModelVertexBufferBytes)
                + " live_model_index_buffer_bytes=" + std::to_string(LiveModelIndexBufferBytes));
    }

    /// Releases one runtime material previously created by the Windows renderer.
    void Win32RenderManager3D::ReleaseMaterial(RuntimeMaterial* material) {
        if (material == nullptr) {
            throw std::invalid_argument("Runtime material must be provided for release.");
        }

        const std::string materialId = material->get_Id();
        RuntimeRenderDiagnostics::RecordAssetReleaseRequested("material", materialId, "renderer=windows");
        if (!materialId.empty()) {
            MaterialShaderResources.erase(materialId);
        }

        RuntimeRenderDiagnostics::RecordAssetReleaseCompleted(
            "material",
            materialId,
            "renderer=windows material_shader_resources=" + std::to_string(MaterialShaderResources.size())
                + " material_constant_buffers=" + std::to_string(MaterialConstantBuffers.size())
                + " material_constant_buffer_bytes=" + std::to_string(LiveMaterialConstantBufferBytes));
    }

    /// Flushes any deferred Windows runtime asset releases.
    void Win32RenderManager3D::FlushReleasedAssets() {
        MaterialConstantBuffers.clear();
        LiveMaterialConstantBufferBytes = 0;
        HasWrittenRenderSnapshot = false;
        Logged3DVisitCount = 0;
        HasWritten2DSummary = false;
        HasWritten2DDraw = false;
        Logged2DVisitCount = 0;
        Logged2DRectCount = 0;
        Logged2DTextCount = 0;
        Logged2DSpriteCount = 0;
        Logged2DTextEarlyReturnCount = 0;
        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        if (context != nullptr) {
            context->ClearState();
            context->Flush();
        }
        RuntimeRenderDiagnostics::WriteHostEvent("asset-release", "Win32RenderManager3D flushed released assets.");
    }

    /// Releases Windows renderer-owned 3D resources.
    void Win32RenderManager3D::Dispose() {
        MaterialShaderResources.clear();
        MaterialConstantBuffers.clear();
        LiveModelBufferCount = 0;
        LiveModelVertexBufferBytes = 0;
        LiveModelIndexBufferBytes = 0;
        LiveMaterialConstantBufferBytes = 0;
        ForwardLightConstantBuffer.Reset();
        ShadowConstantBuffer.Reset();
        TextureSamplerState.Reset();
        ShadowSamplerState.Reset();
        DiagnosticVertexShader.Reset();
        DiagnosticPixelShader.Reset();
        DiagnosticInputLayout.Reset();
        InputLayout.Reset();
        ShadowInputLayout.Reset();
        TransformBuffer.Reset();
        ShadowTransformBuffer.Reset();
        DebugTriangleBuffer.Reset();
        RasterizerState.Reset();
        DepthStencilState.Reset();
        ShadowRasterizerState.Reset();
        ShadowDepthStencilState.Reset();
        ShadowVertexShader.Reset();
        ShadowPixelShader.Reset();
        ShadowMapTexture.Reset();
        ShadowMapShaderResourceView.Reset();
        ShadowMapDepthStencilView.Reset();
    }

    /// Draws every registered camera to the Windows back buffer in camera order.
    void Win32RenderManager3D::Draw() {
        RenderManager3D::Draw();
        EnsurePipelineState();
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("draw begin");
            AppendRenderDiagnosticsLine("3d.draw begin");
        }

        if (Core::get_Instance() == nullptr || Core::get_Instance()->get_ObjectManager() == nullptr) {
            ClearBackBuffer(0.0f, 0.0f, 0.0f, 1.0f);
            HasWrittenRenderSnapshot = true;
            return;
        }

        List<ICamera*>* cameras = Core::get_Instance()->get_ObjectManager()->get_Cameras();
        if (cameras == nullptr || cameras->Count() == 0) {
            if (!HasWrittenRenderSnapshot) {
                AppendRenderSnapshotLine("camera count=0");
                AppendRenderDiagnosticsLine("3d.camera_count=0");
            }

            ClearBackBuffer(0.0f, 0.0f, 0.0f, 1.0f);
            HasWrittenRenderSnapshot = true;
            return;
        }

        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("camera count=" + std::to_string(cameras->Count()));
            AppendRenderDiagnosticsLine("3d.camera_count=" + std::to_string(cameras->Count()));
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        context->IASetInputLayout(InputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* transformBuffer = TransformBuffer.Get();
        context->VSSetConstantBuffers(0, 1, &transformBuffer);
        context->PSSetConstantBuffers(0, 1, &transformBuffer);
        context->RSSetState(RasterizerState.Get());
        context->OMSetDepthStencilState(DepthStencilState.Get(), 0);

        bool renderedAnyCamera = false;
        bool clearedColorBuffer = false;
        for (int32_t index = 0; index < cameras->Count(); index++) {
            ICamera* camera = (*cameras)[index];
            if (camera == nullptr || camera->get_Parent() == nullptr || !camera->get_Parent()->get_IsHierarchyEnabled()) {
                continue;
            }

            if (camera->get_RenderTarget() != nullptr) {
                continue;
            }

            RenderCamera(camera, !clearedColorBuffer);
            clearedColorBuffer = true;
            renderedAnyCamera = true;
        }

        if (!renderedAnyCamera) {
            ClearBackBuffer(0.0f, 0.0f, 0.0f, 1.0f);
        }

        HasWrittenRenderSnapshot = true;
    }

    /// Draws one queued mesh for the currently active camera.
    void Win32RenderManager3D::Visit(IDrawable3D* drawable) {
        if (IsShadowPassActive) {
            DrawDirectionalShadowCaster(drawable, CurrentShadowViewProjection);
            return;
        }

        if (drawable == nullptr || drawable->get_Parent() == nullptr || !drawable->get_Parent()->get_IsHierarchyEnabled()) {
            return;
        }

        RuntimeModel* modelBase = drawable->get_Model();
        if (modelBase == nullptr) {
            return;
        }

        auto* model = static_cast<Win32RuntimeModel*>(modelBase);
        if (!model->VertexBuffer) {
            return;
        }

        Array<RuntimeMaterial*>* runtimeMaterials = drawable->get_Materials();
        RuntimeMaterial* runtimeMaterial = runtimeMaterials != nullptr && runtimeMaterials->Length > 0
            ? (*runtimeMaterials)[0]
            : nullptr;
        RuntimeMaterial* rootMaterial = runtimeMaterial != nullptr ? runtimeMaterial->ResolveRootMaterial() : nullptr;
        Win32ShaderResource* shaderResource = nullptr;
        if (rootMaterial != nullptr) {
            std::string materialId = rootMaterial->get_Id();
            if (!materialId.empty()) {
                auto materialResource = MaterialShaderResources.find(materialId);
                if (materialResource != MaterialShaderResources.end()) {
                    shaderResource = materialResource->second.get();
                }
            }
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        const UINT stride = sizeof(Win32VertexPositionNormalUV);
        const UINT offset = 0;
        ID3D11Buffer* vertexBuffer = model->VertexBuffer.Get();
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

        if (model->IndexBuffer && model->IndexCount > 0) {
            context->IASetIndexBuffer(model->IndexBuffer.Get(), model->IndexFormat, 0);
        }

        Entity* parent = drawable->get_Parent();
        if (!HasWrittenPlazaTowerViewDebug && IsDirectionalShadowCentralTower(parent)) {
            HasWrittenPlazaTowerViewDebug = true;
            const float3 entityPosition = parent->get_Position();
            const float3 entityScale = parent->get_Scale();
            const float4 entityOrientation = parent->get_Orientation();
            const float3 cameraToTower = float3::Normalize(CurrentCameraPosition - entityPosition);
            const float3 positiveX = float4::RotateVector(float3(1.0f, 0.0f, 0.0f), entityOrientation);
            const float3 negativeX = float4::RotateVector(float3(-1.0f, 0.0f, 0.0f), entityOrientation);
            const float3 positiveZ = float4::RotateVector(float3(0.0f, 0.0f, 1.0f), entityOrientation);
            const float3 negativeZ = float4::RotateVector(float3(0.0f, 0.0f, -1.0f), entityOrientation);
            AppendRenderDiagnosticsLine(
                "PlazaTowerViewDebug position="
                + std::to_string(entityPosition.X) + ","
                + std::to_string(entityPosition.Y) + ","
                + std::to_string(entityPosition.Z)
                + " scale="
                + std::to_string(entityScale.X) + ","
                + std::to_string(entityScale.Y) + ","
                + std::to_string(entityScale.Z)
                + " cameraToTower="
                + std::to_string(cameraToTower.X) + ","
                + std::to_string(cameraToTower.Y) + ","
                + std::to_string(cameraToTower.Z)
                + " pxView=" + std::to_string(float3::Dot(positiveX, cameraToTower))
                + " nxView=" + std::to_string(float3::Dot(negativeX, cameraToTower))
                + " pzView=" + std::to_string(float3::Dot(positiveZ, cameraToTower))
                + " nzView=" + std::to_string(float3::Dot(negativeZ, cameraToTower)));
        }
        float4 orientation = parent->get_Orientation();
        float3 scale = parent->get_Scale();
        float3 position = parent->get_Position();

        ::float4x4 world = parent->get_WorldTransformMatrix();

        if (GroundCubeProbeGroundDebugCount < 16 && IsGroundCubeProbeGround(parent)) {
            HasWrittenGroundCubeProbeGroundDebug = true;
            GroundCubeProbeGroundDebugCount++;
            AppendRenderDiagnosticsLine(
                "ProbeGround frame=" + std::to_string(GroundCubeProbeGroundDebugCount)
                + " world position="
                + std::to_string(position.X) + "," + std::to_string(position.Y) + "," + std::to_string(position.Z)
                + " scale="
                + std::to_string(scale.X) + "," + std::to_string(scale.Y) + "," + std::to_string(scale.Z)
                + " orientation="
                + std::to_string(orientation.X) + "," + std::to_string(orientation.Y) + "," + std::to_string(orientation.Z) + "," + std::to_string(orientation.W)
                + " rows="
                + "[" + std::to_string(world.M11) + "," + std::to_string(world.M12) + "," + std::to_string(world.M13) + "," + std::to_string(world.M14) + "]"
                + "[" + std::to_string(world.M21) + "," + std::to_string(world.M22) + "," + std::to_string(world.M23) + "," + std::to_string(world.M24) + "]"
                + "[" + std::to_string(world.M31) + "," + std::to_string(world.M32) + "," + std::to_string(world.M33) + "," + std::to_string(world.M34) + "]"
                + "[" + std::to_string(world.M41) + "," + std::to_string(world.M42) + "," + std::to_string(world.M43) + "," + std::to_string(world.M44) + "]");
            List<Component*>* components = parent->get_Components();
#if HE_WINDOWS_HAS_RUNTIME_PHYSICS_DEBUG_TYPES
            for (int componentIndex = 0; componentIndex < components->Count(); componentIndex++) {
                Component* component = components->get_Item(componentIndex);
                if (RigidBody3DComponent* rigidBody = dynamic_cast<RigidBody3DComponent*>(component)) {
                    const float3 linearVelocity = rigidBody->get_LinearVelocity();
                    const float3 angularVelocity = rigidBody->get_AngularVelocity();
                    AppendRenderDiagnosticsLine(
                        "ProbeGround rigidbody kind=" + std::to_string(static_cast<int>(rigidBody->get_BodyKind()))
                        + " gravity=" + std::to_string(rigidBody->get_UseGravity() ? 1 : 0)
                        + " mass=" + std::to_string(rigidBody->get_Mass())
                        + " linear=" + std::to_string(linearVelocity.X) + "," + std::to_string(linearVelocity.Y) + "," + std::to_string(linearVelocity.Z)
                        + " angular=" + std::to_string(angularVelocity.X) + "," + std::to_string(angularVelocity.Y) + "," + std::to_string(angularVelocity.Z));
                } else if (BoxCollider3DComponent* boxCollider = dynamic_cast<BoxCollider3DComponent*>(component)) {
                    const float3 sizeValue = boxCollider->get_Size();
                    AppendRenderDiagnosticsLine(
                        "ProbeGround collider size="
                        + std::to_string(sizeValue.X) + "," + std::to_string(sizeValue.Y) + "," + std::to_string(sizeValue.Z)
                        + " layer=" + std::to_string(boxCollider->get_CollisionLayer())
                        + " mask=" + std::to_string(boxCollider->get_CollisionMask())
                        + " trigger=" + std::to_string(boxCollider->get_IsTrigger() ? 1 : 0));
                }
            }
#endif
        }

        int probeCubeSlotIndex = -1;
        if (IsGroundCubeProbeCube(parent)) {
            probeCubeSlotIndex = GetOrCreateProbeCubeDebugSlot(parent);
        }

        if (probeCubeSlotIndex >= 0
            && GroundCubeProbeCubeDebugCount < (MaxProbeCubeDebugFrameCount * MaxProbeCubeDebugSlotCount)
            && ProbeCubeDebugSlots[static_cast<std::size_t>(probeCubeSlotIndex)].FrameCount < MaxProbeCubeDebugFrameCount) {
            HasWrittenGroundCubeProbeCubeDebug = true;
            GroundCubeProbeCubeDebugCount++;
            ProbeCubeDebugSlots[static_cast<std::size_t>(probeCubeSlotIndex)].FrameCount++;
            AppendRenderDiagnosticsLine(
                "ProbeCube[" + std::to_string(probeCubeSlotIndex) + "] frame=" + std::to_string(ProbeCubeDebugSlots[static_cast<std::size_t>(probeCubeSlotIndex)].FrameCount)
                + " material=" + (rootMaterial != nullptr ? rootMaterial->get_Id() : std::string("<null>"))
                + " world position="
                + std::to_string(position.X) + "," + std::to_string(position.Y) + "," + std::to_string(position.Z)
                + " scale="
                + std::to_string(scale.X) + "," + std::to_string(scale.Y) + "," + std::to_string(scale.Z)
                + " orientation="
                + std::to_string(orientation.X) + "," + std::to_string(orientation.Y) + "," + std::to_string(orientation.Z) + "," + std::to_string(orientation.W)
                + " rows="
                + "[" + std::to_string(world.M11) + "," + std::to_string(world.M12) + "," + std::to_string(world.M13) + "," + std::to_string(world.M14) + "]"
                + "[" + std::to_string(world.M21) + "," + std::to_string(world.M22) + "," + std::to_string(world.M23) + "," + std::to_string(world.M24) + "]"
                + "[" + std::to_string(world.M31) + "," + std::to_string(world.M32) + "," + std::to_string(world.M33) + "," + std::to_string(world.M34) + "]"
                + "[" + std::to_string(world.M41) + "," + std::to_string(world.M42) + "," + std::to_string(world.M43) + "," + std::to_string(world.M44) + "]");
            List<Component*>* components = parent->get_Components();
#if HE_WINDOWS_HAS_RUNTIME_PHYSICS_DEBUG_TYPES
            for (int componentIndex = 0; componentIndex < components->Count(); componentIndex++) {
                Component* component = components->get_Item(componentIndex);
                if (RigidBody3DComponent* rigidBody = dynamic_cast<RigidBody3DComponent*>(component)) {
                    const float3 linearVelocity = rigidBody->get_LinearVelocity();
                    const float3 angularVelocity = rigidBody->get_AngularVelocity();
                    AppendRenderDiagnosticsLine(
                        "ProbeCube[" + std::to_string(probeCubeSlotIndex) + "] rigidbody kind=" + std::to_string(static_cast<int>(rigidBody->get_BodyKind()))
                        + " gravity=" + std::to_string(rigidBody->get_UseGravity() ? 1 : 0)
                        + " mass=" + std::to_string(rigidBody->get_Mass())
                        + " linear=" + std::to_string(linearVelocity.X) + "," + std::to_string(linearVelocity.Y) + "," + std::to_string(linearVelocity.Z)
                        + " angular=" + std::to_string(angularVelocity.X) + "," + std::to_string(angularVelocity.Y) + "," + std::to_string(angularVelocity.Z));
                } else if (BoxCollider3DComponent* boxCollider = dynamic_cast<BoxCollider3DComponent*>(component)) {
                    const float3 sizeValue = boxCollider->get_Size();
                    AppendRenderDiagnosticsLine(
                        "ProbeCube[" + std::to_string(probeCubeSlotIndex) + "] collider size="
                        + std::to_string(sizeValue.X) + "," + std::to_string(sizeValue.Y) + "," + std::to_string(sizeValue.Z)
                        + " layer=" + std::to_string(boxCollider->get_CollisionLayer())
                        + " mask=" + std::to_string(boxCollider->get_CollisionMask())
                        + " trigger=" + std::to_string(boxCollider->get_IsTrigger() ? 1 : 0));
                }
            }
#endif
        }

        ::float4x4 inverseTransposeNormalMatrix;
        float4x4::InverseTranspose__ref0_out1(world, inverseTransposeNormalMatrix);
        ::float4x4 uploadedNormalMatrix;
        float4x4::Transpose__ref0_out1(inverseTransposeNormalMatrix, uploadedNormalMatrix);

        ::float4x4 worldViewProjection;
        float4x4::Multiply__ref0_ref1_out2(world, CurrentViewProjection, worldViewProjection);

        ::float4x4 transposedWorldViewProjection;
        float4x4::Transpose__ref0_out1(worldViewProjection, transposedWorldViewProjection);

        ::float4x4 transposedWorld;
        float4x4::Transpose__ref0_out1(world, transposedWorld);

        if (Logged3DVisitCount < MaxLogged3DVisitCount) {
            int rigidBodyKind = -1;
            List<Component*>* components = parent->get_Components();
#if HE_WINDOWS_HAS_RUNTIME_PHYSICS_DEBUG_TYPES
            for (int componentIndex = 0; componentIndex < components->Count(); componentIndex++) {
                if (RigidBody3DComponent* rigidBody = dynamic_cast<RigidBody3DComponent*>(components->get_Item(componentIndex))) {
                    rigidBodyKind = static_cast<int>(rigidBody->get_BodyKind());
                    break;
                }
            }
#endif

            std::string snapshotMaterialId = rootMaterial != nullptr ? rootMaterial->get_Id() : std::string("<null>");
            AppendRenderSnapshotLine(
                "drawable[" + std::to_string(Logged3DVisitCount) + "] position=" + std::to_string(position.X) + "," + std::to_string(position.Y) + "," + std::to_string(position.Z) +
                " scale=" + std::to_string(scale.X) + "," + std::to_string(scale.Y) + "," + std::to_string(scale.Z) +
                " orientation=" + std::to_string(orientation.X) + "," + std::to_string(orientation.Y) + "," + std::to_string(orientation.Z) + "," + std::to_string(orientation.W) +
                " material=" + snapshotMaterialId +
                " rigidbody_kind=" + std::to_string(rigidBodyKind) +
                " vertices=" + std::to_string(model->VertexCount) +
                " indices=" + std::to_string(model->IndexCount));
            AppendRenderSnapshotLine(
                "drawable[" + std::to_string(Logged3DVisitCount) + "] world rows="
                "[" + std::to_string(world.M11) + "," + std::to_string(world.M12) + "," + std::to_string(world.M13) + "," + std::to_string(world.M14) + "]"
                "[" + std::to_string(world.M21) + "," + std::to_string(world.M22) + "," + std::to_string(world.M23) + "," + std::to_string(world.M24) + "]"
                "[" + std::to_string(world.M31) + "," + std::to_string(world.M32) + "," + std::to_string(world.M33) + "," + std::to_string(world.M34) + "]"
                "[" + std::to_string(world.M41) + "," + std::to_string(world.M42) + "," + std::to_string(world.M43) + "," + std::to_string(world.M44) + "]");
            AppendRenderSnapshotLine(
                "drawable[" + std::to_string(Logged3DVisitCount) + "] uploaded world rows="
                "[" + std::to_string(transposedWorld.M11) + "," + std::to_string(transposedWorld.M12) + "," + std::to_string(transposedWorld.M13) + "," + std::to_string(transposedWorld.M14) + "]"
                "[" + std::to_string(transposedWorld.M21) + "," + std::to_string(transposedWorld.M22) + "," + std::to_string(transposedWorld.M23) + "," + std::to_string(transposedWorld.M24) + "]"
                "[" + std::to_string(transposedWorld.M31) + "," + std::to_string(transposedWorld.M32) + "," + std::to_string(transposedWorld.M33) + "," + std::to_string(transposedWorld.M34) + "]"
                "[" + std::to_string(transposedWorld.M41) + "," + std::to_string(transposedWorld.M42) + "," + std::to_string(transposedWorld.M43) + "," + std::to_string(transposedWorld.M44) + "]");
            AppendRenderSnapshotLine(
                "drawable[" + std::to_string(Logged3DVisitCount) + "] wvp rows="
                "[" + std::to_string(worldViewProjection.M11) + "," + std::to_string(worldViewProjection.M12) + "," + std::to_string(worldViewProjection.M13) + "," + std::to_string(worldViewProjection.M14) + "]"
                "[" + std::to_string(worldViewProjection.M21) + "," + std::to_string(worldViewProjection.M22) + "," + std::to_string(worldViewProjection.M23) + "," + std::to_string(worldViewProjection.M24) + "]"
                "[" + std::to_string(worldViewProjection.M31) + "," + std::to_string(worldViewProjection.M32) + "," + std::to_string(worldViewProjection.M33) + "," + std::to_string(worldViewProjection.M34) + "]"
                "[" + std::to_string(worldViewProjection.M41) + "," + std::to_string(worldViewProjection.M42) + "," + std::to_string(worldViewProjection.M43) + "," + std::to_string(worldViewProjection.M44) + "]");
            Logged3DVisitCount++;
            if (Logged3DVisitCount >= MaxLogged3DVisitCount) {
                HasWrittenRenderSnapshot = true;
            }
        }

        if (shaderResource != nullptr && rootMaterial != nullptr) {
            ApplyMaterial(runtimeMaterial != nullptr ? runtimeMaterial : rootMaterial);
            ID3D11Buffer* forwardLightBuffer = ForwardLightConstantBuffer.Get();
            context->PSSetConstantBuffers(1, 1, &forwardLightBuffer);
            ID3D11Buffer* shadowBuffer = ShadowConstantBuffer.Get();
            context->PSSetConstantBuffers(2, 1, &shadowBuffer);
            ID3D11ShaderResourceView* shadowShaderResourceView = ShadowMapShaderResourceView.Get();
            context->PSSetShaderResources(1, 1, &shadowShaderResourceView);
            ID3D11SamplerState* shadowSamplerState = ShadowSamplerState.Get();
            context->PSSetSamplers(1, 1, &shadowSamplerState);
            ID3D11ShaderResourceView* pointShadowShaderResources[4] = { nullptr, nullptr, nullptr, nullptr };
            context->PSSetShaderResources(2, 4, pointShadowShaderResources);
            ID3D11SamplerState* pointShadowSamplerState = nullptr;
            context->PSSetSamplers(2, 1, &pointShadowSamplerState);
        } else {
            context->IASetInputLayout(InputLayout.Get());
            context->VSSetShader(VertexShader.Get(), nullptr, 0);
            context->PSSetShader(PixelShader.Get(), nullptr, 0);
            ID3D11Buffer* transformBuffer = TransformBuffer.Get();
            context->VSSetConstantBuffers(0, 1, &transformBuffer);
            context->PSSetConstantBuffers(0, 1, &transformBuffer);
        }

        Win32TransformConstants constants {};
        constants.World = StoreMatrix(transposedWorld);
        constants.WorldViewProjection = StoreMatrix(transposedWorldViewProjection);
        constants.WorldNormal = StoreMatrix(uploadedNormalMatrix);
        constants.CameraPosition = DirectX::XMFLOAT4(CurrentCameraPosition.X, CurrentCameraPosition.Y, CurrentCameraPosition.Z, 0.0f);
        constants.MaterialFlags = DirectX::XMFLOAT4(
            runtimeMaterial != nullptr && runtimeMaterial->get_ReceivesShadows() ? 1.0f : 0.0f,
            0.0f,
            0.0f,
            0.0f);
        constants.LightDirection = DirectX::XMFLOAT4(-0.35f, -0.65f, -0.55f, 0.0f);
        constants.LightColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        constants.AmbientColor = DirectX::XMFLOAT4(0.16f, 0.18f, 0.22f, 1.0f);
        constants.SpecularColor = DirectX::XMFLOAT4(0.9f, 0.9f, 0.95f, 1.0f);
        constants.MaterialParameters = DirectX::XMFLOAT4(24.0f, 0.55f, 0.0f, 0.0f);
        context->UpdateSubresource(TransformBuffer.Get(), 0, nullptr, &constants, 0, 0);

        if (model->IndexBuffer && model->IndexCount > 0) {
            context->DrawIndexed(model->IndexCount, 0, 0);
        } else {
            context->Draw(model->VertexCount, 0);
        }

    }

    /// Creates the shaders, input layout, and fixed pipeline state on first use.
    void Win32RenderManager3D::EnsurePipelineState() {
        if (VertexShader && PixelShader && InputLayout && TransformBuffer && RasterizerState && DepthStencilState) {
            EnsureShadowPipelineState();
            EnsureTextureSamplerState();
            return;
        }

        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("ensure pipeline state begin");
        }

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

        HRESULT vertexShaderResult = D3DCompile(VertexShaderSource, strlen(VertexShaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vertexShaderBlob.GetAddressOf(), errorBlob.GetAddressOf());
        if (FAILED(vertexShaderResult) && errorBlob) {
            AppendRenderSnapshotLine(std::string("vertex shader compile failed: ") + static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        ThrowIfFailed(
            vertexShaderResult,
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge vertex shader.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("vertex shader compiled");
        }
        errorBlob.Reset();
        HRESULT pixelShaderResult = D3DCompile(PixelShaderSource, strlen(PixelShaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, pixelShaderBlob.GetAddressOf(), errorBlob.GetAddressOf());
        if (FAILED(pixelShaderResult) && errorBlob) {
            AppendRenderSnapshotLine(std::string("pixel shader compile failed: ") + static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        ThrowIfFailed(
            pixelShaderResult,
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge pixel shader.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("pixel shader compiled");
        }

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, VertexShader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader failed for the Windows bridge.");
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, PixelShader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader failed for the Windows bridge.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("pipeline shaders created");
        }

        static const D3D11_INPUT_ELEMENT_DESC InputElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateInputLayout(
                InputElements,
                static_cast<UINT>(std::size(InputElements)),
                vertexShaderBlob->GetBufferPointer(),
                vertexShaderBlob->GetBufferSize(),
                InputLayout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout failed for the Windows bridge.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("input layout created");
        }

        D3D11_BUFFER_DESC constantBufferDescription {};
        constantBufferDescription.ByteWidth = sizeof(Win32TransformConstants);
        constantBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        constantBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateBuffer(&constantBufferDescription, nullptr, TransformBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for the Windows bridge transform buffer.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("transform buffer created");
        }

        EnsureTextureSamplerState();

        D3D11_RASTERIZER_DESC rasterizerDescription {};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.FrontCounterClockwise = TRUE;
        rasterizerDescription.DepthClipEnable = TRUE;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateRasterizerState(&rasterizerDescription, RasterizerState.GetAddressOf()),
            "ID3D11Device::CreateRasterizerState failed for the Windows bridge.");

        D3D11_DEPTH_STENCIL_DESC depthStencilDescription {};
        depthStencilDescription.DepthEnable = TRUE;
        depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthStencilDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        depthStencilDescription.StencilEnable = TRUE;
        depthStencilDescription.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        depthStencilDescription.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
        depthStencilDescription.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        depthStencilDescription.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        depthStencilDescription.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        depthStencilDescription.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        depthStencilDescription.BackFace = depthStencilDescription.FrontFace;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateDepthStencilState(&depthStencilDescription, DepthStencilState.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilState failed for the Windows bridge.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("depth stencil state created");
        }

        Microsoft::WRL::ComPtr<ID3DBlob> diagnosticVertexShaderBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> diagnosticPixelShaderBlob;
        errorBlob.Reset();
        ThrowIfFailed(
            D3DCompile(DiagnosticVertexShaderSource, strlen(DiagnosticVertexShaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, diagnosticVertexShaderBlob.GetAddressOf(), errorBlob.GetAddressOf()),
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge diagnostic vertex shader.");
        errorBlob.Reset();
        ThrowIfFailed(
            D3DCompile(DiagnosticPixelShaderSource, strlen(DiagnosticPixelShaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, diagnosticPixelShaderBlob.GetAddressOf(), errorBlob.GetAddressOf()),
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge diagnostic pixel shader.");

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateVertexShader(diagnosticVertexShaderBlob->GetBufferPointer(), diagnosticVertexShaderBlob->GetBufferSize(), nullptr, DiagnosticVertexShader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader failed for the Windows bridge diagnostic pass.");
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreatePixelShader(diagnosticPixelShaderBlob->GetBufferPointer(), diagnosticPixelShaderBlob->GetBufferSize(), nullptr, DiagnosticPixelShader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader failed for the Windows bridge diagnostic pass.");

        static const D3D11_INPUT_ELEMENT_DESC DiagnosticInputElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateInputLayout(
                DiagnosticInputElements,
                static_cast<UINT>(std::size(DiagnosticInputElements)),
                diagnosticVertexShaderBlob->GetBufferPointer(),
                diagnosticVertexShaderBlob->GetBufferSize(),
                DiagnosticInputLayout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout failed for the Windows bridge diagnostic pass.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("diagnostic pipeline created");
        }

        EnsureShadowPipelineState();
    }

    /// Creates the shaders, constant buffers, and textures required by the directional shadow path.
    void Win32RenderManager3D::EnsureShadowPipelineState() {
        if (ForwardLightConstantBuffer
            && ShadowConstantBuffer
            && ShadowTransformBuffer
            && ShadowVertexShader
            && ShadowPixelShader
            && ShadowInputLayout
            && ShadowRasterizerState
            && ShadowDepthStencilState
            && ShadowMapTexture
            && ShadowMapShaderResourceView
            && ShadowMapDepthStencilView
            && ShadowSamplerState) {
            return;
        }

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

        ThrowIfFailed(
            D3DCompile(ShadowVertexShaderSource, strlen(ShadowVertexShaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vertexShaderBlob.GetAddressOf(), errorBlob.GetAddressOf()),
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge shadow vertex shader.");
        errorBlob.Reset();
        ThrowIfFailed(
            D3DCompile(ShadowPixelShaderSource, strlen(ShadowPixelShaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, pixelShaderBlob.GetAddressOf(), errorBlob.GetAddressOf()),
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge shadow pixel shader.");

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, ShadowVertexShader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader failed for the Windows bridge shadow pass.");
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, ShadowPixelShader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader failed for the Windows bridge shadow pass.");

        static const D3D11_INPUT_ELEMENT_DESC ShadowInputElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateInputLayout(
                ShadowInputElements,
                static_cast<UINT>(std::size(ShadowInputElements)),
                vertexShaderBlob->GetBufferPointer(),
                vertexShaderBlob->GetBufferSize(),
                ShadowInputLayout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout failed for the Windows bridge shadow pass.");

        D3D11_BUFFER_DESC forwardLightBufferDescription {};
        forwardLightBufferDescription.ByteWidth = sizeof(Win32ForwardLightConstants);
        forwardLightBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        forwardLightBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateBuffer(&forwardLightBufferDescription, nullptr, ForwardLightConstantBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for the Windows bridge forward-light constant buffer.");

        D3D11_BUFFER_DESC shadowBufferDescription {};
        shadowBufferDescription.ByteWidth = sizeof(Win32ShadowConstants);
        shadowBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        shadowBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateBuffer(&shadowBufferDescription, nullptr, ShadowConstantBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for the Windows bridge shadow constant buffer.");

        D3D11_BUFFER_DESC shadowTransformBufferDescription {};
        shadowTransformBufferDescription.ByteWidth = sizeof(Win32ShadowTransformConstants);
        shadowTransformBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        shadowTransformBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateBuffer(&shadowTransformBufferDescription, nullptr, ShadowTransformBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for the Windows bridge shadow transform buffer.");

        D3D11_TEXTURE2D_DESC shadowTextureDescription {};
        shadowTextureDescription.Width = DirectionalShadowMapResolution;
        shadowTextureDescription.Height = DirectionalShadowMapResolution;
        shadowTextureDescription.MipLevels = 1;
        shadowTextureDescription.ArraySize = 1;
        shadowTextureDescription.Format = DXGI_FORMAT_R32_TYPELESS;
        shadowTextureDescription.SampleDesc.Count = 1;
        shadowTextureDescription.Usage = D3D11_USAGE_DEFAULT;
        shadowTextureDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateTexture2D(&shadowTextureDescription, nullptr, ShadowMapTexture.GetAddressOf()),
            "ID3D11Device::CreateTexture2D failed for the Windows bridge shadow map.");

        D3D11_DEPTH_STENCIL_VIEW_DESC shadowDepthStencilViewDescription {};
        shadowDepthStencilViewDescription.Format = DXGI_FORMAT_D32_FLOAT;
        shadowDepthStencilViewDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateDepthStencilView(ShadowMapTexture.Get(), &shadowDepthStencilViewDescription, ShadowMapDepthStencilView.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilView failed for the Windows bridge shadow depth buffer.");

        D3D11_SHADER_RESOURCE_VIEW_DESC shadowShaderResourceViewDescription {};
        shadowShaderResourceViewDescription.Format = DXGI_FORMAT_R32_FLOAT;
        shadowShaderResourceViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        shadowShaderResourceViewDescription.Texture2D.MipLevels = 1;
        shadowShaderResourceViewDescription.Texture2D.MostDetailedMip = 0;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateShaderResourceView(ShadowMapTexture.Get(), &shadowShaderResourceViewDescription, ShadowMapShaderResourceView.GetAddressOf()),
            "ID3D11Device::CreateShaderResourceView failed for the Windows bridge shadow map.");

        D3D11_SAMPLER_DESC shadowSamplerDescription {};
        shadowSamplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        shadowSamplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        shadowSamplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        shadowSamplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        shadowSamplerDescription.MinLOD = 0;
        shadowSamplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateSamplerState(&shadowSamplerDescription, ShadowSamplerState.GetAddressOf()),
            "ID3D11Device::CreateSamplerState failed for the Windows bridge shadow sampler.");

        D3D11_RASTERIZER_DESC shadowRasterizerDescription {};
        shadowRasterizerDescription.FillMode = D3D11_FILL_SOLID;
        shadowRasterizerDescription.CullMode = D3D11_CULL_NONE;
        shadowRasterizerDescription.FrontCounterClockwise = TRUE;
        shadowRasterizerDescription.DepthClipEnable = TRUE;
        shadowRasterizerDescription.DepthBias = ShadowDepthBias;
        shadowRasterizerDescription.SlopeScaledDepthBias = ShadowSlopeScaledDepthBias;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateRasterizerState(&shadowRasterizerDescription, ShadowRasterizerState.GetAddressOf()),
            "ID3D11Device::CreateRasterizerState failed for the Windows bridge shadow rasterizer state.");

        D3D11_DEPTH_STENCIL_DESC shadowDepthStencilDescription {};
        shadowDepthStencilDescription.DepthEnable = TRUE;
        shadowDepthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        shadowDepthStencilDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateDepthStencilState(&shadowDepthStencilDescription, ShadowDepthStencilState.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilState failed for the Windows bridge shadow depth state.");
    }

    /// Creates the default sampler used by material texture bindings.
    void Win32RenderManager3D::EnsureTextureSamplerState() {
        if (TextureSamplerState) {
            return;
        }

        D3D11_SAMPLER_DESC samplerDescription {};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDescription.MinLOD = 0.0f;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateSamplerState(&samplerDescription, TextureSamplerState.GetAddressOf()),
            "ID3D11Device::CreateSamplerState failed for the Windows bridge texture sampler.");
    }

    /// Creates the small diagnostic triangle buffer used to prove the native pipeline can draw to the back buffer.
    void Win32RenderManager3D::EnsureDebugTriangleBuffer() {
        if (DebugTriangleBuffer) {
            return;
        }

        const Win32VertexPositionNormalUV vertices[] = {
            { DirectX::XMFLOAT3(-1.0f, -1.0f, 0.0f), DirectX::XMFLOAT3(0.0f, 0.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },
            { DirectX::XMFLOAT3(-1.0f, 3.0f, 0.0f), DirectX::XMFLOAT3(0.0f, 0.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) },
            { DirectX::XMFLOAT3(3.0f, -1.0f, 0.0f), DirectX::XMFLOAT3(0.0f, 0.0f, -1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) }
        };

        D3D11_BUFFER_DESC vertexBufferDescription {};
        vertexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(vertices));
        vertexBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        vertexBufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vertexData {};
        vertexData.pSysMem = vertices;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateBuffer(&vertexBufferDescription, &vertexData, DebugTriangleBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for the Windows bridge debug triangle.");
    }

    /// Clears and renders one camera directly into the main back buffer.
    void Win32RenderManager3D::RenderCamera(ICamera* camera, bool clearColorBuffer) {
        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        ID3D11RenderTargetView* renderTargetView = Bootstrap.GetRenderTargetView();
        ID3D11DepthStencilView* depthStencilView = Bootstrap.GetDepthStencilView();

        context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);

        CameraClearSettings clearSettings = camera->get_ClearSettings();
        if (clearColorBuffer) {
            float4 clearColor = clearSettings.get_ClearColorEnabled() ? clearSettings.get_ClearColor() : float4(0.0f, 0.0f, 0.0f, 1.0f);
            const float clearColorValues[] = { clearColor.X, clearColor.Y, clearColor.Z, clearColor.W };
            context->ClearRenderTargetView(renderTargetView, clearColorValues);
        }

        UINT clearFlags = 0;
        if (clearSettings.get_ClearDepthEnabled()) {
            clearFlags |= D3D11_CLEAR_DEPTH;
        }
        if (clearSettings.get_ClearStencilEnabled()) {
            clearFlags |= D3D11_CLEAR_STENCIL;
        }
        if (clearFlags != 0 && depthStencilView != nullptr) {
            context->ClearDepthStencilView(depthStencilView, clearFlags, clearSettings.get_ClearDepth(), clearSettings.get_ClearStencil());
        }

        const D3D11_VIEWPORT viewport = ResolveViewport(camera);
        context->RSSetViewports(1, &viewport);
        if (!HasWrittenRenderSnapshot) {
            AppendRenderDiagnosticsLine(
                "3d.render_camera viewport="
                + std::to_string(viewport.TopLeftX) + ","
                + std::to_string(viewport.TopLeftY) + ","
                + std::to_string(viewport.Width) + ","
                + std::to_string(viewport.Height));
        }

        Entity* cameraParent = camera->get_Parent();
        float3 cameraPosition = cameraParent->get_Position();
        float4 cameraOrientation = cameraParent->get_Orientation();
        float3 cameraForward = float4::RotateVector(float3(0.0f, 0.0f, -1.0f), cameraOrientation);
        float3 cameraUp = float4::RotateVector(float3(0.0f, 1.0f, 0.0f), cameraOrientation);
        float3 cameraTarget = cameraPosition + cameraForward;
        CurrentCameraPosition = cameraPosition;

        ::float4x4 view;
        float4x4::CreateLookAt__ref0_ref1_ref2_out3(cameraPosition, cameraTarget, cameraUp, view);
        const float aspectRatio = viewport.Height > 0.0f ? viewport.Width / viewport.Height : 1.0f;
        constexpr float CameraFieldOfViewRadians = 0.78539816339f;
        float nearPlaneDistance = camera->get_NearPlaneDistance();
        if (nearPlaneDistance <= 0.0f) {
            nearPlaneDistance = 0.1f;
        }

        float farPlaneDistance = camera->get_FarPlaneDistance();
        if (farPlaneDistance <= nearPlaneDistance) {
            farPlaneDistance = nearPlaneDistance + 0.1f;
        }

        ::float4x4 projection;
        float4x4::CreatePerspectiveFieldOfView__out4(CameraFieldOfViewRadians, aspectRatio, nearPlaneDistance, farPlaneDistance, projection);
        float4x4::Multiply__ref0_ref1_out2(view, projection, CurrentViewProjection);

        std::vector<LightComponent*> visibleLights = SnapshotVisibleLights(camera);
        if (!HasWrittenRenderSnapshot) {
            AppendRenderDiagnosticsLine("3d.visible_lights=" + std::to_string(visibleLights.size()));
        }
        PrepareForwardLightState(visibleLights);
        PrepareShadowState(camera, visibleLights);
        context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
        context->RSSetViewports(1, &viewport);
        context->RSSetState(RasterizerState.Get());
        context->OMSetDepthStencilState(DepthStencilState.Get(), 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine(
                "camera position=" + std::to_string(cameraPosition.X) + "," + std::to_string(cameraPosition.Y) + "," + std::to_string(cameraPosition.Z) +
                " forward=" + std::to_string(cameraForward.X) + "," + std::to_string(cameraForward.Y) + "," + std::to_string(cameraForward.Z) +
                " up=" + std::to_string(cameraUp.X) + "," + std::to_string(cameraUp.Y) + "," + std::to_string(cameraUp.Z));
            AppendRenderSnapshotLine(
                "viewport=" + std::to_string(viewport.TopLeftX) + "," + std::to_string(viewport.TopLeftY) + "," + std::to_string(viewport.Width) + "," + std::to_string(viewport.Height));
            AppendRenderSnapshotLine(
                "viewProjection rows="
                "[" + std::to_string(CurrentViewProjection.M11) + "," + std::to_string(CurrentViewProjection.M12) + "," + std::to_string(CurrentViewProjection.M13) + "," + std::to_string(CurrentViewProjection.M14) + "]"
                "[" + std::to_string(CurrentViewProjection.M21) + "," + std::to_string(CurrentViewProjection.M22) + "," + std::to_string(CurrentViewProjection.M23) + "," + std::to_string(CurrentViewProjection.M24) + "]"
                "[" + std::to_string(CurrentViewProjection.M31) + "," + std::to_string(CurrentViewProjection.M32) + "," + std::to_string(CurrentViewProjection.M33) + "," + std::to_string(CurrentViewProjection.M34) + "]"
                "[" + std::to_string(CurrentViewProjection.M41) + "," + std::to_string(CurrentViewProjection.M42) + "," + std::to_string(CurrentViewProjection.M43) + "," + std::to_string(CurrentViewProjection.M44) + "]");
        }

        IRenderQueue3D* renderQueue = camera->get_RenderQueue3D();
        if (renderQueue != nullptr) {
            renderQueue->VisitOrdered(this);
        }

        Win32RenderManager2D* renderManager2D = static_cast<Win32RenderManager2D*>(Core::get_Instance()->get_RenderManager2D());
        if (renderManager2D != nullptr) {
            renderManager2D->RenderCamera(camera);
        }
    }

    /// Copies the currently visible authored lights relevant to one camera into a render-ready list.
    std::vector<LightComponent*> Win32RenderManager3D::SnapshotVisibleLights(ICamera* camera) const {
        if (camera == nullptr) {
            throw new ArgumentNullException("camera");
        }

        std::vector<LightComponent*> lights;
        if (Core::get_Instance() == nullptr || Core::get_Instance()->get_ObjectManager() == nullptr) {
            return lights;
        }

        List<Entity*>* entities = Core::get_Instance()->get_ObjectManager()->get_Entities();
        if (entities == nullptr) {
            return lights;
        }

        for (int32_t entityIndex = 0; entityIndex < entities->Count(); entityIndex++) {
            Entity* entity = (*entities)[entityIndex];
            if (entity == nullptr || !entity->get_IsHierarchyEnabled()) {
                continue;
            } else if ((entity->get_LayerMask() & camera->get_LayerMask()) == 0) {
                continue;
            }

            List<Component*>* components = entity->get_Components();
            if (components == nullptr) {
                continue;
            }

            for (int32_t componentIndex = 0; componentIndex < components->Count(); componentIndex++) {
                Component* component = (*components)[componentIndex];
                LightComponent* light = dynamic_cast<LightComponent*>(component);
                if (light != nullptr) {
                    lights.push_back(light);
                }
            }
        }

        return lights;
    }

    /// Uploads the packed forward-light constant buffer used by built-in forward scene shaders.
    void Win32RenderManager3D::PrepareForwardLightState(const std::vector<LightComponent*>& lights) {
        EnsureShadowPipelineState();

        constexpr int32_t AmbientLightTypeValue = 3;

        Win32ForwardLightConstants constants {};
        DirectX::XMFLOAT3 ambientLightColor(0.0f, 0.0f, 0.0f);
        int32_t packedLightCount = 0;
        for (std::size_t lightIndex = 0; lightIndex < lights.size() && packedLightCount < 4; lightIndex++) {
            LightComponent* light = lights[lightIndex];
            if (light == nullptr || light->get_Parent() == nullptr) {
                continue;
            }

            float4 color = light->get_Color();
            float intensity = light->get_Intensity();
            if (static_cast<int32_t>(light->get_LightType()) == AmbientLightTypeValue) {
                ambientLightColor.x += color.X * intensity;
                ambientLightColor.y += color.Y * intensity;
                ambientLightColor.z += color.Z * intensity;
                continue;
            }

            Win32ForwardLightSlotConstants slot {};
            slot.ColorAndType = DirectX::XMFLOAT4(
                color.X * intensity,
                color.Y * intensity,
                color.Z * intensity,
                static_cast<float>(light->get_LightType()));

            float3 lightDirection = LightDirectionUtility::GetEntityForwardDirection(light->get_Parent());
            slot.DirectionAndShadow = DirectX::XMFLOAT4(
                lightDirection.X,
                lightDirection.Y,
                lightDirection.Z,
                light->get_ShadowStrength());

            if (PointLightComponent* pointLight = dynamic_cast<PointLightComponent*>(light)) {
                float3 lightPosition = pointLight->get_Parent()->get_Position();
                slot.PositionAndRange = DirectX::XMFLOAT4(
                    lightPosition.X,
                    lightPosition.Y,
                    lightPosition.Z,
                    pointLight->get_Range());
            } else if (SpotLightComponent* spotLight = dynamic_cast<SpotLightComponent*>(light)) {
                float3 lightPosition = spotLight->get_Parent()->get_Position();
                slot.PositionAndRange = DirectX::XMFLOAT4(
                    lightPosition.X,
                    lightPosition.Y,
                    lightPosition.Z,
                    spotLight->get_Range());

                double innerRadians = static_cast<double>(spotLight->get_InnerConeAngleDegrees()) * (std::numbers::pi / 180.0);
                double outerRadians = static_cast<double>(spotLight->get_OuterConeAngleDegrees()) * (std::numbers::pi / 180.0);
                slot.SpotAngles = DirectX::XMFLOAT4(
                    static_cast<float>(std::cos(innerRadians)),
                    static_cast<float>(std::cos(outerRadians)),
                    0.0f,
                    0.0f);
            }

            if (packedLightCount == 0) {
                constants.Light0 = slot;
            } else if (packedLightCount == 1) {
                constants.Light1 = slot;
            } else if (packedLightCount == 2) {
                constants.Light2 = slot;
            } else if (packedLightCount == 3) {
                constants.Light3 = slot;
            }

            packedLightCount++;
        }

        constants.AmbientLightColor = DirectX::XMFLOAT4(
            ambientLightColor.x,
            ambientLightColor.y,
            ambientLightColor.z,
            0.0f);
        constants.LightMetadata = DirectX::XMFLOAT4(static_cast<float>(packedLightCount), 0.0f, 0.0f, 0.0f);
        if (!HasWrittenRenderSnapshot) {
            AppendRenderDiagnosticsLine("3d.ambient_light=" + std::to_string(constants.AmbientLightColor.x) + "," + std::to_string(constants.AmbientLightColor.y) + "," + std::to_string(constants.AmbientLightColor.z));
            AppendRenderDiagnosticsLine("3d.packed_lights=" + std::to_string(packedLightCount));
            if (packedLightCount > 0) {
                AppendRenderSnapshotLine(
                    "light0 colorAndType="
                    + std::to_string(constants.Light0.ColorAndType.x) + ","
                    + std::to_string(constants.Light0.ColorAndType.y) + ","
                    + std::to_string(constants.Light0.ColorAndType.z) + ","
                    + std::to_string(constants.Light0.ColorAndType.w)
                    + " directionAndShadow="
                    + std::to_string(constants.Light0.DirectionAndShadow.x) + ","
                    + std::to_string(constants.Light0.DirectionAndShadow.y) + ","
                    + std::to_string(constants.Light0.DirectionAndShadow.z) + ","
                    + std::to_string(constants.Light0.DirectionAndShadow.w));
            }
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        context->UpdateSubresource(ForwardLightConstantBuffer.Get(), 0, nullptr, &constants, 0, 0);
        ID3D11Buffer* buffer = ForwardLightConstantBuffer.Get();
        context->PSSetConstantBuffers(1, 1, &buffer);
    }

    /// Uploads the packed shadow constant buffer and bindings for the active directional shadow light.
    void Win32RenderManager3D::PrepareShadowState(ICamera* camera, const std::vector<LightComponent*>& lights) {
        if (camera == nullptr) {
            throw new ArgumentNullException("camera");
        }

        EnsureShadowPipelineState();

        DirectionalLightComponent* light = FindPrimaryDirectionalShadowLight(lights);
        if (!HasWrittenRenderSnapshot) {
            AppendRenderDiagnosticsLine(std::string("3d.shadow_light=") + (light != nullptr ? "directional" : "none"));
            if (light != nullptr) {
                AppendRenderSnapshotLine(
                    "shadow light strength=" + std::to_string(light->get_ShadowStrength())
                    + " distance=" + std::to_string(light->get_ShadowDistance()));
            }
        }
        if (light != nullptr) {
            RenderDirectionalShadowMap(camera, light);
        }

        Win32ShadowConstants constants {};
        if (light != nullptr) {
            ::float4x4 worldToShadowClip = BuildDirectionalShadowViewProjection(camera, light);
            ::float4x4 transposedWorldToShadowClip;
            float4x4::Transpose__ref0_out1(worldToShadowClip, transposedWorldToShadowClip);

            constants.ShadowMetadata = DirectX::XMFLOAT4(
                1.0f,
                1.0f / static_cast<float>(DirectionalShadowMapResolution),
                1.0f / static_cast<float>(DirectionalShadowMapResolution),
                1.0f);
            constants.Light0.AtlasRect = DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f);
            constants.Light0.Metadata = DirectX::XMFLOAT4(1.0f, light->get_ShadowStrength(), 1.0f, 0.0f);
            constants.Light0.WorldToShadowClip = StoreMatrix(transposedWorldToShadowClip);
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        context->UpdateSubresource(ShadowConstantBuffer.Get(), 0, nullptr, &constants, 0, 0);
        ID3D11Buffer* buffer = ShadowConstantBuffer.Get();
        context->PSSetConstantBuffers(2, 1, &buffer);

        ID3D11ShaderResourceView* shadowShaderResourceView = light != nullptr ? ShadowMapShaderResourceView.Get() : nullptr;
        context->PSSetShaderResources(1, 1, &shadowShaderResourceView);
        ID3D11SamplerState* shadowSamplerState = light != nullptr ? ShadowSamplerState.Get() : nullptr;
        context->PSSetSamplers(1, 1, &shadowSamplerState);
        ID3D11ShaderResourceView* pointShadowShaderResources[4] = { nullptr, nullptr, nullptr, nullptr };
        context->PSSetShaderResources(2, 4, pointShadowShaderResources);
        ID3D11SamplerState* pointShadowSamplerState = nullptr;
        context->PSSetSamplers(2, 1, &pointShadowSamplerState);
    }

    /// Finds the first visible shadow-enabled directional light affecting the current camera.
    DirectionalLightComponent* Win32RenderManager3D::FindPrimaryDirectionalShadowLight(const std::vector<LightComponent*>& lights) const {
        for (std::size_t lightIndex = 0; lightIndex < lights.size(); lightIndex++) {
            DirectionalLightComponent* light = dynamic_cast<DirectionalLightComponent*>(lights[lightIndex]);
            if (light == nullptr || light->get_Parent() == nullptr) {
                continue;
            } else if (!light->get_ShadowsEnabled()) {
                continue;
            } else if (!light->get_Parent()->get_IsHierarchyEnabled()) {
                continue;
            }

            return light;
        }

        return nullptr;
    }

    /// Renders the current scene depth from the active directional light into the shared shadow map.
    void Win32RenderManager3D::RenderDirectionalShadowMap(ICamera* camera, DirectionalLightComponent* light) {
        if (camera == nullptr) {
            throw new ArgumentNullException("camera");
        } else if (light == nullptr) {
            throw new ArgumentNullException("light");
        }

        EnsureShadowPipelineState();

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        ID3D11ShaderResourceView* nullShaderResourceView = nullptr;
        context->PSSetShaderResources(1, 1, &nullShaderResourceView);

        ID3D11DepthStencilView* depthStencilView = ShadowMapDepthStencilView.Get();
        context->OMSetRenderTargets(0, nullptr, depthStencilView);
        context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        D3D11_VIEWPORT viewport {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(DirectionalShadowMapResolution);
        viewport.Height = static_cast<float>(DirectionalShadowMapResolution);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        context->RSSetState(ShadowRasterizerState.Get());
        context->OMSetDepthStencilState(ShadowDepthStencilState.Get(), 0);
        context->IASetInputLayout(ShadowInputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(ShadowVertexShader.Get(), nullptr, 0);
        context->PSSetShader(ShadowPixelShader.Get(), nullptr, 0);
        ID3D11Buffer* transformBuffer = ShadowTransformBuffer.Get();
        context->VSSetConstantBuffers(0, 1, &transformBuffer);

        CurrentShadowViewProjection = BuildDirectionalShadowViewProjection(camera, light);
        IsShadowPassActive = true;
        try {
            IRenderQueue3D* renderQueue = camera->get_RenderQueue3D();
            if (renderQueue != nullptr) {
                renderQueue->VisitOrdered(this);
            }
        } catch (...) {
            IsShadowPassActive = false;
            throw;
        }

        IsShadowPassActive = false;
    }

    /// Draws one shadow-casting mesh into the active directional shadow map.
    void Win32RenderManager3D::DrawDirectionalShadowCaster(IDrawable3D* drawable, ::float4x4& lightViewProjection) {
        if (drawable == nullptr || drawable->get_Parent() == nullptr || !drawable->get_Parent()->get_IsHierarchyEnabled()) {
            return;
        }

        Array<RuntimeMaterial*>* runtimeMaterials = drawable->get_Materials();
        RuntimeMaterial* runtimeMaterial = runtimeMaterials != nullptr && runtimeMaterials->Length > 0
            ? (*runtimeMaterials)[0]
            : nullptr;
        if (!ShouldMaterialCastShadows(runtimeMaterial)) {
            return;
        }

        RuntimeModel* modelBase = drawable->get_Model();
        if (modelBase == nullptr) {
            return;
        }

        if (String::Equals(modelBase->get_Id(), BuiltInPlaneModelId, StringComparison::Ordinal)) {
            return;
        }

        auto* model = static_cast<Win32RuntimeModel*>(modelBase);
        if (!model->VertexBuffer) {
            return;
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        const UINT stride = sizeof(Win32VertexPositionNormalUV);
        const UINT offset = 0;
        ID3D11Buffer* vertexBuffer = model->VertexBuffer.Get();
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        if (model->IndexBuffer && model->IndexCount > 0) {
            context->IASetIndexBuffer(model->IndexBuffer.Get(), model->IndexFormat, 0);
        }

        Entity* parent = drawable->get_Parent();
        float4 orientation = parent->get_Orientation();
        float3 scale = parent->get_Scale();
        float3 position = parent->get_Position();

        ::float4x4 world = parent->get_WorldTransformMatrix();
        ::float4x4 worldViewProjection;
        float4x4::Multiply__ref0_ref1_out2(world, lightViewProjection, worldViewProjection);
        ::float4x4 transposedWorldViewProjection;
        float4x4::Transpose__ref0_out1(worldViewProjection, transposedWorldViewProjection);

        Win32ShadowTransformConstants constants {};
        constants.WorldViewProjection = StoreMatrix(transposedWorldViewProjection);
        context->UpdateSubresource(ShadowTransformBuffer.Get(), 0, nullptr, &constants, 0, 0);

        if (model->IndexBuffer && model->IndexCount > 0) {
            context->DrawIndexed(model->IndexCount, 0, 0);
        } else {
            context->Draw(model->VertexCount, 0);
        }
    }

    /// Returns whether one runtime material should contribute geometry to the directional shadow pass.
    bool Win32RenderManager3D::ShouldMaterialCastShadows(RuntimeMaterial* material) const {
        if (material == nullptr) {
            return true;
        }

        RuntimeMaterial* rootMaterial = material->ResolveRootMaterial();
        if (rootMaterial == nullptr) {
            throw new InvalidOperationException("Runtime materials must resolve to a root material before shadow rendering.");
        }

        return rootMaterial->get_CastsShadows();
    }

    /// Builds the light-space view-projection matrix used by the active directional shadow pass.
    ::float4x4 Win32RenderManager3D::BuildDirectionalShadowViewProjection(ICamera* camera, DirectionalLightComponent* light) const {
        if (camera == nullptr) {
            throw new ArgumentNullException("camera");
        } else if (light == nullptr) {
            throw new ArgumentNullException("light");
        } else if (camera->get_Parent() == nullptr) {
            throw new InvalidOperationException("Directional shadow rendering requires the camera to be attached to an entity.");
        } else if (light->get_Parent() == nullptr) {
            throw new InvalidOperationException("Directional shadow rendering requires the light to be attached to an entity.");
        }

        float3 rotatedForward = LightDirectionUtility::GetEntityForwardDirection(light->get_Parent());
        float3 lightDirection = float3::Normalize(float3(-rotatedForward.X, -rotatedForward.Y, -rotatedForward.Z));
        float shadowDistance = std::max(light->get_ShadowDistance(), MinimumDirectionalShadowDistance);
        float3 cameraForward = float4::RotateVector(float3(0.0f, 0.0f, -1.0f), camera->get_Parent()->get_Orientation());
        float3 target = camera->get_Parent()->get_Position() + (cameraForward * (shadowDistance * DirectionalShadowFocusDistanceFactor));
        float depthRange = shadowDistance * 2.0f;
        float3 lightPosition = target + (lightDirection * shadowDistance);
        float3 defaultUp(0.0f, 1.0f, 0.0f);
        float3 fallbackUp(0.0f, 0.0f, 1.0f);
        float3 up = std::abs(float3::Dot(lightDirection, defaultUp)) > 0.99f ? fallbackUp : defaultUp;

        ::float4x4 view;
        float4x4::CreateLookAt__ref0_ref1_ref2_out3(lightPosition, target, up, view);
        float halfDistance = shadowDistance * 0.5f;
        ::float4x4 projection;
        float4x4::CreateOrthographicOffCenter__out6(-halfDistance, halfDistance, -halfDistance, halfDistance, 0.1f, depthRange, projection);
        ::float4x4 worldToShadowClip;
        float4x4::Multiply__ref0_ref1_out2(view, projection, worldToShadowClip);
        return worldToShadowClip;
    }

    /// Draws one clip-space triangle with an identity transform to validate the native draw pipeline.
    void Win32RenderManager3D::DrawDebugTriangle() {
        EnsureDebugTriangleBuffer();

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        const UINT stride = sizeof(Win32VertexPositionNormalUV);
        const UINT offset = 0;
        ID3D11Buffer* vertexBuffer = DebugTriangleBuffer.Get();
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

        ::float4x4 identity = float4x4::get_Identity();
        ::float4x4 transposedIdentity;
        float4x4::Transpose__ref0_out1(identity, transposedIdentity);

        Win32TransformConstants constants {};
        constants.WorldViewProjection = StoreMatrix(transposedIdentity);
        context->UpdateSubresource(TransformBuffer.Get(), 0, nullptr, &constants, 0, 0);

        context->IASetInputLayout(InputLayout.Get());
        context->VSSetShader(VertexShader.Get(), nullptr, 0);
        context->PSSetShader(PixelShader.Get(), nullptr, 0);
        context->Draw(3, 0);
    }

    /// Builds a shader resource from one packaged shader asset and material program selection.
    Win32ShaderResource Win32RenderManager3D::BuildShaderResource(ShaderMaterialAsset* materialAsset, ShaderAsset* shaderAsset) {
        if (materialAsset == nullptr) {
            throw new ArgumentNullException("materialAsset");
        }

        if (shaderAsset == nullptr) {
            throw new ArgumentNullException("shaderAsset");
        }

        if (String::IsNullOrWhiteSpace(materialAsset->VertexProgram)) {
            throw new InvalidOperationException("Material assets must define a vertex program name.");
        }

        if (String::IsNullOrWhiteSpace(materialAsset->PixelProgram)) {
            throw new InvalidOperationException("Material assets must define a pixel program name.");
        }

        if (String::IsNullOrWhiteSpace(materialAsset->Variant)) {
            throw new InvalidOperationException("Material assets must define a shader variant.");
        }

        ShaderBinaryAsset* vertexBinary = GetShaderBinary(shaderAsset, materialAsset->VertexProgram, ShaderStage::Vertex, materialAsset->Variant);
        ShaderBinaryAsset* pixelBinary = GetShaderBinary(shaderAsset, materialAsset->PixelProgram, ShaderStage::Pixel, materialAsset->Variant);
        if (vertexBinary == nullptr || pixelBinary == nullptr) {
            throw new InvalidOperationException("Material shader binaries could not be resolved.");
        }

        std::vector<std::string> semanticStorage;
        std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements = BuildInputElements(shaderAsset, materialAsset->VertexProgram, materialAsset->Variant, semanticStorage);
        if (inputElements.empty()) {
            inputElements = BuildDefaultInputElements();
        }

        Win32ShaderResource shaderResource {};
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateVertexShader(vertexBinary->Bytecode->Data, static_cast<SIZE_T>(vertexBinary->Bytecode->Length), nullptr, shaderResource.VertexShader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader failed for a packaged material shader.");
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreatePixelShader(pixelBinary->Bytecode->Data, static_cast<SIZE_T>(pixelBinary->Bytecode->Length), nullptr, shaderResource.PixelShader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader failed for a packaged material shader.");
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateInputLayout(
                inputElements.data(),
                static_cast<UINT>(inputElements.size()),
                vertexBinary->Bytecode->Data,
                static_cast<SIZE_T>(vertexBinary->Bytecode->Length),
                shaderResource.InputLayout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout failed for a packaged material shader.");
        return shaderResource;
    }

    /// Builds Direct3D input elements from the vertex signature exposed by a shader program.
    std::vector<D3D11_INPUT_ELEMENT_DESC> Win32RenderManager3D::BuildInputElements(
        ShaderAsset* shaderAsset,
        std::string vertexProgram,
        std::string variant,
        std::vector<std::string>& semanticStorage) {
        if (shaderAsset == nullptr) {
            throw new ArgumentNullException("shaderAsset");
        }

        if (String::IsNullOrWhiteSpace(vertexProgram)) {
            throw new InvalidOperationException("Vertex program name must be provided.");
        }

        if (String::IsNullOrWhiteSpace(variant)) {
            throw new InvalidOperationException("Shader variant name must be provided.");
        }

        (void)variant;

        ShaderProgramAsset* vertexProgramAsset = nullptr;
        if (shaderAsset->Programs != nullptr) {
            for (int32_t programIndex = 0; programIndex < shaderAsset->Programs->Length; programIndex++) {
                ShaderProgramAsset* candidate = (*shaderAsset->Programs)[programIndex];
                if (candidate == nullptr) {
                    continue;
                }

                if (candidate->Stage != ShaderStage::Vertex) {
                    continue;
                }

                if (!String::Equals(candidate->Name, vertexProgram, StringComparison::Ordinal)) {
                    continue;
                }

                vertexProgramAsset = candidate;
                break;
            }
        }

        if (vertexProgramAsset == nullptr || vertexProgramAsset->Inputs == nullptr || vertexProgramAsset->Inputs->Length == 0) {
            return BuildDefaultInputElements();
        }

        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        elements.reserve(static_cast<std::size_t>(vertexProgramAsset->Inputs->Length));
        semanticStorage.reserve(static_cast<std::size_t>(vertexProgramAsset->Inputs->Length));

        UINT byteOffset = 0;
        for (int32_t inputIndex = 0; inputIndex < vertexProgramAsset->Inputs->Length; inputIndex++) {
            ShaderVertexElementAsset* element = (*vertexProgramAsset->Inputs)[inputIndex];
            if (element == nullptr) {
                throw new InvalidOperationException("Shader program input elements contain a null entry.");
            }

            semanticStorage.push_back(element->Semantic);
            D3D11_INPUT_ELEMENT_DESC inputElement {};
            inputElement.SemanticName = semanticStorage.back().c_str();
            inputElement.SemanticIndex = static_cast<UINT>(element->Index);
            inputElement.Format = ResolveVertexElementFormat(element->Format);
            inputElement.InputSlot = 0;
            inputElement.AlignedByteOffset = byteOffset;
            inputElement.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            inputElement.InstanceDataStepRate = 0;
            elements.push_back(inputElement);

            switch (inputElement.Format) {
                case DXGI_FORMAT_R32_FLOAT:
                    byteOffset += 4;
                    break;
                case DXGI_FORMAT_R32G32_FLOAT:
                    byteOffset += 8;
                    break;
                case DXGI_FORMAT_R32G32B32_FLOAT:
                    byteOffset += 12;
                    break;
                case DXGI_FORMAT_R32G32B32A32_FLOAT:
                    byteOffset += 16;
                    break;
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                    byteOffset += 4;
                    break;
                default:
                    throw new InvalidOperationException("Unsupported vertex element format.");
            }
        }

        return elements;
    }

    /// Resolves one shader binary for the requested shader program, stage, and variant.
    ShaderBinaryAsset* Win32RenderManager3D::GetShaderBinary(ShaderAsset* shaderAsset, std::string programName, ShaderStage stage, std::string variant) {
        if (shaderAsset == nullptr) {
            throw new ArgumentNullException("shaderAsset");
        }

        if (shaderAsset->Binaries == nullptr) {
            throw new InvalidOperationException("Shader assets must include compiled binaries.");
        }

        for (int32_t binaryIndex = 0; binaryIndex < shaderAsset->Binaries->Length; binaryIndex++) {
            ShaderBinaryAsset* binary = (*shaderAsset->Binaries)[binaryIndex];
            if (binary == nullptr) {
                continue;
            }

            if (!String::Equals(binary->TargetName, shaderAsset->TargetName, StringComparison::OrdinalIgnoreCase)) {
                continue;
            }

            if (!String::Equals(binary->ProgramName, programName, StringComparison::Ordinal)) {
                continue;
            }

            if (binary->Stage != stage) {
                continue;
            }

            if (!String::Equals(binary->Variant, variant, StringComparison::Ordinal)) {
                continue;
            }

            if (binary->Bytecode == nullptr || binary->Bytecode->Length == 0) {
                throw new InvalidOperationException("Shader binary does not include bytecode.");
            }

            return binary;
        }

        throw new InvalidOperationException("Shader binary was not found for the requested program.");
    }

    /// Resolves the DirectX input format for one shader vertex element format string.
    DXGI_FORMAT Win32RenderManager3D::ResolveVertexElementFormat(std::string format) const {
        if (String::Equals(format, "float", StringComparison::OrdinalIgnoreCase) ||
            String::Equals(format, "float1", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32_FLOAT;
        }

        if (String::Equals(format, "float2", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32_FLOAT;
        }

        if (String::Equals(format, "float3", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32B32_FLOAT;
        }

        if (String::Equals(format, "float4", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }

        if (String::Equals(format, "uint4", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32B32A32_UINT;
        }

        if (String::Equals(format, "int4", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32B32A32_SINT;
        }

        throw new InvalidOperationException(std::string("Unsupported shader vertex element format: ") + format);
    }

    /// Applies the shader and resource bindings for one runtime material.
    void Win32RenderManager3D::ApplyMaterial(RuntimeMaterial* material) {
        if (material == nullptr) {
            throw new ArgumentNullException("material");
        }

        RuntimeMaterial* rootMaterial = material->ResolveRootMaterial();
        if (rootMaterial == nullptr) {
            throw new InvalidOperationException("Runtime materials must resolve to a root material.");
        }

        std::string materialId = rootMaterial->get_Id();
        if (materialId.empty()) {
            throw new InvalidOperationException("Runtime materials must have an identifier before rendering.");
        }

        if (!HasWrittenRenderSnapshot) {
            AppendRenderDiagnosticsLine("3d.material_id=" + materialId);
        }

        auto shaderResourceIt = MaterialShaderResources.find(materialId);
        if (shaderResourceIt == MaterialShaderResources.end() || shaderResourceIt->second == nullptr) {
            throw new InvalidOperationException(std::string("No shader resource was cached for runtime material '") + materialId + std::string("'."));
        }

        if (!HasWrittenRenderSnapshot) {
            AppendRenderDiagnosticsLine("3d.material_path=packaged_shader");
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        Win32ShaderResource* shaderResource = shaderResourceIt->second.get();
        context->IASetInputLayout(shaderResource->InputLayout.Get());
        context->VSSetShader(shaderResource->VertexShader.Get(), nullptr, 0);
        context->PSSetShader(shaderResource->PixelShader.Get(), nullptr, 0);

        ID3D11Buffer* transformBuffer = TransformBuffer.Get();
        context->VSSetConstantBuffers(0, 1, &transformBuffer);
        context->PSSetConstantBuffers(0, 1, &transformBuffer);
        BindMaterialConstantBuffers(material);

        ShaderRuntimeMaterial* shaderRuntimeMaterial = RequireShaderRuntimeMaterial(material);
        MaterialLayout* layout = shaderRuntimeMaterial->get_Layout();
        MaterialPropertyBlock* properties = shaderRuntimeMaterial->get_Properties();
        if (!HasWrittenRenderSnapshot) {
            const int32_t textureBindingCount = layout != nullptr && layout->get_TextureBindings() != nullptr
                ? layout->get_TextureBindings()->Length
                : 0;
            const int32_t constantBufferBindingCount = layout != nullptr && layout->get_ConstantBufferBindings() != nullptr
                ? layout->get_ConstantBufferBindings()->Length
                : 0;
            AppendRenderDiagnosticsLine(
                "3d.material_layout material_id=" + materialId
                + " texture_bindings=" + std::to_string(textureBindingCount)
                + " constant_buffer_bindings=" + std::to_string(constantBufferBindingCount)
                + " has_properties=" + std::to_string(properties != nullptr ? 1 : 0));
        }
        if (layout != nullptr && properties != nullptr) {
            Array<MaterialLayoutBinding*>* textureBindings = layout->get_TextureBindings();
            if (textureBindings != nullptr && textureBindings->Length > 0) {
                BindMaterialTexture(material);
            } else {
                ID3D11ShaderResourceView* nullResourceView = nullptr;
                context->PSSetShaderResources(0, 1, &nullResourceView);
                ID3D11SamplerState* nullSampler = nullptr;
                context->PSSetSamplers(0, 1, &nullSampler);
            }
        }
    }

    /// Applies authored material constant-buffer payloads for one runtime material while leaving engine-managed buffers untouched.
    void Win32RenderManager3D::BindMaterialConstantBuffers(RuntimeMaterial* material) {
        if (material == nullptr) {
            throw new ArgumentNullException("material");
        }

        ShaderRuntimeMaterial* shaderRuntimeMaterial = RequireShaderRuntimeMaterial(material);
        MaterialLayout* layout = shaderRuntimeMaterial->get_Layout();
        if (layout == nullptr) {
            return;
        }

        Array<MaterialLayoutBinding*>* constantBufferBindings = layout->get_ConstantBufferBindings();
        if (constantBufferBindings == nullptr) {
            return;
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        for (int32_t bindingIndex = 0; bindingIndex < constantBufferBindings->Length; bindingIndex++) {
            MaterialLayoutBinding* binding = (*constantBufferBindings)[bindingIndex];
            if (binding == nullptr) {
                continue;
            }

            std::string bindingName = binding->get_Name();
            if (IsEngineManagedConstantBufferBinding(bindingName)) {
                if (!HasWrittenRenderSnapshot) {
                    AppendRenderDiagnosticsLine(
                        "3d.material_cb skip_engine_managed name=" + bindingName
                        + " slot=" + std::to_string(binding->get_Slot()));
                }
                continue;
            }

            Array<uint8_t>* data = nullptr;
            if (!shaderRuntimeMaterial->TryResolveConstantBufferData__out1(bindingName, data) || data == nullptr || data->Length == 0) {
                if (!HasWrittenRenderSnapshot) {
                    AppendRenderDiagnosticsLine(
                        "3d.material_cb missing name=" + bindingName
                        + " slot=" + std::to_string(binding->get_Slot()));
                }
                ID3D11Buffer* nullBuffer = nullptr;
                context->VSSetConstantBuffers(static_cast<UINT>(binding->get_Slot()), 1, &nullBuffer);
                context->PSSetConstantBuffers(static_cast<UINT>(binding->get_Slot()), 1, &nullBuffer);
                continue;
            }

            int32_t dataLength = data->Length;
            if (!HasWrittenRenderSnapshot) {
                AppendRenderDiagnosticsLine(
                    "3d.material_cb bind name=" + bindingName
                    + " slot=" + std::to_string(binding->get_Slot())
                    + " size=" + std::to_string(dataLength));
            }
            ID3D11Buffer* constantBuffer = GetOrCreateMaterialConstantBuffer(binding->get_Slot(), dataLength);
            context->UpdateSubresource(constantBuffer, 0, nullptr, data->Data, 0, 0);
            delete data;
            context->VSSetConstantBuffers(static_cast<UINT>(binding->get_Slot()), 1, &constantBuffer);
            context->PSSetConstantBuffers(static_cast<UINT>(binding->get_Slot()), 1, &constantBuffer);
        }
    }

    /// Binds the resolved runtime material texture to the pixel shader slot consumed by the Windows forward path.
    void Win32RenderManager3D::BindMaterialTexture(RuntimeMaterial* material) {
        if (material == nullptr) {
            throw new ArgumentNullException("material");
        }

        ShaderRuntimeMaterial* shaderRuntimeMaterial = RequireShaderRuntimeMaterial(material);
        RuntimeTexture* texture = shaderRuntimeMaterial->ResolveTexture();
#if __has_include("StandardMaterialTextureBindingDefaults.hpp")
        if (texture == nullptr) {
            MaterialLayout* layout = shaderRuntimeMaterial->get_Layout();
            if (layout != nullptr) {
                int32_t diffuseTextureBindingIndex = layout->FindTextureBindingIndex(StandardMaterialTextureBindingDefaults::DiffuseTextureBindingName);
                if (diffuseTextureBindingIndex >= 0) {
                    texture = TextureUtils::get_PixelTexture();
                }
            }
        }
#endif
        ID3D11ShaderResourceView* resourceView = ResolveTextureResourceView(texture);
        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        context->PSSetShaderResources(0, 1, &resourceView);
        if (resourceView != nullptr) {
            ID3D11SamplerState* samplerState = TextureSamplerState.Get();
            context->PSSetSamplers(0, 1, &samplerState);
        } else {
            ID3D11SamplerState* nullSampler = nullptr;
            context->PSSetSamplers(0, 1, &nullSampler);
        }
    }

    /// Resolves an uploaded shader resource view for one runtime texture.
    ID3D11ShaderResourceView* Win32RenderManager3D::ResolveTextureResourceView(RuntimeTexture* texture) const {
        if (texture == nullptr) {
            return nullptr;
        }

        const std::string& textureId = texture->get_Id();
        if (textureId.empty()) {
            return nullptr;
        }

        auto resource = TextureResources.find(textureId);
        if (resource == TextureResources.end() || resource->second == nullptr) {
            return nullptr;
        }

        return resource->second->ShaderResourceView.Get();
    }

    /// Resolves or creates one Direct3D constant buffer matching the requested shader slot and byte size.
    ID3D11Buffer* Win32RenderManager3D::GetOrCreateMaterialConstantBuffer(int32_t slot, int32_t sizeInBytes) {
        if (slot < 0) {
            throw new ArgumentOutOfRangeException("slot", "Constant-buffer slot cannot be negative.");
        }
        if (sizeInBytes <= 0) {
            throw new ArgumentOutOfRangeException("sizeInBytes", "Constant-buffer size must be positive.");
        }

        uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(slot)) << 32) | static_cast<uint32_t>(sizeInBytes);
        auto existing = MaterialConstantBuffers.find(key);
        if (existing != MaterialConstantBuffers.end() && existing->second) {
            return existing->second.Get();
        }

        D3D11_BUFFER_DESC description {};
        description.ByteWidth = static_cast<UINT>(sizeInBytes);
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        description.CPUAccessFlags = 0;
        description.MiscFlags = 0;
        description.StructureByteStride = 0;

        Microsoft::WRL::ComPtr<ID3D11Buffer> constantBuffer;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateBuffer(&description, nullptr, constantBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for a Windows material constant buffer.");
        MaterialConstantBuffers[key] = constantBuffer;
        LiveMaterialConstantBufferBytes += static_cast<std::size_t>(description.ByteWidth);
        RuntimeRenderDiagnostics::WriteHostEvent(
            "material-constant-buffer",
            "slot=" + std::to_string(slot)
                + " size_in_bytes=" + std::to_string(sizeInBytes)
                + " live_material_constant_buffers=" + std::to_string(MaterialConstantBuffers.size())
                + " live_material_constant_buffer_bytes=" + std::to_string(LiveMaterialConstantBufferBytes));
        return MaterialConstantBuffers[key].Get();
    }

    /// Returns whether one constant-buffer binding is owned by the renderer instead of material-authored property data.
    bool Win32RenderManager3D::IsEngineManagedConstantBufferBinding(std::string bindingName) {
        return bindingName == "TransformBuffer"
            || bindingName == "ForwardLightBuffer"
            || bindingName == "ShadowBuffer";
    }

    /// Clears the back buffer to a solid fallback color when nothing else renders.
    void Win32RenderManager3D::ClearBackBuffer(float red, float green, float blue, float alpha) {
        ID3D11RenderTargetView* renderTargetView = Bootstrap.GetRenderTargetView();
        ID3D11DepthStencilView* depthStencilView = Bootstrap.GetDepthStencilView();
        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();

        const float clearColor[] = { red, green, blue, alpha };
        context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
        context->ClearRenderTargetView(renderTargetView, clearColor);
        if (depthStencilView != nullptr) {
            context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        }

        D3D11_VIEWPORT viewport {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(Bootstrap.GetWidth());
        viewport.Height = static_cast<float>(Bootstrap.GetHeight());
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
    }

    /// Creates the native 2D bridge for one DirectX11 bootstrap.
    Win32RenderManager2D::Win32RenderManager2D(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap) {
#if __has_include("RenderCommandListBuilder2D.hpp")
        CommandListBuilder = std::make_unique<RenderCommandListBuilder2D>();
#endif
    }

    /// Creates the DirectX11 shaders, buffers, and fixed pipeline state needed for 2D rendering.
    void Win32RenderManager2D::EnsurePipelineState() {
        if (QuadVertexBuffer
            && QuadInputLayout
            && QuadVertexShader
            && QuadPixelShader
            && RoundedRectVertexShader
            && RoundedRectPixelShader
            && RoundedRectConstantBuffer
            && TextureSamplerState
            && AlphaBlendState
            && RasterizerState
            && DepthStencilState
            && WhiteShaderResourceView) {
            return;
        }

        ID3D11Device* device = Bootstrap.GetDevice();
        if (device == nullptr) {
            throw std::runtime_error("DirectX11 device must exist before initializing the native 2D bridge.");
        }

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBytecode;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBytecode;
        Microsoft::WRL::ComPtr<ID3DBlob> compileErrors;

        ThrowIfFailed(
            D3DCompile(
                QuadVertexShaderSource,
                std::strlen(QuadVertexShaderSource),
                nullptr,
                nullptr,
                nullptr,
                "VSMain",
                "vs_4_0",
                0,
                0,
                vertexShaderBytecode.GetAddressOf(),
                compileErrors.GetAddressOf()),
            "D3DCompile failed for the Windows 2D vertex shader.");

        compileErrors.Reset();
        ThrowIfFailed(
            D3DCompile(
                QuadPixelShaderSource,
                std::strlen(QuadPixelShaderSource),
                nullptr,
                nullptr,
                nullptr,
                "PSMain",
                "ps_4_0",
                0,
                0,
                pixelShaderBytecode.GetAddressOf(),
                compileErrors.GetAddressOf()),
            "D3DCompile failed for the Windows 2D pixel shader.");

        ThrowIfFailed(
            device->CreateVertexShader(
                vertexShaderBytecode->GetBufferPointer(),
                vertexShaderBytecode->GetBufferSize(),
                nullptr,
                QuadVertexShader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader failed for the Windows 2D vertex shader.");

        ThrowIfFailed(
            device->CreatePixelShader(
                pixelShaderBytecode->GetBufferPointer(),
                pixelShaderBytecode->GetBufferSize(),
                nullptr,
                QuadPixelShader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader failed for the Windows 2D pixel shader.");

        Microsoft::WRL::ComPtr<ID3DBlob> roundedRectVertexShaderBytecode;
        Microsoft::WRL::ComPtr<ID3DBlob> roundedRectPixelShaderBytecode;
        compileErrors.Reset();
        ThrowIfFailed(
            D3DCompile(
                RoundedRectVertexShaderSource,
                std::strlen(RoundedRectVertexShaderSource),
                nullptr,
                nullptr,
                nullptr,
                "VSMain",
                "vs_4_0",
                0,
                0,
                roundedRectVertexShaderBytecode.GetAddressOf(),
                compileErrors.GetAddressOf()),
            "D3DCompile failed for the Windows rounded-rect vertex shader.");

        compileErrors.Reset();
        ThrowIfFailed(
            D3DCompile(
                RoundedRectPixelShaderSource,
                std::strlen(RoundedRectPixelShaderSource),
                nullptr,
                nullptr,
                nullptr,
                "PSMain",
                "ps_4_0",
                0,
                0,
                roundedRectPixelShaderBytecode.GetAddressOf(),
                compileErrors.GetAddressOf()),
            "D3DCompile failed for the Windows rounded-rect pixel shader.");

        ThrowIfFailed(
            device->CreateVertexShader(
                roundedRectVertexShaderBytecode->GetBufferPointer(),
                roundedRectVertexShaderBytecode->GetBufferSize(),
                nullptr,
                RoundedRectVertexShader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader failed for the Windows rounded-rect vertex shader.");

        ThrowIfFailed(
            device->CreatePixelShader(
                roundedRectPixelShaderBytecode->GetBufferPointer(),
                roundedRectPixelShaderBytecode->GetBufferSize(),
                nullptr,
                RoundedRectPixelShader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader failed for the Windows rounded-rect pixel shader.");

        const D3D11_INPUT_ELEMENT_DESC inputElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        ThrowIfFailed(
            device->CreateInputLayout(
                inputElements,
                static_cast<UINT>(std::size(inputElements)),
                vertexShaderBytecode->GetBufferPointer(),
                vertexShaderBytecode->GetBufferSize(),
                QuadInputLayout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout failed for the Windows 2D quad pipeline.");

        D3D11_BUFFER_DESC vertexBufferDescription {};
        vertexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(Win32QuadVertex) * QuadVertexBufferVertexCapacity);
        vertexBufferDescription.Usage = D3D11_USAGE_DYNAMIC;
        vertexBufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vertexBufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ThrowIfFailed(
            device->CreateBuffer(&vertexBufferDescription, nullptr, QuadVertexBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for the Windows 2D quad vertex buffer.");
        QuadVertexCapacity = QuadVertexBufferVertexCapacity;
        QuadVertexCursor = 0;

        D3D11_BUFFER_DESC roundedRectConstantBufferDescription {};
        roundedRectConstantBufferDescription.ByteWidth = static_cast<UINT>(sizeof(Win32RoundedRectShaderConstants));
        roundedRectConstantBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        roundedRectConstantBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        ThrowIfFailed(
            device->CreateBuffer(&roundedRectConstantBufferDescription, nullptr, RoundedRectConstantBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for the Windows rounded-rect constant buffer.");

        D3D11_SAMPLER_DESC samplerDescription {};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDescription.MinLOD = 0.0f;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;

        ThrowIfFailed(
            device->CreateSamplerState(&samplerDescription, TextureSamplerState.GetAddressOf()),
            "ID3D11Device::CreateSamplerState failed for the Windows 2D sampler state.");

        D3D11_BLEND_DESC blendDescription {};
        blendDescription.RenderTarget[0].BlendEnable = TRUE;
        blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDescription.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        ThrowIfFailed(
            device->CreateBlendState(&blendDescription, AlphaBlendState.GetAddressOf()),
            "ID3D11Device::CreateBlendState failed for the Windows 2D alpha blend state.");

        D3D11_RASTERIZER_DESC rasterizerDescription {};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = FALSE;
        rasterizerDescription.ScissorEnable = TRUE;

        ThrowIfFailed(
            device->CreateRasterizerState(&rasterizerDescription, RasterizerState.GetAddressOf()),
            "ID3D11Device::CreateRasterizerState failed for the Windows 2D rasterizer state.");

        D3D11_DEPTH_STENCIL_DESC depthStencilDescription {};
        depthStencilDescription.DepthEnable = FALSE;
        depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthStencilDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;

        ThrowIfFailed(
            device->CreateDepthStencilState(&depthStencilDescription, DepthStencilState.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilState failed for the Windows 2D depth state.");

        const uint32_t whitePixel = 0xFFFFFFFFu;
        D3D11_TEXTURE2D_DESC whiteTextureDescription {};
        whiteTextureDescription.Width = 1;
        whiteTextureDescription.Height = 1;
        whiteTextureDescription.MipLevels = 1;
        whiteTextureDescription.ArraySize = 1;
        whiteTextureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        whiteTextureDescription.SampleDesc.Count = 1;
        whiteTextureDescription.Usage = D3D11_USAGE_DEFAULT;
        whiteTextureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA whiteTextureData {};
        whiteTextureData.pSysMem = &whitePixel;
        whiteTextureData.SysMemPitch = sizeof(uint32_t);

        ThrowIfFailed(
            device->CreateTexture2D(&whiteTextureDescription, &whiteTextureData, WhiteTexture.GetAddressOf()),
            "ID3D11Device::CreateTexture2D failed for the Windows 2D white texture.");

        D3D11_SHADER_RESOURCE_VIEW_DESC whiteResourceViewDescription {};
        whiteResourceViewDescription.Format = whiteTextureDescription.Format;
        whiteResourceViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        whiteResourceViewDescription.Texture2D.MostDetailedMip = 0;
        whiteResourceViewDescription.Texture2D.MipLevels = 1;

        ThrowIfFailed(
            device->CreateShaderResourceView(WhiteTexture.Get(), &whiteResourceViewDescription, WhiteShaderResourceView.GetAddressOf()),
            "ID3D11Device::CreateShaderResourceView failed for the Windows 2D white texture.");
    }

    /// Resolves one camera viewport against the current swap-chain size.
    D3D11_VIEWPORT Win32RenderManager2D::ResolveViewport(ICamera* camera) const {
        if (camera == nullptr) {
            throw new ArgumentNullException("camera");
        }

        const float4 rawViewport = camera->get_Viewport();
        float offsetX = rawViewport.X;
        float offsetY = rawViewport.Y;
        float width = rawViewport.Z;
        float height = rawViewport.W;

        if (width <= 1.0f && height <= 1.0f && width > 0.0f && height > 0.0f) {
            offsetX *= static_cast<float>(Bootstrap.GetWidth());
            offsetY *= static_cast<float>(Bootstrap.GetHeight());
            width *= static_cast<float>(Bootstrap.GetWidth());
            height *= static_cast<float>(Bootstrap.GetHeight());
        }

        if (width <= 0.0f || height <= 0.0f) {
            offsetX = 0.0f;
            offsetY = 0.0f;
            width = static_cast<float>(Bootstrap.GetWidth());
            height = static_cast<float>(Bootstrap.GetHeight());
        }

        D3D11_VIEWPORT viewport {};
        viewport.TopLeftX = offsetX;
        viewport.TopLeftY = offsetY;
        viewport.Width = width;
        viewport.Height = height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        return viewport;
    }

    /// Resolves an uploaded shader resource view for one runtime texture.
    ID3D11ShaderResourceView* Win32RenderManager2D::ResolveTextureResourceView(RuntimeTexture* texture) const {
        if (texture == nullptr) {
            return nullptr;
        }

        const std::string& textureId = texture->get_Id();
        if (textureId.empty()) {
            return nullptr;
        }

        auto resource = TextureResources.find(textureId);
        if (resource == TextureResources.end() || resource->second == nullptr) {
            return nullptr;
        }

        return resource->second->ShaderResourceView.Get();
    }

    /// Configures the DirectX11 state used by one textured quad draw.
    void Win32RenderManager2D::PrepareTexturedQuadDraw(ID3D11ShaderResourceView* textureView) {
        EnsurePipelineState();

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        ID3D11RenderTargetView* renderTargetView = Bootstrap.GetRenderTargetView();
        ID3D11DepthStencilView* depthStencilView = Bootstrap.GetDepthStencilView();
        context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
        context->RSSetViewports(1, &CurrentViewport);
        context->RSSetState(RasterizerState.Get());
        ApplyScissorRect();
        context->OMSetDepthStencilState(DepthStencilState.Get(), 0);

        const float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        context->OMSetBlendState(AlphaBlendState.Get(), blendFactor, 0xFFFFFFFFu);
        context->IASetInputLayout(QuadInputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        const UINT stride = sizeof(Win32QuadVertex);
        const UINT offset = 0;
        ID3D11Buffer* quadVertexBuffer = QuadVertexBuffer.Get();
        context->IASetVertexBuffers(0, 1, &quadVertexBuffer, &stride, &offset);
        context->VSSetShader(QuadVertexShader.Get(), nullptr, 0);
        context->PSSetShader(QuadPixelShader.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &textureView);
        ID3D11SamplerState* samplerState = TextureSamplerState.Get();
        context->PSSetSamplers(0, 1, &samplerState);
    }

    /// Configures the DirectX11 state used by one rounded-rect SDF draw.
    void Win32RenderManager2D::PrepareRoundedRectDraw() {
        EnsurePipelineState();

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        ID3D11RenderTargetView* renderTargetView = Bootstrap.GetRenderTargetView();
        ID3D11DepthStencilView* depthStencilView = Bootstrap.GetDepthStencilView();
        context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
        context->RSSetViewports(1, &CurrentViewport);
        context->RSSetState(RasterizerState.Get());
        ApplyScissorRect();
        context->OMSetDepthStencilState(DepthStencilState.Get(), 0);

        const float blendFactor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        context->OMSetBlendState(AlphaBlendState.Get(), blendFactor, 0xFFFFFFFFu);
        context->IASetInputLayout(QuadInputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        const UINT stride = sizeof(Win32QuadVertex);
        const UINT offset = 0;
        ID3D11Buffer* quadVertexBuffer = QuadVertexBuffer.Get();
        context->IASetVertexBuffers(0, 1, &quadVertexBuffer, &stride, &offset);
        context->VSSetShader(RoundedRectVertexShader.Get(), nullptr, 0);
        context->PSSetShader(RoundedRectPixelShader.Get(), nullptr, 0);
        ID3D11Buffer* roundedRectConstantBuffer = RoundedRectConstantBuffer.Get();
        context->VSSetConstantBuffers(0, 1, &roundedRectConstantBuffer);
        context->PSSetConstantBuffers(0, 1, &roundedRectConstantBuffer);
    }

    /// Applies the currently active scissor rectangle, or falls back to the full viewport when clipping is inactive.
    void Win32RenderManager2D::ApplyScissorRect() {
        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        if (context == nullptr) {
            return;
        }

        if (!HasActiveViewport) {
            return;
        }

        if (HasActiveClipRect) {
            context->RSSetScissorRects(1, &CurrentScissorRect);
            return;
        }

        const LONG viewportLeft = static_cast<LONG>(std::floor(CurrentViewport.TopLeftX));
        const LONG viewportTop = static_cast<LONG>(std::floor(CurrentViewport.TopLeftY));
        const LONG viewportRight = static_cast<LONG>(std::ceil(CurrentViewport.TopLeftX + CurrentViewport.Width));
        const LONG viewportBottom = static_cast<LONG>(std::ceil(CurrentViewport.TopLeftY + CurrentViewport.Height));
        D3D11_RECT viewportRect {
            viewportLeft,
            viewportTop,
            viewportRight,
            viewportBottom
        };
        context->RSSetScissorRects(1, &viewportRect);
    }

    /// Pushes one clip rectangle onto the active stack after converting it into the current viewport's scissor space.
    void Win32RenderManager2D::PushClipRect(float4 clipRect) {
        D3D11_RECT scissorRect = ResolveScissorRect(clipRect);
        ClipRectStack.push_back(scissorRect);
        CurrentScissorRect = scissorRect;
        HasActiveClipRect = true;
    }

    /// Pops the most recent clip rectangle and restores the previous scissor state.
    void Win32RenderManager2D::PopClipRect() {
        if (ClipRectStack.empty()) {
            HasActiveClipRect = false;
            return;
        }

        ClipRectStack.pop_back();
        if (ClipRectStack.empty()) {
            HasActiveClipRect = false;
            return;
        }

        CurrentScissorRect = ClipRectStack.back();
        HasActiveClipRect = true;
    }

    /// Resolves one raw clip rectangle into a viewport-clamped Direct3D scissor rectangle.
    D3D11_RECT Win32RenderManager2D::ResolveScissorRect(float4 clipRect) const {
        const LONG viewportLeft = static_cast<LONG>(std::floor(CurrentViewport.TopLeftX));
        const LONG viewportTop = static_cast<LONG>(std::floor(CurrentViewport.TopLeftY));
        const LONG viewportRight = static_cast<LONG>(std::ceil(CurrentViewport.TopLeftX + CurrentViewport.Width));
        const LONG viewportBottom = static_cast<LONG>(std::ceil(CurrentViewport.TopLeftY + CurrentViewport.Height));

        const LONG clipLeft = static_cast<LONG>(std::floor(clipRect.X));
        const LONG clipTop = static_cast<LONG>(std::floor(clipRect.Y));
        const LONG clipRight = static_cast<LONG>(std::ceil(clipRect.X + clipRect.Z));
        const LONG clipBottom = static_cast<LONG>(std::ceil(clipRect.Y + clipRect.W));

        return D3D11_RECT {
            std::max(viewportLeft, clipLeft),
            std::max(viewportTop, clipTop),
            std::min(viewportRight, clipRight),
            std::min(viewportBottom, clipBottom)
        };
    }

    /// Draws one textured quad in window-space pixel coordinates.
    void Win32RenderManager2D::DrawTexturedQuad(
        ID3D11ShaderResourceView* textureView,
        float x,
        float y,
        float width,
        float height,
        float4 sourceRect,
        byte4 color) {
        if (!HasActiveViewport || textureView == nullptr || width <= 0.0f || height <= 0.0f || CurrentViewport.Width <= 0.0f || CurrentViewport.Height <= 0.0f) {
            return;
        }

        PrepareTexturedQuadDraw(textureView);

        const float leftNdc = (((x - CurrentViewport.TopLeftX) / CurrentViewport.Width) * 2.0f) - 1.0f;
        const float rightNdc = ((((x + width) - CurrentViewport.TopLeftX) / CurrentViewport.Width) * 2.0f) - 1.0f;
        const float topNdc = 1.0f - (((y - CurrentViewport.TopLeftY) / CurrentViewport.Height) * 2.0f);
        const float bottomNdc = 1.0f - ((((y + height) - CurrentViewport.TopLeftY) / CurrentViewport.Height) * 2.0f);
        const DirectX::XMFLOAT4 tint = ConvertColor(color);

        std::array<Win32QuadVertex, 4> vertices = {
            Win32QuadVertex { DirectX::XMFLOAT3(leftNdc, bottomNdc, 0.0f), DirectX::XMFLOAT2(sourceRect.X, sourceRect.Y + sourceRect.W), tint },
            Win32QuadVertex { DirectX::XMFLOAT3(leftNdc, topNdc, 0.0f), DirectX::XMFLOAT2(sourceRect.X, sourceRect.Y), tint },
            Win32QuadVertex { DirectX::XMFLOAT3(rightNdc, bottomNdc, 0.0f), DirectX::XMFLOAT2(sourceRect.X + sourceRect.Z, sourceRect.Y + sourceRect.W), tint },
            Win32QuadVertex { DirectX::XMFLOAT3(rightNdc, topNdc, 0.0f), DirectX::XMFLOAT2(sourceRect.X + sourceRect.Z, sourceRect.Y), tint }
        };

        DrawQuadVertices(vertices.data(), static_cast<UINT>(vertices.size()));
    }

    /// Draws one textured quad in window-space pixel coordinates after applying a clockwise 2D rotation around its center.
    void Win32RenderManager2D::DrawTexturedQuadTransformed(
        ID3D11ShaderResourceView* textureView,
        float x,
        float y,
        float width,
        float height,
        float rotationRadians,
        float4 sourceRect,
        byte4 color) {
        if (!HasActiveViewport || textureView == nullptr || width <= 0.0f || height <= 0.0f || CurrentViewport.Width <= 0.0f || CurrentViewport.Height <= 0.0f) {
            return;
        }

        PrepareTexturedQuadDraw(textureView);

        const float halfWidth = width * 0.5f;
        const float halfHeight = height * 0.5f;
        const float centerX = x + halfWidth;
        const float centerYUp = -(y + halfHeight);
        const float rotationSin = std::sin(rotationRadians);
        const float rotationCos = std::cos(rotationRadians);
        const DirectX::XMFLOAT4 tint = ConvertColor(color);

        const float bottomLeftX = centerX + ((-halfWidth * rotationCos) - (-halfHeight * rotationSin));
        const float bottomLeftY = -(centerYUp + ((-halfWidth * rotationSin) + (-halfHeight * rotationCos)));
        const float topLeftX = centerX + ((-halfWidth * rotationCos) - (halfHeight * rotationSin));
        const float topLeftY = -(centerYUp + ((-halfWidth * rotationSin) + (halfHeight * rotationCos)));
        const float bottomRightX = centerX + ((halfWidth * rotationCos) - (-halfHeight * rotationSin));
        const float bottomRightY = -(centerYUp + ((halfWidth * rotationSin) + (-halfHeight * rotationCos)));
        const float topRightX = centerX + ((halfWidth * rotationCos) - (halfHeight * rotationSin));
        const float topRightY = -(centerYUp + ((halfWidth * rotationSin) + (halfHeight * rotationCos)));

        const float bottomLeftNdcX = (((bottomLeftX - CurrentViewport.TopLeftX) / CurrentViewport.Width) * 2.0f) - 1.0f;
        const float bottomLeftNdcY = 1.0f - (((bottomLeftY - CurrentViewport.TopLeftY) / CurrentViewport.Height) * 2.0f);
        const float topLeftNdcX = (((topLeftX - CurrentViewport.TopLeftX) / CurrentViewport.Width) * 2.0f) - 1.0f;
        const float topLeftNdcY = 1.0f - (((topLeftY - CurrentViewport.TopLeftY) / CurrentViewport.Height) * 2.0f);
        const float bottomRightNdcX = (((bottomRightX - CurrentViewport.TopLeftX) / CurrentViewport.Width) * 2.0f) - 1.0f;
        const float bottomRightNdcY = 1.0f - (((bottomRightY - CurrentViewport.TopLeftY) / CurrentViewport.Height) * 2.0f);
        const float topRightNdcX = (((topRightX - CurrentViewport.TopLeftX) / CurrentViewport.Width) * 2.0f) - 1.0f;
        const float topRightNdcY = 1.0f - (((topRightY - CurrentViewport.TopLeftY) / CurrentViewport.Height) * 2.0f);

        std::array<Win32QuadVertex, 4> vertices = {
            Win32QuadVertex { DirectX::XMFLOAT3(bottomLeftNdcX, bottomLeftNdcY, 0.0f), DirectX::XMFLOAT2(sourceRect.X, sourceRect.Y + sourceRect.W), tint },
            Win32QuadVertex { DirectX::XMFLOAT3(topLeftNdcX, topLeftNdcY, 0.0f), DirectX::XMFLOAT2(sourceRect.X, sourceRect.Y), tint },
            Win32QuadVertex { DirectX::XMFLOAT3(bottomRightNdcX, bottomRightNdcY, 0.0f), DirectX::XMFLOAT2(sourceRect.X + sourceRect.Z, sourceRect.Y + sourceRect.W), tint },
            Win32QuadVertex { DirectX::XMFLOAT3(topRightNdcX, topRightNdcY, 0.0f), DirectX::XMFLOAT2(sourceRect.X + sourceRect.Z, sourceRect.Y), tint }
        };

        DrawQuadVertices(vertices.data(), static_cast<UINT>(vertices.size()));
    }

    /// Uploads quad vertices into the reusable dynamic buffer and issues one draw from the written range.
    void Win32RenderManager2D::DrawQuadVertices(const void* vertices, UINT vertexCount) {
        if (vertices == nullptr || vertexCount == 0U) {
            return;
        }

        if (QuadVertexBuffer == nullptr || QuadVertexCapacity < vertexCount) {
            throw std::runtime_error("Windows 2D quad vertex buffer must be initialized before drawing.");
        }

        bool shouldDiscard = QuadVertexCursor == 0 || QuadVertexCursor + vertexCount > QuadVertexCapacity;
        if (shouldDiscard) {
            QuadVertexCursor = 0;
        }

        const UINT startVertex = QuadVertexCursor;
        QuadVertexCursor += vertexCount;

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        D3D11_MAPPED_SUBRESOURCE mappedResource {};
        ThrowIfFailed(
            context->Map(
                QuadVertexBuffer.Get(),
                0,
                shouldDiscard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE,
                0,
                &mappedResource),
            "ID3D11DeviceContext::Map failed for the Windows 2D quad vertex buffer.");

        Win32QuadVertex* vertexData = static_cast<Win32QuadVertex*>(mappedResource.pData);
        std::memcpy(vertexData + startVertex, vertices, sizeof(Win32QuadVertex) * vertexCount);
        context->Unmap(QuadVertexBuffer.Get(), 0);
        context->Draw(vertexCount, startVertex);
    }

    /// Draws one solid-color rectangle in window-space pixel coordinates.
    void Win32RenderManager2D::DrawSolidRect(float x, float y, float width, float height, byte4 color) {
        DrawTexturedQuad(WhiteShaderResourceView.Get(), x, y, width, height, float4(0.0f, 0.0f, 1.0f, 1.0f), color);
    }

    /// Draws one rounded rectangle using the native signed-distance-field shader path.
    void Win32RenderManager2D::DrawRoundedRectSdf(float4 bounds, float radius, float borderThickness, byte4 fillColor, byte4 borderColor, int32_t corners) {
        if (!HasActiveViewport || bounds.Z <= 0.0f || bounds.W <= 0.0f || CurrentViewport.Width <= 0.0f || CurrentViewport.Height <= 0.0f) {
            return;
        }

        PrepareRoundedRectDraw();

        const float leftNdc = (((bounds.X - CurrentViewport.TopLeftX) / CurrentViewport.Width) * 2.0f) - 1.0f;
        const float rightNdc = ((((bounds.X + bounds.Z) - CurrentViewport.TopLeftX) / CurrentViewport.Width) * 2.0f) - 1.0f;
        const float topNdc = 1.0f - (((bounds.Y - CurrentViewport.TopLeftY) / CurrentViewport.Height) * 2.0f);
        const float bottomNdc = 1.0f - ((((bounds.Y + bounds.W) - CurrentViewport.TopLeftY) / CurrentViewport.Height) * 2.0f);

        std::array<Win32QuadVertex, 4> vertices = {
            Win32QuadVertex { DirectX::XMFLOAT3(leftNdc, bottomNdc, 0.0f), DirectX::XMFLOAT2(0.0f, 1.0f), DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) },
            Win32QuadVertex { DirectX::XMFLOAT3(leftNdc, topNdc, 0.0f), DirectX::XMFLOAT2(0.0f, 0.0f), DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) },
            Win32QuadVertex { DirectX::XMFLOAT3(rightNdc, bottomNdc, 0.0f), DirectX::XMFLOAT2(1.0f, 1.0f), DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) },
            Win32QuadVertex { DirectX::XMFLOAT3(rightNdc, topNdc, 0.0f), DirectX::XMFLOAT2(1.0f, 0.0f), DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) }
        };

        Win32RoundedRectShaderConstants constants {};
        constants.DestRect = DirectX::XMFLOAT4(bounds.X, bounds.Y, bounds.Z, bounds.W);
        constants.Params1 = DirectX::XMFLOAT4(radius, borderThickness, 1.0f, static_cast<float>(corners));
        constants.FillColor = ConvertColor(fillColor);
        constants.BorderColor = ConvertColor(borderColor);
        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        context->UpdateSubresource(RoundedRectConstantBuffer.Get(), 0, nullptr, &constants, 0, 0);
        DrawQuadVertices(vertices.data(), static_cast<UINT>(vertices.size()));
    }

    /// Draws every queued 2D drawable for one camera.
    void Win32RenderManager2D::RenderCamera(ICamera* camera) {
        if (camera == nullptr) {
            throw new ArgumentNullException("camera");
        }

        Entity* cameraParent = camera->get_Parent();
        if (cameraParent == nullptr || !cameraParent->get_IsHierarchyEnabled()) {
            return;
        }

        IRenderQueue2D* renderQueue = camera->get_RenderQueue2D();
        if (renderQueue == nullptr) {
            return;
        }

#if __has_include("RenderCommandListBuilder2D.hpp")
        if (!CommandListBuilder) {
            CommandListBuilder = std::make_unique<RenderCommandListBuilder2D>();
        }

        RenderCommandList2D* commandList = CommandListBuilder->Build(renderQueue);
        if (commandList == nullptr || commandList->get_Count() <= 0) {
            return;
        }
#endif

        CurrentViewport = ResolveViewport(camera);
        HasActiveViewport = true;
        QuadVertexCursor = 0;
        ClipRectStack.clear();
        HasActiveClipRect = false;
        if (!HasWritten2DSummary) {
            AppendRenderDiagnosticsLine(
                "2d.render_camera queue_count=" + std::to_string(renderQueue->get_Count())
                + " viewport="
                + std::to_string(CurrentViewport.TopLeftX) + ","
                + std::to_string(CurrentViewport.TopLeftY) + ","
                + std::to_string(CurrentViewport.Width) + ","
                + std::to_string(CurrentViewport.Height));
        }

#if __has_include("RenderCommandListBuilder2D.hpp")
        int texturedQuadCount = 0;
        int glyphQuadCount = 0;
        int roundedRectCount = 0;
        const int32_t commandCount = commandList->get_Count();
        for (int32_t commandIndex = 0; commandIndex < commandCount; commandIndex++) {
            RenderCommand2DType commandType = commandList->GetCommandType(commandIndex);
            if (commandType == RenderCommand2DType::ClipPush) {
                const int32_t payloadIndex = commandList->GetClipPushPayloadIndex(commandIndex);
                PushClipRect(commandList->GetClipPushRect(payloadIndex));
                continue;
            }

            if (commandType == RenderCommand2DType::ClipPop) {
                PopClipRect();
                continue;
            }

            if (commandType == RenderCommand2DType::TexturedQuad) {
                texturedQuadCount++;
                const int32_t payloadIndex = commandList->GetTexturedQuadPayloadIndex(commandIndex);
                RuntimeTexture* texture = commandList->GetTexturedQuadTexture(payloadIndex);
                ID3D11ShaderResourceView* textureView = ResolveTextureResourceView(texture);
                if (textureView == nullptr) {
                    continue;
                }

                const float4 bounds = commandList->GetTexturedQuadBounds(payloadIndex);
                if (!HasWritten2DDraw) {
                    AppendRenderDiagnosticsLine(
                        "2d.command textured_quad bounds="
                        + std::to_string(bounds.X) + ","
                        + std::to_string(bounds.Y) + ","
                        + std::to_string(bounds.Z) + ","
                        + std::to_string(bounds.W)
                        + " rotation="
                        + std::to_string(commandList->GetTexturedQuadRotation(payloadIndex)));
                    HasWritten2DDraw = true;
                }

                DrawTexturedQuadTransformed(
                    textureView,
                    bounds.X,
                    bounds.Y,
                    bounds.Z,
                    bounds.W,
                    commandList->GetTexturedQuadRotation(payloadIndex),
                    commandList->GetTexturedQuadSourceRect(payloadIndex),
                    commandList->GetTexturedQuadColor(payloadIndex));
                continue;
            }

            if (commandType == RenderCommand2DType::GlyphQuad) {
                glyphQuadCount++;
                const int32_t payloadIndex = commandList->GetGlyphQuadPayloadIndex(commandIndex);
                RuntimeTexture* texture = commandList->GetGlyphQuadTexture(payloadIndex);
                ID3D11ShaderResourceView* textureView = ResolveTextureResourceView(texture);
                if (textureView == nullptr) {
                    continue;
                }

                const float4 bounds = commandList->GetGlyphQuadBounds(payloadIndex);
                if (!HasWritten2DDraw) {
                    AppendRenderDiagnosticsLine(
                        "2d.command glyph_quad bounds="
                        + std::to_string(bounds.X) + ","
                        + std::to_string(bounds.Y) + ","
                        + std::to_string(bounds.Z) + ","
                        + std::to_string(bounds.W));
                    HasWritten2DDraw = true;
                }

                DrawTexturedQuad(
                    textureView,
                    bounds.X,
                    bounds.Y,
                    bounds.Z,
                    bounds.W,
                    commandList->GetGlyphQuadSourceRect(payloadIndex),
                    commandList->GetGlyphQuadColor(payloadIndex));
                continue;
            }

            if (commandType == RenderCommand2DType::RoundedRect) {
                roundedRectCount++;
                const int32_t payloadIndex = commandList->GetRoundedRectPayloadIndex(commandIndex);
                const float4 bounds = commandList->GetRoundedRectBounds(payloadIndex);

                if (!HasWritten2DDraw) {
                    AppendRenderDiagnosticsLine(
                        "2d.command rounded_rect bounds="
                        + std::to_string(bounds.X) + ","
                        + std::to_string(bounds.Y) + ","
                        + std::to_string(bounds.Z) + ","
                        + std::to_string(bounds.W)
                        );
                    HasWritten2DDraw = true;
                }

                DrawRoundedRectSdf(
                    bounds,
                    commandList->GetRoundedRectRadius(payloadIndex),
                    commandList->GetRoundedRectBorderThickness(payloadIndex),
                    commandList->GetRoundedRectFillColor(payloadIndex),
                    commandList->GetRoundedRectBorderColor(payloadIndex),
                    static_cast<int32_t>(commandList->GetRoundedRectCorners(payloadIndex)));
                continue;
            }
        }
#else
        renderQueue->VisitOrdered(this);
#endif

        if (!HasWritten2DSummary) {
            AppendRenderDiagnosticsLine(
                std::string("2d.summary")
#if __has_include("RenderCommandListBuilder2D.hpp")
                + " command_count=" + std::to_string(commandCount)
                + " command_textured_quads=" + std::to_string(texturedQuadCount)
                + " command_glyph_quads=" + std::to_string(glyphQuadCount)
                + " command_rounded_rects=" + std::to_string(roundedRectCount));
#else
                + " visits=" + std::to_string(Logged2DVisitCount)
                + " rects=" + std::to_string(Logged2DRectCount)
                + " texts=" + std::to_string(Logged2DTextCount)
                + " sprites=" + std::to_string(Logged2DSpriteCount)
                + " text_early_returns=" + std::to_string(Logged2DTextEarlyReturnCount));
#endif
            HasWritten2DSummary = true;
        }
        ClipRectStack.clear();
        HasActiveClipRect = false;
        HasActiveViewport = false;
    }

    /// Visits one queued 2D drawable and lets it dispatch into the concrete draw methods.
    void Win32RenderManager2D::Visit(IDrawable2D* drawable) {
        if (drawable == nullptr) {
            return;
        }

        if (!HasWritten2DSummary) {
            Logged2DVisitCount++;
        }

        if (!HasWritten2DDraw) {
            AppendRenderDiagnosticsLine("2d.visit drawable");
        }

        drawable->Draw();
    }

    /// Resolves a camera viewport against the current swap-chain size.
    D3D11_VIEWPORT Win32RenderManager3D::ResolveViewport(ICamera* camera) const {
        const float4 rawViewport = camera->get_Viewport();
        float offsetX = rawViewport.X;
        float offsetY = rawViewport.Y;
        float width = rawViewport.Z;
        float height = rawViewport.W;

        if (width <= 1.0f && height <= 1.0f && width > 0.0f && height > 0.0f) {
            offsetX *= static_cast<float>(Bootstrap.GetWidth());
            offsetY *= static_cast<float>(Bootstrap.GetHeight());
            width *= static_cast<float>(Bootstrap.GetWidth());
            height *= static_cast<float>(Bootstrap.GetHeight());
        }

        if (width <= 0.0f || height <= 0.0f) {
            offsetX = 0.0f;
            offsetY = 0.0f;
            width = static_cast<float>(Bootstrap.GetWidth());
            height = static_cast<float>(Bootstrap.GetHeight());
        }

        D3D11_VIEWPORT viewport {};
        viewport.TopLeftX = offsetX;
        viewport.TopLeftY = offsetY;
        viewport.Width = width;
        viewport.Height = height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        return viewport;
    }

    /// Returns the number of uploaded texture resources currently cached by the Windows bridge.
    std::size_t Win32RenderManager2D::GetTextureResourceCount() const {
        return TextureResources.size();
    }

    /// Returns the number of engine-owned uploaded texture resources currently cached by the Windows bridge.
    std::size_t Win32RenderManager2D::GetEngineOwnedTextureResourceCount() const {
        return EngineOwnedTextureResourceIds.size();
    }

    /// Builds a placeholder runtime texture from raw asset metadata.
    RuntimeTexture* Win32RenderManager2D::BuildTextureFromRaw(TextureAsset* data) {
        RuntimeTexture* runtimeTexture = new RuntimeTexture();
        if (data != nullptr) {
            std::string textureId = data->get_Id();
            if (textureId.empty()) {
                textureId = BuildGeneratedTextureResourceId();
            }

            runtimeTexture->set_Id(textureId);
            runtimeTexture->set_Width(data->Width);
            runtimeTexture->set_Height(data->Height);
            runtimeTexture->set_IsEngineOwned(data->IsEngineOwned);

            if (data->Colors == nullptr || data->Colors->Length == 0) {
                throw new InvalidOperationException("Texture assets must include embedded color data.");
            }

            if (data->Width == 0 || data->Height == 0) {
                throw new InvalidOperationException("Texture assets must define a non-zero width and height.");
            }

            D3D11_TEXTURE2D_DESC textureDescription {};
            textureDescription.Width = static_cast<UINT>(data->Width);
            textureDescription.Height = static_cast<UINT>(data->Height);
            textureDescription.MipLevels = 1;
            textureDescription.ArraySize = 1;
            textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            textureDescription.SampleDesc.Count = 1;
            textureDescription.Usage = D3D11_USAGE_DEFAULT;
            textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA textureData {};
            textureData.pSysMem = data->Colors->Data;
            textureData.SysMemPitch = static_cast<UINT>(static_cast<UINT64>(data->Width) * 4ULL);

            Win32TextureResource textureResource;
            ThrowIfFailed(
                Bootstrap.GetDevice()->CreateTexture2D(&textureDescription, &textureData, textureResource.Texture.GetAddressOf()),
                "ID3D11Device::CreateTexture2D failed for a packaged texture asset.");

            D3D11_SHADER_RESOURCE_VIEW_DESC resourceViewDescription {};
            resourceViewDescription.Format = textureDescription.Format;
            resourceViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            resourceViewDescription.Texture2D.MostDetailedMip = 0;
            resourceViewDescription.Texture2D.MipLevels = 1;
            ThrowIfFailed(
                Bootstrap.GetDevice()->CreateShaderResourceView(textureResource.Texture.Get(), &resourceViewDescription, textureResource.ShaderResourceView.GetAddressOf()),
                "ID3D11Device::CreateShaderResourceView failed for a packaged texture asset.");

            TextureResources[textureId] = std::make_unique<Win32TextureResource>(std::move(textureResource));
            if (data->IsEngineOwned) {
                EngineOwnedTextureResourceIds.insert(textureId);
            } else {
                EngineOwnedTextureResourceIds.erase(textureId);
            }
            RuntimeRenderDiagnostics::RecordAssetBuild(
                "texture",
                textureId,
                BuildTextureDiagnosticsDetail(data, textureId),
                TextureResources.size());
        }

        return runtimeTexture;
    }

    /// Releases one runtime texture previously created by the Windows renderer.
    void Win32RenderManager2D::ReleaseTexture(RuntimeTexture* texture) {
        if (texture == nullptr) {
            throw std::invalid_argument("Runtime texture must be provided for release.");
        }

        const std::string textureId = texture->get_Id();
        RuntimeRenderDiagnostics::RecordAssetReleaseRequested("texture", textureId, "renderer=windows");
        if (!textureId.empty()) {
            TextureResources.erase(textureId);
            EngineOwnedTextureResourceIds.erase(textureId);
        }

        RuntimeRenderDiagnostics::RecordAssetReleaseCompleted(
            "texture",
            textureId,
            "renderer=windows texture_resources=" + std::to_string(TextureResources.size()));
    }

    /// Releases one font asset previously materialized for the Windows renderer.
    void Win32RenderManager2D::ReleaseFont(FontAsset* font) {
        if (font == nullptr) {
            throw std::invalid_argument("Font asset must be provided for release.");
        }

        const std::string fontId = "font@" + std::to_string(reinterpret_cast<std::uintptr_t>(font));
        RuntimeRenderDiagnostics::RecordAssetReleaseRequested("font", fontId, "renderer=windows");
        RenderManager2D::ReleaseFont(font);
        RuntimeRenderDiagnostics::RecordAssetReleaseCompleted("font", fontId, "renderer=windows");
    }

    /// Flushes any deferred Windows runtime texture releases.
    void Win32RenderManager2D::FlushReleasedTextures() {
    }

    /// Releases Windows renderer-owned 2D resources.
    void Win32RenderManager2D::Dispose() {
        TextureResources.clear();
        EngineOwnedTextureResourceIds.clear();
        QuadVertexBuffer.Reset();
        QuadVertexCapacity = 0;
        QuadVertexCursor = 0;
        QuadInputLayout.Reset();
        QuadVertexShader.Reset();
        QuadPixelShader.Reset();
        RoundedRectVertexShader.Reset();
        RoundedRectPixelShader.Reset();
        TextureSamplerState.Reset();
        AlphaBlendState.Reset();
        RasterizerState.Reset();
        DepthStencilState.Reset();
        WhiteTexture.Reset();
        WhiteShaderResourceView.Reset();
        RoundedRectConstantBuffer.Reset();
        ClipRectStack.clear();
        CommandListBuilder.reset();
        HasActiveViewport = false;
        HasActiveClipRect = false;
    }

    /// Accepts a sprite draw request without issuing backend rendering yet.
    void Win32RenderManager2D::DrawSprite(ISpriteDrawable2D* sprite) {
        if (sprite == nullptr || sprite->get_Parent() == nullptr || !sprite->get_Parent()->get_IsHierarchyEnabled()) {
            return;
        }

        if (!HasWritten2DSummary) {
            Logged2DSpriteCount++;
        }

        RuntimeTexture* texture = sprite->get_Texture();
        if (texture == nullptr) {
            return;
        }

        ID3D11ShaderResourceView* textureView = ResolveTextureResourceView(texture);
        if (textureView == nullptr) {
            return;
        }

        int2 size = sprite->get_Size();
        const float baseWidth = size.X > 0 ? static_cast<float>(size.X) : static_cast<float>(texture->get_Width());
        const float baseHeight = size.Y > 0 ? static_cast<float>(size.Y) : static_cast<float>(texture->get_Height());
        Entity* parent = sprite->get_Parent();
        const float3 position = parent->get_Position();
        const float3 scale = parent->get_Scale();
        const float4 orientation = parent->get_Orientation();
        const float width = baseWidth * scale.X;
        const float height = baseHeight * scale.Y;
        const float3 rotatedRight = float4::RotateVector(float3(1.0f, 0.0f, 0.0f), orientation);
        const float rotationRadians = static_cast<float>(std::atan2(rotatedRight.Y, rotatedRight.X));
        if (!HasWritten2DDraw) {
            AppendRenderDiagnosticsLine(
                "2d.draw_sprite pos="
                + std::to_string(position.X) + ","
                + std::to_string(position.Y)
                + " size="
                + std::to_string(width) + ","
                + std::to_string(height)
                + " scale="
                + std::to_string(scale.X) + ","
                + std::to_string(scale.Y) + ","
                + std::to_string(scale.Z)
                + " rotation="
                + std::to_string(rotationRadians));
            HasWritten2DDraw = true;
        }
        DrawTexturedQuadTransformed(textureView, position.X, position.Y, width, height, rotationRadians, sprite->get_SourceRect(), sprite->get_Color());
    }

    /// Accepts a text draw request without issuing backend rendering yet.
    void Win32RenderManager2D::DrawText(ITextDrawable2D* text) {
        if (text == nullptr || text->get_Parent() == nullptr || !text->get_Parent()->get_IsHierarchyEnabled()) {
            return;
        }

        if (!HasWritten2DSummary) {
            Logged2DTextCount++;
        }

        FontAsset* font = text->get_Font();
        if (font == nullptr || font->get_Texture() == nullptr || font->get_Characters() == nullptr) {
            if (!HasWritten2DSummary) {
                Logged2DTextEarlyReturnCount++;
            }
            return;
        }

        ID3D11ShaderResourceView* textureView = ResolveTextureResourceView(font->get_Texture());
        if (textureView == nullptr) {
            if (!HasWritten2DSummary) {
                Logged2DTextEarlyReturnCount++;
            }
            return;
        }

        std::string value = text->get_Text();
        if (value.empty()) {
            if (!HasWritten2DSummary) {
                Logged2DTextEarlyReturnCount++;
            }
            return;
        }

        const float3 position = text->get_Parent()->get_Position();
        const double baseX = std::round(position.X);
        const double baseY = std::round(position.Y);
        const double lineHeight = std::max(static_cast<double>(font->get_LineHeight()), 1.0);
        const float atlasWidth = static_cast<float>(std::max(font->get_AtlasWidth(), 1));
        const float atlasHeight = static_cast<float>(std::max(font->get_AtlasHeight(), 1));
        const float spaceWidth = font->get_FontInfo() != nullptr ? font->get_FontInfo()->get_SpaceWidth() : 0.0f;
        double offsetX = 0.0;
        double offsetY = 0.0;

        for (char character : value) {
            if (character == '\n') {
                offsetY += lineHeight;
                offsetX = 0.0;
                continue;
            }

            if (character == ' ') {
                offsetX += spaceWidth;
                continue;
            }

            FontChar glyph;
            if (!font->get_Characters()->TryGetValue(character, glyph)) {
                continue;
            }

            const float4 sourceRect = glyph.SourceRect;
            const float glyphWidth = sourceRect.Z * atlasWidth;
            const float glyphHeight = sourceRect.W * atlasHeight;
            const float drawX = static_cast<float>(baseX + offsetX);
            const float drawY = static_cast<float>(baseY + std::round(offsetY) + glyph.OffsetY);
            if (!HasWritten2DDraw) {
                AppendRenderDiagnosticsLine(
                    "2d.draw_text glyph pos="
                    + std::to_string(drawX) + ","
                    + std::to_string(drawY)
                    + " size="
                    + std::to_string(glyphWidth) + ","
                    + std::to_string(glyphHeight));
                HasWritten2DDraw = true;
            }
            DrawTexturedQuad(textureView, drawX, drawY, glyphWidth, glyphHeight, sourceRect, text->get_Color());

            const double advance = glyph.AdvanceWidth > 0.0f ? glyph.AdvanceWidth : glyphWidth;
            offsetX += advance;
        }
    }

    /// Accepts a rounded-rectangle draw request without issuing backend rendering yet.
    void Win32RenderManager2D::DrawRoundedRect(IRoundedRectDrawable2D* shape) {
        if (shape == nullptr || shape->get_Parent() == nullptr || !shape->get_Parent()->get_IsHierarchyEnabled()) {
            return;
        }

        if (!HasWritten2DSummary) {
            Logged2DRectCount++;
        }

        int2 size = shape->get_Size();
        if (size.X <= 0 || size.Y <= 0) {
            return;
        }
        const float3 position = shape->get_Parent()->get_Position();
        const float width = static_cast<float>(size.X);
        const float height = static_cast<float>(size.Y);
        if (!HasWritten2DDraw) {
            AppendRenderDiagnosticsLine(
                "2d.draw_rect pos="
                + std::to_string(position.X) + ","
                + std::to_string(position.Y)
                + " size="
                + std::to_string(width) + ","
                + std::to_string(height));
            HasWritten2DDraw = true;
        }
        DrawRoundedRectSdf(
            float4(position.X, position.Y, width, height),
            shape->get_Radius(),
            shape->get_BorderThickness(),
            shape->get_FillColor(),
            shape->get_BorderColor(),
            static_cast<int32_t>(shape->get_Corners()));
    }
#endif
}














