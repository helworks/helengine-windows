#pragma once
#include <string>
#include "platform/windows/win32/win32_scene_ssgi_shader.hpp"
#include "platform/windows/win32/win32_scene_ink_shader.hpp"
#include "platform/windows/directx11/directx11_output_color_shader.hpp"

namespace helengine::windows {
        /// Implements material-preserving stepped lighting and geometric outlines without a source-image effect.
    inline const std::string CameraToonGeometryShader = std::string(SceneSsgiShader)+SceneInkShader+R"(
cbuffer TransformBuffer : register(b0) {
    float4x4 World; float4x4 WorldViewProjection; float4x4 WorldNormal;
    float4 CameraPosition; float4 MaterialFlags;
};
cbuffer ForwardLights : register(b1) {
    float4 Ambient; float4 LightMetadata;
    float4 Light0Color; float4 Light0Direction; float4 Light0Position; float4 Light0Spot;
    float4 Light1Color; float4 Light1Direction; float4 Light1Position; float4 Light1Spot;
    float4 Light2Color; float4 Light2Direction; float4 Light2Position; float4 Light2Spot;
    float4 Light3Color; float4 Light3Direction; float4 Light3Position; float4 Light3Spot;
};
cbuffer BaseColorBuffer : register(b3) { float4 BaseColor; };
cbuffer RoughnessBuffer : register(b4) { float4 Roughness; };
cbuffer EmissiveColorBuffer : register(b7) { float4 EmissiveColor; };
cbuffer ToonBuffer : register(b8) { float4 Style; float4 Size; float4 Effects; float4 Material; float4 Shadow; float4 Blur; float4 Pearl; float4 Display; float4 Occlusion; float4 SurfaceStyle; float4 Crease; float4 DepthProjection; float4 SelectionColor; float4 SelectionBounds; };
cbuffer ShadowBuffer : register(b2) {
    float4 ShadowMetadata;
    float4 ShadowRect0; float4 ShadowSlot0; float4x4 ShadowMatrix0;
    float4 ShadowRect1; float4 ShadowSlot1; float4x4 ShadowMatrix1;
    float4 ShadowRect2; float4 ShadowSlot2; float4x4 ShadowMatrix2;
    float4 ShadowRect3; float4 ShadowSlot3; float4x4 ShadowMatrix3;
    float4x4 CascadeWorldToShadowClip;
    float4 CascadeAtlasRect;
    float4 CascadeDepthPlane;
    float4 CascadeDepths;
    float4x4 PointShadowMatrices[24];
};
Texture2D DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);
Texture2D<float> ShadowAtlas : register(t1);
SamplerState ShadowSampler : register(s1);
Texture2D RoughnessTexture : register(t6);
SamplerState RoughnessSampler : register(s6);
Texture2D EmissiveTexture : register(t7);
SamplerState EmissiveSampler : register(s7);
Texture2D ColorTexture : register(t8);
#if TOON_SAMPLES == 4
Texture2DMS<uint,4> PartTexture : register(t9);
Texture2DMS<uint2,4> AuxiliaryPartTexture : register(t28);
Texture2DMS<float,4> ContourDepthTexture : register(t30);
#else
Texture2D<uint> PartTexture : register(t9);
Texture2D<uint2> AuxiliaryPartTexture : register(t28);
Texture2D<float> ContourDepthTexture : register(t30);
#endif
Texture2D<float> CoverageTexture : register(t10);
Texture2D GlowSource : register(t11);
Texture2D EnvironmentTexture : register(t12);
Texture2D FilteredGlow : register(t13);
Texture2D<float4> GeometryPositionTexture : register(t14);
Texture2D<float4> GeometryNormalTexture : register(t15);
Texture2D<float> OutlineTileTexture : register(t16);
Texture2D<float4> DiffuseIndirectTexture : register(t26);
Texture2D<float> AmbientVisibilityTexture : register(t27);
SamplerState EffectSampler : register(s8);
#if defined(HELENGINE_LIGHTMAP)
cbuffer LightmapBuffer : register(b11) { float4 LightmapState; };
Texture2D<float4> LightmapAtlas : register(t23);
SamplerState LightmapSampler : register(s9);
#endif
cbuffer ReflectionProbeBuffer : register(b12) {
    float4 ReflectionProbeState; float4 ReflectionProbePosition; float4 ReflectionProbeMinimum; float4 ReflectionProbeMaximum;
};
TextureCube<float4> ReflectionProbeCube : register(t24);
Texture2D<float2> ReflectionProbeBrdf : register(t25);
SamplerState ReflectionProbeSampler : register(s10);
struct MeshInput { float3 Position : POSITION; float3 Normal : NORMAL; float2 UV : TEXCOORD0; };
#if defined(HELENGINE_LIGHTMAP)
struct LightmapMeshInput { float3 Position : POSITION; float3 Normal : NORMAL; float2 UV : TEXCOORD0; float2 LightmapUV : TEXCOORD1; };
#endif
struct MeshOutput { float4 Position : SV_POSITION; float3 WorldPosition : TEXCOORD0; float3 Normal : TEXCOORD1; float2 UV : TEXCOORD2;
#if defined(HELENGINE_LIGHTMAP)
float2 LightmapUV : TEXCOORD3;
#endif
 };
