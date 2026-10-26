#pragma once

namespace helengine::windows {
        /// Implements material-preserving stepped lighting and geometric outlines without a source-image effect.
    inline constexpr const char* CameraToonGeometryShader = R"(
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
cbuffer ToonBuffer : register(b8) { float4 Style; float4 Size; float4 Effects; float4 Material; float4 Shadow; float4 Blur; float4 Pearl; };
cbuffer ShadowBuffer : register(b2) {
    float4 ShadowMetadata;
    float4 ShadowRect0; float4 ShadowSlot0; float4x4 ShadowMatrix0;
    float4 ShadowRect1; float4 ShadowSlot1; float4x4 ShadowMatrix1;
    float4 ShadowRect2; float4 ShadowSlot2; float4x4 ShadowMatrix2;
    float4 ShadowRect3; float4 ShadowSlot3; float4x4 ShadowMatrix3;
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
#else
Texture2D<uint> PartTexture : register(t9);
#endif
Texture2D<float> CoverageTexture : register(t10);
Texture2D GlowSource : register(t11);
Texture2D EnvironmentTexture : register(t12);
Texture2D FilteredGlow : register(t13);
SamplerState EffectSampler : register(s8);
struct MeshInput { float3 Position : POSITION; float3 Normal : NORMAL; float2 UV : TEXCOORD0; };
struct MeshOutput { float4 Position : SV_POSITION; float3 WorldPosition : TEXCOORD0; float3 Normal : TEXCOORD1; float2 UV : TEXCOORD2; };
struct GeometryOutput { float4 Color : SV_Target0; uint Part : SV_Target1; float Coverage : SV_Target2; float4 Emission : SV_Target3; };
MeshOutput GeometryVS(MeshInput input) {
    MeshOutput output;
    output.Position = mul(float4(input.Position, 1.0), WorldViewProjection);
    output.WorldPosition = mul(float4(input.Position, 1.0), World).xyz;
    output.Normal = mul(float4(input.Normal, 0.0), WorldNormal).xyz;
    output.UV = input.UV;
    return output;
}
float ShadowVisibility(float3 position, float4 rect, float4 metadata, float4x4 worldToShadowClip) {
    if (Shadow.x<=0 || MaterialFlags.x<0.5 || ShadowMetadata.x<0.5 || metadata.x<0.5) return 1;
    float4 clipPosition=mul(float4(position,1),worldToShadowClip);
    if(clipPosition.w<=0) return 1;
    float3 ndc=clipPosition.xyz/clipPosition.w;
    float2 uv=float2(ndc.x*0.5+0.5,-ndc.y*0.5+0.5);
    if(any(uv<0) || any(uv>1) || ndc.z<0 || ndc.z>1) return 1;
    float2 atlasUv=rect.xy+uv*rect.zw;
    float2 texel=ShadowMetadata.yz;
    float2 lower=rect.xy+texel*0.5, upper=rect.xy+rect.zw-texel*0.5;
    float visible=0;
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        float2 tap=clamp(atlasUv+float2(x,y)*texel*Shadow.y,lower,upper);
        visible+=(ndc.z-0.01)<=ShadowAtlas.SampleLevel(ShadowSampler,tap,0) ? 1.0 : 0.0;
    }
    return lerp(1.0,visible/9.0,saturate(metadata.y*Shadow.x));
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
GeometryOutput ShadeGeometry(MeshOutput input, bool pearlSurface) {
    GeometryOutput output;
    float4 surface = DiffuseTexture.Sample(DiffuseSampler, input.UV) * BaseColor;
    clip(surface.a - 0.001);
    float3 normal = normalize(input.Normal);
    float3 lighting = Ambient.rgb;
    if (LightMetadata.x > 0.5) lighting += EvaluateLight(normal, input.WorldPosition, Light0Color, Light0Direction, Light0Position, Light0Spot,ShadowVisibility(input.WorldPosition,ShadowRect0,ShadowSlot0,ShadowMatrix0));
    if (LightMetadata.x > 1.5) lighting += EvaluateLight(normal, input.WorldPosition, Light1Color, Light1Direction, Light1Position, Light1Spot,ShadowVisibility(input.WorldPosition,ShadowRect1,ShadowSlot1,ShadowMatrix1));
    if (LightMetadata.x > 2.5) lighting += EvaluateLight(normal, input.WorldPosition, Light2Color, Light2Direction, Light2Position, Light2Spot,ShadowVisibility(input.WorldPosition,ShadowRect2,ShadowSlot2,ShadowMatrix2));
    if (LightMetadata.x > 3.5) lighting += EvaluateLight(normal, input.WorldPosition, Light3Color, Light3Direction, Light3Position, Light3Spot,ShadowVisibility(input.WorldPosition,ShadowRect3,ShadowSlot3,ShadowMatrix3));
    float3 emission = MaterialFlags.y > 0.5
        ? EmissiveTexture.Sample(EmissiveSampler, input.UV).rgb * (EmissiveColor.a > 0.0001 ? EmissiveColor.rgb * EmissiveColor.a : float3(1,1,1))
        : EmissiveColor.rgb * EmissiveColor.a;
    float3 reflection=0;
    if(Effects.x>0 && Material.x>0) {
        float3 viewDirection=normalize(CameraPosition.xyz-input.WorldPosition);
        float3 reflected=reflect(-viewDirection,normal);
        float2 uv=float2(atan2(reflected.x,reflected.z)/6.2831853+0.5,acos(clamp(reflected.y,-1.0,1.0))/3.14159265);
        float roughness=saturate(Roughness.x*RoughnessTexture.Sample(RoughnessSampler,input.UV).r);
        reflection=EnvironmentTexture.SampleLevel(EffectSampler,uv,roughness*Material.z).rgb*Effects.x*Material.x;
    }
    float3 specular=0;
    if(pearlSurface) {
        float3 viewDirection=PearlSafeDirection(CameraPosition.xyz-input.WorldPosition);
        float roughness=saturate(Roughness.x*RoughnessTexture.Sample(RoughnessSampler,input.UV).r);
        surface.rgb=PearlCoatedColor(surface.rgb,normal,viewDirection,Pearl.x);
        if(LightMetadata.x>0.5) specular+=EvaluatePearlLight(normal,input.WorldPosition,viewDirection,roughness,Light0Color,Light0Direction,Light0Position,Light0Spot,ShadowVisibility(input.WorldPosition,ShadowRect0,ShadowSlot0,ShadowMatrix0));
        if(LightMetadata.x>1.5) specular+=EvaluatePearlLight(normal,input.WorldPosition,viewDirection,roughness,Light1Color,Light1Direction,Light1Position,Light1Spot,ShadowVisibility(input.WorldPosition,ShadowRect1,ShadowSlot1,ShadowMatrix1));
        if(LightMetadata.x>2.5) specular+=EvaluatePearlLight(normal,input.WorldPosition,viewDirection,roughness,Light2Color,Light2Direction,Light2Position,Light2Spot,ShadowVisibility(input.WorldPosition,ShadowRect2,ShadowSlot2,ShadowMatrix2));
        if(LightMetadata.x>3.5) specular+=EvaluatePearlLight(normal,input.WorldPosition,viewDirection,roughness,Light3Color,Light3Direction,Light3Position,Light3Spot,ShadowVisibility(input.WorldPosition,ShadowRect3,ShadowSlot3,ShadowMatrix3));
    }
    output.Color = float4(saturate(surface.rgb * lighting + specular + reflection + emission) * surface.a, surface.a);
    output.Part = (uint)Style.w;
    output.Coverage = 1.0;
    float3 selected=max(emission-Effects.z,0)*Material.y*surface.a;
    output.Emission=float4(selected,max(selected.r,max(selected.g,selected.b)));
    return output;
}
GeometryOutput GeometryPS(MeshOutput input) { return ShadeGeometry(input,false); }
GeometryOutput PearlGeometryPS(MeshOutput input) { return ShadeGeometry(input,true); }
)";
        /// Keeps the fullscreen contour and bloom entries separate from material shading source.
    inline constexpr const char* CameraToonCompositeShader = R"(
float4 CompositeVS(uint id : SV_VertexID) : SV_POSITION {
    float2 positions[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
    return float4(positions[id], 0, 1);
}
// Integer sample identities are compared before their coverage is averaged.
uint4 LoadParts(int2 position) {
    if (any(position < 0) || any(position >= int2(Size.xy))) return 0;
#if TOON_SAMPLES == 4
    return uint4(PartTexture.Load(position,0),PartTexture.Load(position,1),
        PartTexture.Load(position,2),PartTexture.Load(position,3));
#else
    return PartTexture.Load(int3(position,0)).xxxx;
#endif
}
float PartCoverage(int2 position, uint part) {
    uint4 ids=LoadParts(position);
    return dot(float4(ids==part),float4(0.25,0.25,0.25,0.25));
}
float LoadCoverage(int2 position) {
    if (any(position < 0) || any(position >= int2(Size.xy))) return 0;
    return CoverageTexture.Load(int3(position,0));
}
// Integrates a half-plane over the square pixel footprint instead of smoothing the material image.
float PixelHalfPlane(float distance, float2 normal) {
    float2 extent=abs(normal);
    float largest=max(extent.x,extent.y), smallest=min(extent.x,extent.y);
    if (smallest<0.0001) return saturate(distance/max(largest,0.0001)+0.5);
    float t=distance+(largest+smallest)*0.5;
    float4 positive=max(float4(t,t-largest,t-smallest,t-largest-smallest),0.0);
    return saturate(dot(positive*positive,float4(1,-1,-1,1))/(2.0*largest*smallest));
}
// Inverts the same footprint for an exact resolved coverage value; all arithmetic stays pixel-local.
float CoverageDistance(float coverage, float2 normal) {
    float2 extent=abs(normal);
    float largest=max(extent.x,extent.y), smallest=min(extent.x,extent.y);
    if (smallest<0.0001) return (coverage-0.5)*largest;
    float tail=min(coverage,1.0-coverage);
    float distance=tail<smallest/(2.0*largest)
        ? sqrt(2.0*largest*smallest*tail)-(largest+smallest)*0.5
        : (tail-0.5)*largest;
    return coverage<=0.5 ? distance : -distance;
}
// Reconstructs an observed part interface across two neighboring pixel footprints.
// The 2x3 derivative follows this interface without mixing the opposite side of a thin part.
float InterfaceStroke(int2 pixel, int2 seed, int2 axis, uint part) {
    int2 tangent=int2(-axis.y,axis.x);
    float first=PartCoverage(seed,part), second=PartCoverage(seed+axis,part);
    float firstAbove=PartCoverage(seed-tangent,part), secondAbove=PartCoverage(seed+axis-tangent,part);
    float firstBelow=PartCoverage(seed+tangent,part), secondBelow=PartCoverage(seed+axis+tangent,part);
    float across=(secondAbove-firstAbove+2.0*(second-first)+secondBelow-firstBelow)*0.25;
    float along=(firstBelow+secondBelow-firstAbove-secondAbove)*0.25;
    float2 gradient=float2(axis)*across+float2(tangent)*along;
    float squared=dot(gradient,gradient);
    if (squared<0.0001) {
        gradient=float2(axis)*(second-first);
        squared=dot(gradient,gradient);
        if (squared<0.0001) return 0;
    }
    float2 normal=gradient*rsqrt(squared);
    float2 delta=float2(pixel-seed)-float2(axis)*0.5;
    // Local support prevents extending a remote collinear interface through another piece.
    if (abs(dot(delta,float2(-normal.y,normal.x)))>1.5) return 0;
    float2 extent=float2(1+abs(axis.x),1+abs(axis.y));
    float distance=dot(delta,normal)+CoverageDistance((first+second)*0.5,normal*extent);
    float radius=Style.y*0.5;
    return PixelHalfPlane(radius-distance,normal)-PixelHalfPlane(-radius-distance,normal);
}
// Includes minority parts in mixed MSAA pixels while ignoring the reserved background identity.
float PartInterface(int2 pixel, int2 seed, int2 axis, out bool found) {
    uint4 first=LoadParts(seed), second=LoadParts(seed+axis);
    found=false;
    float stroke=0;
    [unroll] for (int sampleIndex=0;sampleIndex<TOON_SAMPLES;++sampleIndex) {
        uint part=first[sampleIndex];
        if (part==0) continue;
        bool duplicate=false;
        [unroll] for (int previous=0;previous<TOON_SAMPLES;++previous) {
            if (previous<sampleIndex && first[previous]==part) duplicate=true;
        }
        if (duplicate) continue;
        bool different=false;
        [unroll] for (int neighbor=0;neighbor<TOON_SAMPLES;++neighbor) {
            if (second[neighbor]!=0 && second[neighbor]!=part) different=true;
        }
        if (different) {
            found=true;
            stroke=max(stroke,InterfaceStroke(pixel,seed,axis,part));
        }
    }
    return stroke;
}
float InternalStroke(int2 pixel) {
    if (Style.y<=0.0) return 0;
    int limit=(int)ceil(Style.y*0.5)+1;
    float stroke=0;
#if TOON_SAMPLES == 1
    float nearest=1.0e20;
#endif
    [loop] for (int y=-limit;y<=limit;++y) {
        [loop] for (int x=-limit;x<=limit;++x) {
            int2 seed=pixel+int2(x,y);
            [unroll] for (int direction=0;direction<2;++direction) {
                int2 axis=direction==0 ? int2(1,0) : int2(0,1);
#if TOON_SAMPLES == 1
                float2 delta=float2(x,y)+float2(axis)*0.5;
                float squared=dot(delta,delta);
                if (squared>=nearest) continue;
#endif
                bool found;
                float candidate=PartInterface(pixel,seed,axis,found);
#if TOON_SAMPLES == 1
                // A binary staircase supplies alternative estimates of one boundary, not separate strokes.
                // Select its nearest observed anchor rather than unioning inconsistent quantized planes.
                if (found) { nearest=squared; stroke=candidate; }
#else
                stroke=max(stroke,candidate);
#endif
            }
        }
    }
    return saturate(stroke);
}
float ExpandedSilhouette(int2 pixel, float centerCoverage) {
    if (Style.x<=0.0 || centerCoverage>=1.0) return 0;
    int limit=(int)ceil(Style.x)+1;
    float expanded=centerCoverage;
    [loop] for (int y=-limit; y<=limit; ++y) {
        [loop] for (int x=-limit; x<=limit; ++x) {
            int2 seed=pixel+int2(x,y);
            float coverage=LoadCoverage(seed);
            if (coverage<=0.0) continue;
            float left=LoadCoverage(seed+int2(-1,0)), right=LoadCoverage(seed+int2(1,0));
            float top=LoadCoverage(seed+int2(0,-1)), bottom=LoadCoverage(seed+int2(0,1));
            if (min(min(left,right),min(top,bottom))>=1.0) continue;
            float topLeft=LoadCoverage(seed+int2(-1,-1)), topRight=LoadCoverage(seed+int2(1,-1));
            float bottomLeft=LoadCoverage(seed+int2(-1,1)), bottomRight=LoadCoverage(seed+int2(1,1));
            float2 gradient=float2(2.0*(right-left)+topRight+bottomRight-topLeft-bottomLeft,
                2.0*(bottom-top)+bottomLeft+bottomRight-topLeft-topRight);
            float squared=dot(gradient,gradient);
            if (squared<0.0001) continue;
            float2 normal=gradient*rsqrt(squared);
            float2 delta=-float2(x,y);
            if (abs(dot(delta,float2(-normal.y,normal.x)))>1.5) continue;
            // Binary sample masks place the boundary halfway to the neighboring uncovered pixel.
            float distance=coverage>=1.0 ? 0.5*max(abs(normal.x),abs(normal.y)) : CoverageDistance(coverage,normal);
            expanded=max(expanded,PixelHalfPlane(dot(delta,normal)+distance+Style.x,normal));
        }
    }
    return saturate(expanded-centerCoverage);
}
float4 CompositePS(float4 position : SV_POSITION) : SV_Target {
    int2 pixel = int2(position.xy - Size.zw);
    float4 color = ColorTexture.Load(int3(pixel, 0));
    float coverage=LoadCoverage(pixel);
    const float4 ink = float4(0.012, 0.016, 0.022, 1.0);
    float internal=coverage>0.0 && Material.w>0.5 ? InternalStroke(pixel) : 0.0;
    // Internal ink keeps authored surface opacity and geometric sample coverage.
    float4 outlined=float4(lerp(color.rgb,ink.rgb*color.a,internal),color.a);
    outlined+=ink*(Material.w>0.5 ? ExpandedSilhouette(pixel,coverage) : 0.0);
    if(Effects.y>0) {
        float3 glow=saturate(FilteredGlow.SampleLevel(EffectSampler,(float2(pixel)+0.5)/Size.xy,0).rgb*Effects.y);
        float alpha=max(glow.r,max(glow.g,glow.b));
        outlined=float4(min(outlined.rgb+glow*(1-outlined.a),1.0),outlined.a+alpha*(1-outlined.a));
    }
    return outlined;
}
float4 BlurPS(float4 position : SV_POSITION) : SV_Target {
    float2 uv=position.xy/ceil(Size.xy*0.5);
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
namespace helengine::windows {
    /// Describes this effect's existing variants for the common asynchronous DirectX11 compiler.
    inline std::vector<DirectX11ShaderRequest> BuildCameraToonShaderRequests(const std::string& source) {
        const char* entries[]={"GeometryVS","GeometryPS","PearlGeometryPS","CompositeVS","CompositePS","CompositePS","BlurPS"};
        std::vector<DirectX11ShaderRequest> requests;
        for(std::size_t entry=0;entry<std::size(entries);++entry) {
            requests.push_back({source,"CameraToon",entries[entry],entry==0 || entry==3 ? "vs_5_0" : "ps_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES",entry==5 ? "4" : "1"}}});
        }
        return requests;
    }
}
