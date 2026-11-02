#pragma once
namespace helengine::windows {
    /// <summary>Defines the shared screen-space transport ABI and bounded screen-ray integration used by capture and receivers.</summary>
    inline constexpr const char* SceneSsgiShader=R"(
cbuffer SsgiBuffer:register(b10) {
    float4x4 SsgiViewProjection;
    float4 SsgiSize; // width, height, enabled, indirect-only
    float4 SsgiCamera;
    float4 SsgiForward;
    float4 SsgiSunDirection;
    float4 SsgiSunColor;
};
Texture2D<float4> SsgiPosition:register(t18);
Texture2D<float4> SsgiNormal:register(t19);
Texture2D<float4> SsgiRadiance:register(t20);
Texture2D<float4> SsgiIndirect:register(t21);
float2 SsgiUv(float3 p) {
    float4 clip=mul(float4(p,1),SsgiViewProjection);
    return float2(clip.x,-clip.y)/max(clip.w,0.00001)*0.5+0.5;
}
float3 SsgiDisplay(float3 irradiance) {
    if(SsgiSize.w>0.5) return dot(max(irradiance,0),float3(0.2126,0.7152,0.0722)).xxx;
    return irradiance;
}
float3 SampleSharedSsgi(float3 position,float3 normal) {
    if(SsgiSize.z<0.5) return 0;
    float4 clip=mul(float4(position,1),SsgiViewProjection);
    if(clip.w<=0 || clip.z<0 || clip.z>clip.w) return 0;
    float2 pixel=SsgiUv(position)*SsgiSize.xy-0.5;
    int2 base=int2(floor(pixel));float2 fraction=frac(pixel);
    float3 total=0;float weights=0;
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 tap=base+int2(x,y);
        if(any(tap<0) || any(tap>=int2(SsgiSize.xy))) continue;
        float4 source=SsgiPosition.Load(int3(tap,0));float3 n=SsgiNormal.Load(int3(tap,0)).xyz;
        // Plane and full-position validation prevent a different output's foreground from illuminating this receiver.
        float3 delta=source.xyz-position;float separation=length(delta);
        float footprint=max(0.08,length(position-SsgiCamera.xyz)*3.0/max(SsgiSize.x,SsgiSize.y));
        if(source.w<0.5 || dot(n,normal)<0.9 || abs(dot(delta,normal))>0.045 || separation>footprint) continue;
        float weight=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);
        total+=SsgiIndirect.Load(int3(tap,0)).rgb*weight;weights+=weight;
    }
    return weights>0.00001?total/weights:0;
}
float4 SsgiFullscreenVS(uint id:SV_VertexID):SV_Position {
    return float4(id==1?3:-1,id==2?3:-1,0,1);
}
// Converts screen-linear position to the world-linear ray parameter through homogeneous interpolation.
float SsgiWorldParameter(float screenParameter,float startW,float endW,float distance) {
    return distance*screenParameter*startW/max(endW*(1-screenParameter)+screenParameter*startW,0.00001);
}
float4 SsgiTracePS(float4 pixel:SV_POSITION):SV_Target {
    int2 originPixel=int2(pixel.xy);float4 origin=SsgiPosition.Load(int3(originPixel,0));
    if(origin.w<0.5) return 0;
    float3 normal=normalize(SsgiNormal.Load(int3(originPixel,0)).xyz);
    float3 axis=abs(normal.z)<0.9?float3(0,0,1):float3(0,1,0);
    float3 tangent=normalize(cross(axis,normal));float3 bitangent=cross(normal,tangent);
    float3 irradiance=0;
    // Eight cosine-weighted hemisphere directions; screen misses remain zero in the fixed denominator.
    [loop] for(int ray=0;ray<8;++ray) {
        float radius=sqrt((ray+0.5)/8.0);float angle=ray*2.39996323;
        float3 direction=tangent*(cos(angle)*radius)+bitangent*(sin(angle)*radius)+normal*sqrt(1-radius*radius);
        float3 start=origin.xyz+normal*0.045;float maximumDistance=3.84;
        float4 startClip=mul(float4(start,1),SsgiViewProjection);
        if(startClip.w<=0.00001 || startClip.z<0 || startClip.z>startClip.w) continue;
        float4 endClip=mul(float4(start+direction*maximumDistance,1),SsgiViewProjection);
        // Clip the homogeneous segment before projection, including rays pointing towards the near plane.
        float limit=1;
        if(endClip.z<0) limit=min(limit,startClip.z/(startClip.z-endClip.z));
        if(endClip.z>endClip.w) limit=min(limit,(startClip.w-startClip.z)/((startClip.w-startClip.z)-(endClip.w-endClip.z)));
        if(endClip.w<0.00001) limit=min(limit,(startClip.w-0.00001)/(startClip.w-endClip.w));
        endClip=lerp(startClip,endClip,limit);maximumDistance*=limit;
        float2 startPixel=(float2(startClip.x,-startClip.y)/startClip.w*0.5+0.5)*SsgiSize.xy;
        float2 endPixel=(float2(endClip.x,-endClip.y)/max(endClip.w,0.00001)*0.5+0.5)*SsgiSize.xy;
        float2 delta=endPixel-startPixel;int2 cell=int2(floor(startPixel));
        int2 stepSign=int2(delta.x>=0?1:-1,delta.y>=0?1:-1);
        float2 inverse=1/max(abs(delta),0.0000001);
        float2 boundary=float2(cell)+float2(stepSign.x>0?1:0,stepSign.y>0?1:0);
        float2 next=abs(boundary-startPixel)*inverse;
        if(abs(delta.x)<0.0000001) next.x=1e20;
        if(abs(delta.y)<0.0000001) next.y=1e20;
        float entry=0;
        // Traverse every crossed pixel cell, rather than skipping blockers between fixed world-step endpoints.
        // Budget exhaustion deliberately returns a miss; there is no coarser fallback or invented lighting.
        [loop] for(int step=0;step<24;++step) {
            if(any(cell<0) || any(cell>=int2(SsgiSize.xy))) break;
            float exit=min(1,min(next.x,next.y));
            float entryDistance=SsgiWorldParameter(entry,startClip.w,endClip.w,maximumDistance);
            float exitDistance=SsgiWorldParameter(exit,startClip.w,endClip.w,maximumDistance);
            float4 surface=SsgiPosition.Load(int3(cell,0));
            if(surface.w>0.5 && length(surface.xyz-origin.xyz)>0.09) {
                float3 sourceNormal=normalize(SsgiNormal.Load(int3(cell,0)).xyz);
                float denominator=dot(sourceNormal,direction);
                float planeDistance=abs(denominator)>0.00001?dot(surface.xyz-start,sourceNormal)/denominator:-1;
                if(planeDistance>0.025 && planeDistance>=entryDistance-0.025 && planeDistance<=exitDistance+0.025) {
                    // The first visible surface terminates the segment even when it is black or backfacing.
                    if(dot(sourceNormal,-direction)>0.03) irradiance+=max(SsgiRadiance.Load(int3(cell,0)).rgb,0)*3.14159265/8.0;
                    break;
                }
                float entryDepth=dot(start+direction*entryDistance-SsgiCamera.xyz,SsgiForward.xyz);
                float surfaceDepth=dot(surface.xyz-SsgiCamera.xyz,SsgiForward.xyz);
                if(entryDepth>surfaceDepth+0.19) break;
            }
            if(exit>=1) break;
            // Advancing one axis at a time also visits a boundary cell on an exact corner tie.
            if(next.x<next.y) {cell.x+=stepSign.x;entry=next.x;next.x+=inverse.x;}
            else {cell.y+=stepSign.y;entry=next.y;next.y+=inverse.y;}
        }
    }
    return float4(irradiance,1);
}
)";
    /// <summary>Captures material diffuse outgoing radiance under the existing authored sun without ambient, effects or indirect feedback.</summary>
    inline constexpr const char* SceneSsgiCaptureShader=R"(