struct GeometryOutput { float4 Color : SV_Target0; uint Part : SV_Target1; float Coverage : SV_Target2; float4 Emission : SV_Target3; float4 GeometryPosition : SV_Target4; float4 GeometryNormal : SV_Target5; float4 DiffuseIndirect : SV_Target6; uint2 AuxiliaryPart : SV_Target7; };
MeshOutput GeometryVS(MeshInput input) {
    MeshOutput output;
    output.Position = mul(float4(input.Position, 1.0), WorldViewProjection);
    output.WorldPosition = mul(float4(input.Position, 1.0), World).xyz;
    output.Normal = mul(float4(input.Normal, 0.0), WorldNormal).xyz;
    output.UV = input.UV;
#if defined(HELENGINE_LIGHTMAP)
    output.LightmapUV = 0;
#endif
    return output;
}
#if defined(HELENGINE_LIGHTMAP)
MeshOutput LightmapGeometryVS(LightmapMeshInput input) {
    MeshInput original; original.Position=input.Position;original.Normal=input.Normal;original.UV=input.UV;
    MeshOutput output=GeometryVS(original);output.LightmapUV=input.LightmapUV;return output;
}
#endif
// Compare against the receiver plane at the loaded texel center instead of displacing caster slopes.
// World derivatives precede cascade selection so adjacent pixels may select different matrices safely.
float2 HardShadowReceiverPlane(float3 worldDx,float3 worldDy,float4 clip,float4x4 shadowTransform,float4 rect,float2 atlasUv,float2 centerUv,float2 texel) {
    float3 ndc=clip.xyz/clip.w;
    float4 clipDx=mul(float4(worldDx,0),shadowTransform),clipDy=mul(float4(worldDy,0),shadowTransform);
    float3 ndcDx=(clipDx.xyz-ndc*clipDx.w)/clip.w,ndcDy=(clipDy.xyz-ndc*clipDy.w)/clip.w;
    float2 uvDx=ndcDx.xy*float2(0.5,-0.5)*rect.zw,uvDy=ndcDy.xy*float2(0.5,-0.5)*rect.zw;
    float determinant=uvDx.x*uvDy.y-uvDx.y*uvDy.x;
    float scale=max(dot(uvDx,uvDx),dot(uvDy,uvDy));
    // FP32 roundoff allowance validated in the near/far fixtures; the depth floor guards small normalized values.
    float roundoff=8.0*1.192092896e-7*max(abs(ndc.z),0.25);
    // Degenerate footprints retain finite depth and FP32 tolerance without a large constant bias.
    if(scale<=1e-30 || abs(determinant)<=scale*1e-6) return float2(ndc.z,roundoff);
    float2 gradient=float2(ndcDx.z*uvDy.y-uvDx.y*ndcDy.z,uvDx.x*ndcDy.z-ndcDx.z*uvDy.x)/determinant;
    float receiverDepth=ndc.z+dot(gradient,centerUv-atlasUv);
    // D3D11 snaps XY to eight fractional bits: each axis can move by half of 1/256 shadow pixel.
    float rasterError=dot(abs(gradient),texel)*(1.0/512.0);
    roundoff=8.0*1.192092896e-7*max(abs(receiverDepth),0.25);
    return float2(receiverDepth,rasterError+roundoff);
}
float ShadowVisibility(float3 position, float4 rect, float4 metadata, float4x4 worldToShadowClip) {
    float3 worldDx=ddx(position),worldDy=ddy(position);
    if (Shadow.x<=0 || MaterialFlags.x<0.5 || ShadowMetadata.x<0.5 || metadata.x<0.5) return 1;
    if(metadata.z<1.5 && CascadeDepths.w>1.5 && dot(float4(position,1),CascadeDepthPlane)>CascadeDepths.y) {
        worldToShadowClip=CascadeWorldToShadowClip;rect=CascadeAtlasRect;
    }
    float4 clipPosition=mul(float4(position,1),worldToShadowClip);
    if(clipPosition.w<=0) return 1;
    float3 ndc=clipPosition.xyz/clipPosition.w;
    float2 uv=float2(ndc.x*0.5+0.5,-ndc.y*0.5+0.5);
    if(any(uv<0) || any(uv>1) || ndc.z<0 || ndc.z>1) return 1;
    float2 atlasUv=rect.xy+uv*rect.zw;
    float2 texel=ShadowMetadata.yz;
    float2 lower=rect.xy+texel*0.5, upper=rect.xy+rect.zw-texel*0.5;
    if(InkState.x>0.5) {
        uint width,height;ShadowAtlas.GetDimensions(width,height);
        int2 tileMinimum=int2(round(rect.xy*float2(width,height)));
        int2 tileMaximum=int2(round((rect.xy+rect.zw)*float2(width,height)))-1;
        int2 pixel=clamp(int2(atlasUv*float2(width,height)),tileMinimum,tileMaximum);
        float2 centerUv=(float2(pixel)+0.5)/float2(width,height);
        float2 receiverPlane=HardShadowReceiverPlane(worldDx,worldDy,clipPosition,worldToShadowClip,rect,atlasUv,centerUv,float2(1.0/width,1.0/height));
        float visible=(receiverPlane.x-receiverPlane.y)<=ShadowAtlas.Load(int3(pixel,0))?1.0:0.0;
        return lerp(1.0,visible,saturate(metadata.y*Shadow.x));
    }
    float visible=0;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        float2 tap=clamp(atlasUv+float2(x,y)*texel*Shadow.y,lower,upper);
        visible+=(ndc.z-0.0002)<=ShadowAtlas.SampleLevel(ShadowSampler,tap,0) ? 1.0 : 0.0;
    }
    return lerp(1.0,visible/9.0,saturate(metadata.y*Shadow.x));
}
// Point faces reuse the atlas hard-shadow receiver plane without changing directional/spot conventions.
float InkShadowVisibility(float3 position,float4 rect,float4 metadata,float4x4 faceTransform,float3 lightPosition,uint lightIndex) {
    if(metadata.z>2.5) {
        float3 delta=position-lightPosition,axis=abs(delta);
        uint face=axis.x>=axis.y && axis.x>=axis.z?(delta.x>=0?0:1):(axis.y>=axis.z?(delta.y>=0?2:3):(delta.z>=0?4:5));
        faceTransform=PointShadowMatrices[lightIndex*6+face];
        float2 size=rect.zw/float2(2,3);
        rect=float4(rect.xy+float2(face%2,face/2)*size,size);
    }
    return ShadowVisibility(position,rect,metadata,faceTransform);
}
float3 EvaluateLight(float3 normal, float3 position, float4 color, float4 direction, float4 location, float4 spot, float visibility) {
    float3 lightDirection = normalize(-direction.xyz);
    float attenuation = 1.0;
    if (color.w > 0.5) {
        float3 delta = location.xyz - position;
        float distanceToLight = length(delta);
        lightDirection = delta / max(distanceToLight, 0.0001);
        float normalizedDistance = distanceToLight / max(location.w, 0.0001);
        attenuation = saturate(1.0 - normalizedDistance * normalizedDistance);
        attenuation *= attenuation;
        if (color.w > 1.5) {
            float cone = saturate((dot(-lightDirection, normalize(direction.xyz)) - spot.y) / max(spot.x - spot.y, 0.0001));
            attenuation *= cone * cone;
        }
    }
    float diffuse = saturate(dot(normal, lightDirection));
    if(InkState.x>0.5) return (color.w>0.5 ? color.rgb*InkBand(diffuse)*saturate(visibility) : InkLightColor(color.rgb,diffuse,visibility))*attenuation;
    float steppedDiffuse = Material.w>0.5 ? floor(diffuse * (Style.z - 1.0) + 0.5) / (Style.z - 1.0) : diffuse;
    return color.rgb * steppedDiffuse * attenuation * visibility / 3.14159265;
}
float3 EvaluatePearlLight(float3 normal, float3 position, float3 viewDirection, float roughness,
    float4 color, float4 direction, float4 location, float4 spot, float visibility) {
    float3 lightDirection=PearlSafeDirection(-direction.xyz);
    float attenuation=1;
    if(color.w>0.5) {
        float3 delta=location.xyz-position;
        float distanceToLight=length(delta);
        lightDirection=delta/max(distanceToLight,0.0001);
        float normalizedDistance=distanceToLight/max(location.w,0.0001);
        attenuation=saturate(1-normalizedDistance*normalizedDistance);
        attenuation*=attenuation;
        if(color.w>1.5) {
            float cone=saturate((dot(-lightDirection,PearlSafeDirection(direction.xyz))-spot.y)/max(spot.x-spot.y,0.0001));
            attenuation*=cone*cone;
        }
    }
    return float3(1,0.9,1)*color.rgb*Pearl.y*PearlLightLobe(normal,viewDirection,lightDirection,roughness,Pearl.z)*attenuation*visibility;
}
// Evaluates a directional sun with GGX distribution, Smith masking and base-color conductor Fresnel.
// Direct visibility follows the existing shadow contract; no environment or pearl toggle controls this lobe.
float3 EvaluateMetallicSun(float3 normal, float3 viewDirection, float3 baseColor, float roughness,
    float4 color, float4 direction, float visibility) {
    if(InkState.x>0.5) return InkMetallicHighlight(normal,viewDirection,baseColor,roughness,color,direction,visibility);
    if(color.w>0.5 || visibility<=0) return 0;
    float lightLengthSquared=dot(direction.xyz,direction.xyz);
    if(lightLengthSquared<0.00000001) return 0;
    float3 lightDirection=-direction.xyz*rsqrt(lightLengthSquared);
    float nDotL=saturate(dot(normal,lightDirection));
    float nDotV=saturate(dot(normal,viewDirection));
    if(nDotL<=0 || nDotV<=0) return 0;
    float3 halfVector=lightDirection+viewDirection;
    float halfLengthSquared=dot(halfVector,halfVector);
    if(halfLengthSquared<0.00000001) return 0;
    float3 halfDirection=halfVector*rsqrt(halfLengthSquared);
    float nDotH=saturate(dot(normal,halfDirection));
    float vDotH=saturate(dot(viewDirection,halfDirection));
    float boundedRoughness=clamp(roughness,0.045,1.0);
    float alpha=boundedRoughness*boundedRoughness;
    float alphaSquared=alpha*alpha;
    float denominator=nDotH*nDotH*(alphaSquared-1.0)+1.0;
    float distribution=alphaSquared/(3.14159265*denominator*denominator);
    float maskingL=2.0*nDotL/(nDotL+sqrt(alphaSquared+(1.0-alphaSquared)*nDotL*nDotL));
    float maskingV=2.0*nDotV/(nDotV+sqrt(alphaSquared+(1.0-alphaSquared)*nDotV*nDotV));
    float grazing=pow(1.0-vDotH,5.0);
    float3 f0=saturate(baseColor);
    float3 fresnel=f0+(1.0-f0)*grazing;
    // NdotL cancels the BRDF denominator, keeping the grazing evaluation bounded without fake illumination.
    return color.rgb*fresnel*distribution*maskingL*maskingV/max(4.0*nDotV,0.00001)*visibility;
}
)"+R"(
// Projects an interior receiver ray onto the authored room box; the caller rejects exterior receivers.
float3 ReflectionProbeDirection(float3 position,float3 direction) {
    if(any(position<ReflectionProbeMinimum.xyz) || any(position>ReflectionProbeMaximum.xyz)) return direction;
    float distance=3.402823466e+38;
    [unroll] for(int axis=0;axis<3;++axis) {
        if(abs(direction[axis])>0.000001) {
            float boundary=direction[axis]>0?ReflectionProbeMaximum[axis]:ReflectionProbeMinimum[axis];
            distance=min(distance,max(0,(boundary-position[axis])/direction[axis]));
        }
    }
    float3 projected=position+direction*distance-ReflectionProbePosition.xyz;
    return dot(projected,projected)>0.00000001?projected:direction;
}
// Uses the baked GGX radiance and independently integrated single-scatter Fresnel response without exposure, pearl or wallpaper controls.
float3 EvaluateMetallicProbe(float3 position,float3 normal,float3 viewDirection,float3 baseColor,float roughness) {
    if(ReflectionProbeState.x<0.5 || any(position<ReflectionProbeMinimum.xyz) || any(position>ReflectionProbeMaximum.xyz)) return 0;
    float nv=saturate(dot(normal,viewDirection));
    if(nv<=0) return 0;
    float3 direction=ReflectionProbeDirection(position,reflect(-viewDirection,normal));
    float3 radiance=ReflectionProbeCube.SampleLevel(ReflectionProbeSampler,direction,saturate(roughness)*ReflectionProbeState.y).rgb;
    if(InkState.x>0.5) return min(radiance,float3(2,2,2))*lerp(saturate(baseColor),float3(1,1,1),0.15)*0.65;
#if defined(HELENGINE_LIGHTMAP)
    float2 brdf=ReflectionProbeBrdf.SampleLevel(ReflectionProbeSampler,float2(nv,saturate(roughness)),0);
    return radiance*(saturate(baseColor)*brdf.x+brdf.y);
#else
    return 0;
#endif
}
GeometryOutput ShadeGeometryCore(MeshOutput input, bool pearlSurface, bool bakedSurface) {
    GeometryOutput output;
    output.DiffuseIndirect=0; output.AuxiliaryPart=0;
    float4 surface = DiffuseTexture.Sample(DiffuseSampler, input.UV) * BaseColor;
    clip(surface.a - 0.001);
    float3 normal = normalize(input.Normal);
    if(
#if defined(HELENGINE_LIGHTMAP)
    !bakedSurface &&
#endif
    SsgiSize.z>0.5 && SsgiSize.w>0.5) {
        output.Color=float4(SsgiDisplay(SampleSharedSsgi(input.WorldPosition,normal))*surface.a,surface.a);
        output.Part=0;output.Coverage=1;output.Emission=0;output.GeometryPosition=float4(input.WorldPosition,1);output.GeometryNormal=float4(normal,0);return output;
    }
#if defined(HELENGINE_VOXEL_GI)
    if(
#if defined(HELENGINE_LIGHTMAP)
    !bakedSurface &&
#endif
    GiCellSize.w>0.5) {
        float3 indirect=SampleSceneGi(input.WorldPosition,normal)/3.14159265;
        output.Color=float4(indirect*surface.a,surface.a);
        output.Part=0;output.Coverage=1;output.Emission=0;output.GeometryPosition=float4(input.WorldPosition,1);output.GeometryNormal=float4(normal,0);return output;
    }
#endif
    float visibility0=LightMetadata.x>0.5?InkShadowVisibility(input.WorldPosition,ShadowRect0,ShadowSlot0,ShadowMatrix0,Light0Position.xyz,0):1;
    float visibility1=LightMetadata.x>1.5?InkShadowVisibility(input.WorldPosition,ShadowRect1,ShadowSlot1,ShadowMatrix1,Light1Position.xyz,1):1;
    float visibility2=LightMetadata.x>2.5?InkShadowVisibility(input.WorldPosition,ShadowRect2,ShadowSlot2,ShadowMatrix2,Light2Position.xyz,2):1;
    float visibility3=LightMetadata.x>3.5?InkShadowVisibility(input.WorldPosition,ShadowRect3,ShadowSlot3,ShadowMatrix3,Light3Position.xyz,3):1;
    float3 lighting;
    lighting = InkState.x>0.5 ? InkAmbient.rgb : Ambient.rgb+SampleSharedSsgi(input.WorldPosition,normal)/3.14159265;
#if defined(HELENGINE_VOXEL_GI)
    if(InkState.x<0.5) lighting += SampleSceneGi(input.WorldPosition, normal) / 3.14159265;
#endif
    float3 diffuseIndirect=lighting;
    if (LightMetadata.x > 0.5) lighting += EvaluateLight(normal, input.WorldPosition, Light0Color, Light0Direction, Light0Position, Light0Spot,visibility0);
    if (LightMetadata.x > 1.5) lighting += EvaluateLight(normal, input.WorldPosition, Light1Color, Light1Direction, Light1Position, Light1Spot,visibility1);
    if (LightMetadata.x > 2.5) lighting += EvaluateLight(normal, input.WorldPosition, Light2Color, Light2Direction, Light2Position, Light2Spot,visibility2);
    if (LightMetadata.x > 3.5) lighting += EvaluateLight(normal, input.WorldPosition, Light3Color, Light3Direction, Light3Position, Light3Spot,visibility3);
#if defined(HELENGINE_LIGHTMAP)
    lighting = bakedSurface ? LightmapAtlas.SampleLevel(LightmapSampler,input.LightmapUV,0).rgb/3.14159265 : lighting;
#endif
#if defined(HELENGINE_LIGHTMAP)
    if(bakedSurface) diffuseIndirect=lighting;
#endif
    float3 emission = MaterialFlags.y > 0.5
        ? EmissiveTexture.Sample(EmissiveSampler, input.UV).rgb * (EmissiveColor.a > 0.0001 ? EmissiveColor.rgb * EmissiveColor.a : float3(1,1,1))
        : EmissiveColor.rgb * EmissiveColor.a;
    float3 reflection=0;
    if(InkState.x<0.5 && Effects.x>0 && Material.x>0) {
        float3 viewDirection=normalize(CameraPosition.xyz-input.WorldPosition);
        float3 reflected=reflect(-viewDirection,normal);
        float2 uv=float2(atan2(reflected.x,reflected.z)/6.2831853+0.5,acos(clamp(reflected.y,-1.0,1.0))/3.14159265);
        float roughness=saturate(Roughness.x*RoughnessTexture.Sample(RoughnessSampler,input.UV).r);
        reflection=EnvironmentTexture.SampleLevel(EffectSampler,uv,roughness*Material.z).rgb*Effects.x*Material.x;
    }
    float3 conductorBaseColor=surface.rgb;
    float3 specular=0;
    if(pearlSurface && InkState.x<0.5) {
        float3 viewDirection=PearlSafeDirection(CameraPosition.xyz-input.WorldPosition);
        float roughness=saturate(Roughness.x*RoughnessTexture.Sample(RoughnessSampler,input.UV).r);
        surface.rgb=PearlCoatedColor(surface.rgb,normal,viewDirection,Pearl.x);
        if(LightMetadata.x>0.5) specular+=EvaluatePearlLight(normal,input.WorldPosition,viewDirection,roughness,Light0Color,Light0Direction,Light0Position,Light0Spot,visibility0);
        if(LightMetadata.x>1.5) specular+=EvaluatePearlLight(normal,input.WorldPosition,viewDirection,roughness,Light1Color,Light1Direction,Light1Position,Light1Spot,visibility1);
        if(LightMetadata.x>2.5) specular+=EvaluatePearlLight(normal,input.WorldPosition,viewDirection,roughness,Light2Color,Light2Direction,Light2Position,Light2Spot,visibility2);
        if(LightMetadata.x>3.5) specular+=EvaluatePearlLight(normal,input.WorldPosition,viewDirection,roughness,Light3Color,Light3Direction,Light3Position,Light3Spot,visibility3);
    }
    float metallic=saturate(Pearl.w);
    reflection*=1.0-metallic;specular*=1.0-metallic;
    if(metallic>0) {
        float3 eyeDelta=CameraPosition.xyz-input.WorldPosition;
        float eyeLengthSquared=dot(eyeDelta,eyeDelta);
        if(eyeLengthSquared>0.00000001) {
            float3 viewDirection=eyeDelta*rsqrt(eyeLengthSquared);
            float roughness=saturate(Roughness.x*RoughnessTexture.Sample(RoughnessSampler,input.UV).r);
            float3 conductor=0;
            if(InkState.x<0.5 || InkState.y<0.5) conductor+=EvaluateMetallicProbe(input.WorldPosition,normal,viewDirection,conductorBaseColor,roughness);
            if(InkState.x>0.5) conductor+=InkAmbient.rgb*saturate(conductorBaseColor)*0.4;
            if(LightMetadata.x>0.5) conductor+=EvaluateMetallicSun(normal,viewDirection,conductorBaseColor,roughness,Light0Color,Light0Direction,visibility0);
            if(LightMetadata.x>1.5) conductor+=EvaluateMetallicSun(normal,viewDirection,conductorBaseColor,roughness,Light1Color,Light1Direction,visibility1);
            if(LightMetadata.x>2.5) conductor+=EvaluateMetallicSun(normal,viewDirection,conductorBaseColor,roughness,Light2Color,Light2Direction,visibility2);
            if(LightMetadata.x>3.5) conductor+=EvaluateMetallicSun(normal,viewDirection,conductorBaseColor,roughness,Light3Color,Light3Direction,visibility3);
            specular+=conductor*metallic;
        }
    }
    output.DiffuseIndirect=float4(max(surface.rgb*diffuseIndirect*(1.0-metallic),0)*surface.a,surface.a);
    // Saved lightmaps remain a diffuse irradiance approximation; metals suppress it rather than adding baked sun twice.
    output.Color = float4(max(surface.rgb * lighting * (1.0-metallic) + specular + reflection + emission,0) * surface.a, surface.a);
    output.Part = SurfaceStyle.x>0.5 && SurfaceStyle.y>0 ? (uint)Style.w : 0;

    output.GeometryPosition = float4(input.WorldPosition,1);
    output.GeometryNormal = float4(normal,SurfaceStyle.x>0.5 ? SurfaceStyle.y : 0);
    output.Coverage = 1.0;
    float3 selected=max(emission-Effects.z,0)*Material.y*surface.a;
    output.Emission=float4(selected,max(selected.r,max(selected.g,selected.b)));
    return output;
}
// Emits only auxiliary contour ownership; surface color, emission and lighting buffers stay untouched.
GeometryOutput OutlineOnlyGeometryPS(MeshOutput input) {
    if(SurfaceStyle.x<0.5 || SurfaceStyle.y<=0 || MaterialFlags.z<0.5) discard;
    GeometryOutput output=(GeometryOutput)0;
    output.AuxiliaryPart=uint2(0x00800000u | (uint)Style.w | ((uint)max(1.0,round(saturate(SurfaceStyle.y/4.0)*255.0))<<24),asuint(input.Position.z));
    return output;
}
GeometryOutput ShadeGeometry(MeshOutput input, bool pearlSurface) { return ShadeGeometryCore(input,pearlSurface,false); }
#if defined(HELENGINE_LIGHTMAP)
GeometryOutput LightmapGeometryPS(MeshOutput input) { return ShadeGeometryCore(input,false,true); }
GeometryOutput LightmapPearlGeometryPS(MeshOutput input) { return ShadeGeometryCore(input,true,true); }
#endif
GeometryOutput GeometryPS(MeshOutput input) { return ShadeGeometry(input,false); }
GeometryOutput PearlGeometryPS(MeshOutput input) { return ShadeGeometry(input,true); }
)";
        /// Keeps the fullscreen contour and bloom entries separate from material shading source.
    inline const std::string CameraToonCompositeShader = std::string(DirectX11OutputColorShader)+R"(
