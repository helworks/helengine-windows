#pragma once
namespace helengine::windows {
    /// <summary>Visualizes actual free probes using raw Rec.709 luminance of their directional irradiance, with no scene shading.</summary>
    inline constexpr const char* SceneGiSpheresShader=R"(
cbuffer ProbeVolume:register(b9) {float4 Origin;float4 CellSize;float4 CellCount;};
cbuffer ProbeCamera:register(b11) {row_major float4x4 ViewProjection;};
Texture2D<float4> ProbeAtlas:register(t17);
StructuredBuffer<uint> ProbeUsage:register(t22);
struct SphereVertex {float4 Position:SV_POSITION;float3 Normal:NORMAL;nointerpolation uint3 Cell:TEXCOORD0;};
float4 ProbeLoad(uint3 cell,uint lobe) {return ProbeAtlas.Load(int3(cell.x+(uint)CellCount.x*cell.y,cell.z+(uint)CellCount.z*lobe,0));}
SphereVertex ProbeVS(uint vertex:SV_VertexID,uint instance:SV_InstanceID) {
    SphereVertex output;
    uint xCount=(uint)CellCount.x,yCount=(uint)CellCount.y;
    output.Cell=uint3(instance%xCount,(instance/xCount)%yCount,instance/(xCount*yCount));
    // Eight slices and four latitude bands bound the cost to 192 vertices per probe.
    const uint corners[6]={0,1,2,2,1,3};
    uint corner=corners[vertex%6],quad=vertex/6;
    float longitude=(quad%8+(corner&1))*0.7853981633974483;
    float latitude=(quad/8+(corner>>1))*0.7853981633974483;
    float sine=sin(latitude);
    output.Normal=float3(sine*cos(longitude),cos(latitude),sine*sin(longitude));
    float radius=0.18*min(CellSize.x,min(CellSize.y,CellSize.z));
    float3 position=Origin.xyz+(float3(output.Cell)+0.5)*CellSize.xyz+radius*output.Normal;
    output.Position=mul(float4(position,1),ViewProjection);
    if(ProbeLoad(output.Cell,0).a<0.5 || ProbeUsage[instance]==0) output.Position=float4(0,0,-1,1);
    return output;
}
float4 ProbePS(SphereVertex input):SV_Target {
    float3 normal=normalize(input.Normal);
    float3 weight=abs(normal)/max(dot(abs(normal),float3(1,1,1)),1e-8);
    float3 irradiance=ProbeLoad(input.Cell,normal.x>=0?0:1).rgb*weight.x
        +ProbeLoad(input.Cell,normal.y>=0?2:3).rgb*weight.y
        +ProbeLoad(input.Cell,normal.z>=0?4:5).rgb*weight.z;
    float intensity=max(dot(irradiance,float3(0.2126,0.7152,0.0722)),0);
    return float4(intensity.xxx,1);
}
)";
}