cbuffer TransformBuffer:register(b0) {float4x4 World;float4x4 WorldViewProjection;float4x4 WorldNormal;float4 CameraPosition;float4 MaterialFlags;};
cbuffer BaseColorBuffer:register(b3){float4 BaseColor;};
Texture2D DiffuseTexture:register(t0);SamplerState DiffuseSampler:register(s0);
struct SsgiMeshInput {float3 Position:POSITION;float3 Normal:NORMAL;float2 UV:TEXCOORD0;};
struct SsgiMeshOutput {float4 Position:SV_POSITION;float3 WorldPosition:TEXCOORD0;float3 Normal:TEXCOORD1;float2 UV:TEXCOORD2;};
struct SsgiCaptureOutput {float4 Position:SV_Target0;float4 Normal:SV_Target1;float4 Radiance:SV_Target2;};
SsgiMeshOutput SsgiCaptureVS(SsgiMeshInput input) {
    SsgiMeshOutput output;output.Position=mul(float4(input.Position,1),WorldViewProjection);
    output.WorldPosition=mul(float4(input.Position,1),World).xyz;output.Normal=mul(float4(input.Normal,0),WorldNormal).xyz;output.UV=input.UV;return output;
}
SsgiCaptureOutput SsgiCapturePS(SsgiMeshOutput input) {
    float4 surface=DiffuseTexture.Sample(DiffuseSampler,input.UV)*BaseColor;
    clip(surface.a-0.999); // Initial transport models opaque diffuse geometry only.
    float3 normal=normalize(input.Normal);SsgiCaptureOutput output;
    output.Position=float4(input.WorldPosition,1);output.Normal=float4(normal,1);
    output.Radiance=float4(max(surface.rgb,0)*max(SsgiSunColor.rgb,0)*saturate(dot(normal,-SsgiSunDirection.xyz))/3.14159265,1);
    return output;
}
)";
}