float4 CompositeVS(uint id : SV_VertexID) : SV_POSITION {
    float2 positions[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
    return float4(positions[id], 0, 1);
}
// Integer sample identities are compared before their coverage is averaged.
uint4 LoadAuxiliaryParts(int2 position) {
    if(any(position<0) || any(position>=int2(Size.xy))) return 0;
#if TOON_SAMPLES == 4
    return uint4(AuxiliaryPartTexture.Load(position,0).x,AuxiliaryPartTexture.Load(position,1).x,AuxiliaryPartTexture.Load(position,2).x,AuxiliaryPartTexture.Load(position,3).x);
#else
    return AuxiliaryPartTexture.Load(int3(position,0)).xxxx;
#endif
}
uint4 LoadParts(int2 position) {
#if TOON_AUXILIARY_OUTLINE
    return LoadAuxiliaryParts(position) & 0x007fffff;
#else
    if (any(position < 0) || any(position >= int2(Size.xy))) return 0;
#if TOON_SAMPLES == 4
    return uint4(PartTexture.Load(position,0),PartTexture.Load(position,1),
        PartTexture.Load(position,2),PartTexture.Load(position,3));
#else
    return PartTexture.Load(int3(position,0)).xxxx;
#endif
#endif
}
// Geometric contour ownership is independent from authored transparency and room coverage.
float LoadOutlineScale(int2 pixel) {
    if(any(pixel<0) || any(pixel>=int2(Size.xy))) return 0;
    uint4 parts=LoadParts(pixel);
    float coverage=dot(float4(parts!=0),float4(0.25,0.25,0.25,0.25));
    #if TOON_AUXILIARY_OUTLINE
    uint4 widths=LoadAuxiliaryParts(pixel)>>24;
    return max(max(widths.x,widths.y),max(widths.z,widths.w))*(4.0/255.0);
#else
    return coverage>0 ? GeometryNormalTexture.Load(int3(pixel,0)).w/coverage : 0;
#endif
}
float LoadOutlineCoverage(int2 pixel) {
    uint4 parts=LoadParts(pixel);
    return dot(float4(parts!=0),float4(0.25,0.25,0.25,0.25));
}
// Reads the visible depth independently of mesh identity; auxiliary proxies contribute only to their own pass.
float OutlineDeviceDepth(int2 pixel,int sampleIndex) {
    pixel=clamp(pixel,int2(0,0),int2(Size.xy)-1);
#if TOON_SAMPLES == 4
    float depth=ContourDepthTexture.Load(pixel,sampleIndex);
#if TOON_AUXILIARY_OUTLINE
    uint2 proxy=AuxiliaryPartTexture.Load(pixel,sampleIndex);
#else
    uint2 proxy=0;
#endif
#else
    float depth=ContourDepthTexture.Load(int3(pixel,0));
#if TOON_AUXILIARY_OUTLINE
    uint2 proxy=AuxiliaryPartTexture.Load(int3(pixel,0));
#else
    uint2 proxy=0;
#endif
#endif
    return (proxy.x & 0x007fffff)!=0 ? min(depth,asfloat(proxy.y)) : depth;
}
// Perspective depth is reconstructed before measuring relative jumps; identity projection is used by clip-space fixtures.
float OutlineLinearDepth(float depth) {
    return DepthProjection.x>0 ? DepthProjection.x*DepthProjection.y/
        max(DepthProjection.y-depth*(DepthProjection.y-DepthProjection.x),.0000001) : depth;
}
// Opposite taps estimate the continuous slope. Removing it prevents smooth slanted walls from becoming ink.
float2 UnilateralJump(float a,float b) {
    float slope=a*b<0 ? min(abs(a),abs(b)) : 0;
    return max(float2(a,b)-slope,0);
}
// Reuses the resolved normal attachment for four bounded crease taps, only inside one fully covered eligible mesh.
float NormalCreaseOutline(int2 pixel,float width) {
#if TOON_AUXILIARY_OUTLINE
    return 0;
#else
    if(Crease.y<.5 || width<=0) return 0;
    uint4 centerParts=LoadParts(pixel);
    if(centerParts.x==0 || any(centerParts!=centerParts.xxxx)) return 0;
    float3 center=GeometryNormalTexture.Load(int3(pixel,0)).xyz;
    if(dot(center,center)<.5) return 0;
    center=normalize(center);
    int radius=max(1,(int)round(width*.5));
    const int2 directions[4]={int2(-1,0),int2(1,0),int2(0,-1),int2(0,1)};
    float crease=0;
    [unroll] for(int tap=0;tap<4;++tap) {
        int2 neighbor=pixel+directions[tap]*radius;
        uint4 parts=LoadParts(neighbor);
        if(any(parts!=centerParts.xxxx)) continue;
        float4 normal=GeometryNormalTexture.Load(int3(neighbor,0));
        if(normal.w<=0 || dot(normal.xyz,normal.xyz)<.5) continue;
        // Depth discontinuities belong to Sobel; normals must not connect two disconnected surfaces of one mesh.
        float a=OutlineLinearDepth(OutlineDeviceDepth(pixel,0));
        float b=OutlineLinearDepth(OutlineDeviceDepth(neighbor,0));
        if(abs(a-b)>max(a,.01)*.03) continue;
        float cosine=dot(center,normalize(normal.xyz));
        crease=max(crease,1-smoothstep(Crease.x-.01,Crease.x,cosine));
    }
    return crease*min(width*.5,1);
#endif
}
// A 3x3 unilateral Sobel draws on the nearer surface only, including openings within a single mesh.
float DepthSobelOutline(int2 pixel) {
    float radius=min(Style.x*LoadOutlineScale(pixel),16.0);
    if(radius<=0) return 0;
    int step=max(1,(int)round(radius));
    uint4 parts=LoadParts(pixel);float stroke=0;
    float crease=NormalCreaseOutline(pixel,radius);
    uint4 leftParts=LoadParts(pixel+int2(-step,0)),rightParts=LoadParts(pixel+int2(step,0));
    uint4 topParts=LoadParts(pixel+int2(0,-step)),bottomParts=LoadParts(pixel+int2(0,step));
    [unroll] for(int sampleIndex=0;sampleIndex<TOON_SAMPLES;++sampleIndex) {
        if(parts[sampleIndex]==0) continue;
        float deviceDepth=OutlineDeviceDepth(pixel,sampleIndex);
#if TOON_SAMPLES == 4
        uint2 proxy=AuxiliaryPartTexture.Load(pixel,sampleIndex);
#else
        uint2 proxy=AuxiliaryPartTexture.Load(int3(pixel,0));
#endif
#if !TOON_AUXILIARY_OUTLINE
        if((proxy.x & 0x00800000u)!=0 && asfloat(proxy.y)<deviceDepth-.000001) continue;
#endif
        float center=OutlineLinearDepth(deviceDepth);
        float left=OutlineLinearDepth(OutlineDeviceDepth(pixel+int2(-step,0),sampleIndex))-center;
        float right=OutlineLinearDepth(OutlineDeviceDepth(pixel+int2(step,0),sampleIndex))-center;
        float top=OutlineLinearDepth(OutlineDeviceDepth(pixel+int2(0,-step),sampleIndex))-center;
        float bottom=OutlineLinearDepth(OutlineDeviceDepth(pixel+int2(0,step),sampleIndex))-center;
        float tl=OutlineLinearDepth(OutlineDeviceDepth(pixel+int2(-step,-step),sampleIndex))-center;
        float tr=OutlineLinearDepth(OutlineDeviceDepth(pixel+int2(step,-step),sampleIndex))-center;
        float bl=OutlineLinearDepth(OutlineDeviceDepth(pixel+int2(-step,step),sampleIndex))-center;
        float br=OutlineLinearDepth(OutlineDeviceDepth(pixel+int2(step,step),sampleIndex))-center;
        float2 horizontal=UnilateralJump(left,right),vertical=UnilateralJump(top,bottom);
        float2 diagonal=UnilateralJump(tl,br),other=UnilateralJump(tr,bl);
        float2 gradient=float2(2*(horizontal.y-horizontal.x)+other.x+diagonal.y-diagonal.x-other.y,
            2*(vertical.y-vertical.x)+other.y+diagonal.y-diagonal.x-other.x);
        float edge=length(gradient)/(4*max(center,.01));
        // A real mesh boundary can have a tiny depth gap (cloth against a wall).
        // IDs only select sensitivity: coplanar boundaries still have zero depth response.
        uint owner=parts[sampleIndex];
        bool boundary=leftParts[sampleIndex]!=owner || rightParts[sampleIndex]!=owner
            || topParts[sampleIndex]!=owner || bottomParts[sampleIndex]!=owner;
        float depthInk=boundary ? smoothstep(.0005,.001,edge) : smoothstep(.015,.03,edge);
        stroke+=max(depthInk,crease)/TOON_SAMPLES;
    }
    return saturate(stroke);
}
// Tiles only mask participation; depth edges may occur anywhere within one mesh.
float OutlineTilePS(float4 position : SV_POSITION) : SV_Target {
    int2 origin=int2(position.xy)*32;
    [loop] for(int y=0;y<32;++y) [loop] for(int x=0;x<32;++x) {
        int2 pixel=origin+int2(x,y);
        if(all(pixel<int2(Size.xy)) && any(LoadParts(pixel)!=0)) return 1;
    }
    return 0;
}
// Sixteen-pixel stroke bounds require at most one adjacent thirty-two-pixel tile in every direction.
bool HasNearbyOutline(int2 pixel) {
    if(SurfaceStyle.w<0.5 || (Style.x<=0 && Style.y<=0)) return false;
    int2 tile=pixel/32;
    int2 dimensions=(int2(Size.xy)+31)/32;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        int2 neighbor=tile+int2(x,y);
        if(all(neighbor>=0) && all(neighbor<dimensions) && OutlineTileTexture.Load(int3(neighbor,0))>0) return true;
    }
    return false;
}
// Coverage stays independent of native HDR highlight luminance.
float3 SdrDisplayRadiance(float3 linearColor) {
    if(Display.x<0.5) return saturate(linearColor);
    float3 value=max(linearColor*Display.y,0);
    // HDR calls this helper only for its existing bloom coverage, whose policy stays unchanged.
    if(Display.z>0.5) value=saturate((value*(2.51*value+0.03))/(value*(2.43*value+0.59)+0.14));
    else return OutputSceneSdr(value);
    return float3(value.r<=0.0031308 ? value.r*12.92 : 1.055*pow(value.r,1.0/2.4)-0.055,
        value.g<=0.0031308 ? value.g*12.92 : 1.055*pow(value.g,1.0/2.4)-0.055,
        value.b<=0.0031308 ? value.b*12.92 : 1.055*pow(value.b,1.0/2.4)-0.055);
}
// Converts straight scene color before premultiplication; Windows UI white never scales the scene.
float3 DisplayRadiance(float3 linearColor) {
    if(Display.z>0.5) return OutputSceneScRgb(linearColor*Display.y);
    return SdrDisplayRadiance(linearColor);
}
// Selection uses the union coverage of every proxy part, so normals, materials and internal edges cannot add ink.
float4 SelectionOutline(int2 pixel) {
    if(pixel.x<SelectionBounds.x || pixel.y<SelectionBounds.y || pixel.x>=SelectionBounds.z || pixel.y>=SelectionBounds.w) return 0;
    float center=CoverageTexture.Load(int3(pixel,0));
    if(center>=1) return 0;
    float2 filtered=FilteredGlow.SampleLevel(EffectSampler,(float2(pixel)+0.5)/Size.xy,0).rg;
    float border=saturate(filtered.r-center);
    float halo=saturate(filtered.g*0.65)*(1-center);
    float alpha=border+halo*(1-border);
    float3 accent=Display.z>0.5 ? OutputUiScRgb(SelectionColor.rgb,1) : SelectionColor.rgb;
    return float4(accent*alpha,alpha);
}
float4 CompositePS(float4 position : SV_POSITION) : SV_Target {
    int2 pixel=int2(position.xy-Size.zw);
    if(SelectionColor.w>0) return SelectionOutline(pixel);
    float4 color=ColorTexture.Load(int3(pixel,0));
    if(SsgiSize.z>0.5 && SsgiSize.w>0.5) return color;
#if defined(HELENGINE_VOXEL_GI)
    if(GiCellSize.w>0.5) return color;
#endif
    if(Occlusion.w>0 && Occlusion.x>0 && Occlusion.z>0)
        color.rgb=max(color.rgb-DiffuseIndirectTexture.Load(int3(pixel,0)).rgb*(1-AmbientVisibilityTexture.Load(int3(pixel,0)))*Occlusion.w,0);
    float3 glow=0;
    if(Effects.y>0) glow=max(FilteredGlow.SampleLevel(EffectSampler,(float2(pixel)+0.5)/Size.xy,0).rgb*Effects.y,0);
    float3 displayedGlow=SdrDisplayRadiance(glow);
    float glowAlpha=max(displayedGlow.r,max(displayedGlow.g,displayedGlow.b));
    float alpha=saturate(color.a+glowAlpha*(1-color.a));
    float3 radiance=color.rgb+glow;
    float4 outlined=float4(alpha>0 ? DisplayRadiance(radiance/max(alpha,0.00001))*alpha : 0,alpha);
    if(!HasNearbyOutline(pixel)) return outlined;
    const float3 ink=Display.z>0.5 ? OutputSrgbToLinear(float3(0.012,0.016,0.022)) : float3(0.012,0.016,0.022);
    float coverage=LoadOutlineCoverage(pixel);
    float external=DepthSobelOutline(pixel);
    // Ink replaces covered foreground samples; it never expands alpha into the background.
    float replaced=external/max(outlined.a,.00001);
    outlined=float4(ink*external+outlined.rgb*(1-replaced),outlined.a);
    return outlined;
}
// Only ink is composed here: transparent interiors and opaque contours remain untouched.
float4 AuxiliaryOutlinePS(float4 position : SV_POSITION) : SV_Target {
    int2 pixel=int2(position.xy-Size.zw);
    if(!HasNearbyOutline(pixel)) return 0;
    float coverage=LoadOutlineCoverage(pixel);
    float stroke=DepthSobelOutline(pixel);
    const float3 ink=Display.z>0.5 ? OutputSrgbToLinear(float3(0.012,0.016,0.022)) : float3(0.012,0.016,0.022);
    return float4(ink*stroke,stroke);
}
float4 BlurPS(float4 position : SV_POSITION) : SV_Target {
    float2 uv=position.xy/ceil(Size.xy*0.5);
    // Reuse the bloom pair: red dilates coverage, green carries a softer exterior halo.
    if(SelectionColor.w>0) {
        float2 pixel=uv*Size.xy;
        if(any(pixel<SelectionBounds.xy) || any(pixel>=SelectionBounds.zw)) return 0;
        bool horizontal=Blur.x>0;
        float2 axis=horizontal ? float2(1/Size.x,0) : float2(0,1/Size.y);
        float expanded=0, halo=0;
        int radius=(int)ceil(SelectionColor.w);
        for(int offset=-radius;offset<=radius;++offset) {
            float2 sampleUv=uv+axis*offset;
            if(all(sampleUv>=0) && all(sampleUv<=1))
                expanded=max(expanded,GlowSource.SampleLevel(EffectSampler,sampleUv,0).r);
        }
        [unroll] for(int tap=-8;tap<=8;++tap) {
            float2 sampleUv=uv+float2(Blur.x,Blur.y)*(tap/8.0);
            if(all(sampleUv>=0) && all(sampleUv<=1)) {
                float2 source=GlowSource.SampleLevel(EffectSampler,sampleUv,0).rg;
                halo+=(horizontal ? source.r : source.g)*(9-abs(tap));
            }
        }
        return float4(expanded,halo/81.0,0,1);
    }

    float4 total=0;
    [unroll] for(int tap=-4;tap<=4;++tap) {
        float weight=5-abs(tap);
        float2 sampleUv=uv+float2(Blur.x,Blur.y)*(tap*0.25);
        if(all(sampleUv>=0) && all(sampleUv<=1)) total+=GlowSource.SampleLevel(EffectSampler,sampleUv,0)*weight;
    }
    return total/25.0;
}
)";

}

#include "platform/windows/directx11/directx11_shader_request.hpp"
#include "platform/windows/win32/win32_camera_gtao_shader.hpp"
namespace helengine::windows {
    /// Describes this effect's existing variants for the common asynchronous DirectX11 compiler.
    inline std::vector<DirectX11ShaderRequest> BuildCameraToonShaderRequests(const std::string& source) {
        const char* entries[]={"GeometryVS","GeometryPS","PearlGeometryPS","CompositeVS","CompositePS","CompositePS","BlurPS","OutlineTilePS","OutlineTilePS"};
        std::vector<DirectX11ShaderRequest> requests;
        for(std::size_t entry=0;entry<std::size(entries);++entry) {
            requests.push_back({source,"CameraToon",entries[entry],entry==0 || entry==3 ? "vs_5_0" : "ps_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES",entry==5 || entry==8 ? "4" : "1"}}});
        }
#ifdef HELENGINE_LIGHTMAP
        requests.push_back({source,"CameraToon","LightmapGeometryPS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES","1"}}});
        requests.push_back({source,"CameraToon","LightmapPearlGeometryPS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES","1"}}});
        requests.push_back({source,"CameraToon","LightmapGeometryVS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES","1"}}});
#endif
        requests.push_back({source,"CameraToon","OutlineOnlyGeometryPS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES","1"}}});
        for(const char* entry : {"AuxiliaryOutlinePS","OutlineTilePS"})
            for(const char* samples : {"1","4"})
                requests.push_back({source,"CameraToon",entry,"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES",samples},{"TOON_AUXILIARY_OUTLINE","1"}}});
        requests.push_back({source+CameraGtaoShader,"CameraToon","GtaoPS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES","1"}}});
        requests.push_back({source+CameraGtaoShader,"CameraToon","GtaoFilterPS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES","1"}}});
        return requests;
    }
}
