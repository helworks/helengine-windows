#pragma once

namespace helengine::windows {
    /// <summary>Samples the portable six-lobe RGBA32F field without filtering through occupied probes or walls.</summary>
    inline constexpr const char* SceneGiShader = R"(
cbuffer GlobalIlluminationBuffer : register(b9) {
    float4 GiOriginEnabled;
    float4 GiCellSize;
    float4 GiCellCount;
};
Texture2D<float4> GlobalIlluminationAtlas : register(t17);
#if defined(HELENGINE_GI_TRACK_READERS)
RWStructuredBuffer<uint> GiReaderUsage : register(u0);
#endif
bool GiInside(int3 cell) { return all(cell >= 0) && all(cell < (int3)GiCellCount.xyz); }
float4 GiLoad(int3 cell, int lobe) {
    return GlobalIlluminationAtlas.Load(int3(cell.x + (int)GiCellCount.x * cell.y, cell.z + (int)GiCellCount.z * lobe, 0));
}
bool GiVisible(float3 origin, float3 destination) {
    float3 start=(origin-GiOriginEnabled.xyz)/GiCellSize.xyz;
    float3 finish=(destination-GiOriginEnabled.xyz)/GiCellSize.xyz;
    float3 delta=finish-start;
    int3 cell=(int3)floor(start), target=(int3)floor(finish);
    int3 step=int3(delta.x>0?1:-1,delta.y>0?1:-1,delta.z>0?1:-1);
    float3 inverse=float3(abs(delta.x)<1e-20?1e30:1/abs(delta.x),abs(delta.y)<1e-20?1e30:1/abs(delta.y),abs(delta.z)<1e-20?1e30:1/abs(delta.z));
    float3 edge=float3(step.x>0?cell.x+1:cell.x,step.y>0?cell.y+1:cell.y,step.z>0?cell.z+1:cell.z);
    float3 next=abs(edge-start)*inverse;
    if(abs(delta.x)<1e-20) next.x=1e30;
    if(abs(delta.y)<1e-20) next.y=1e30;
    if(abs(delta.z)<1e-20) next.z=1e30;
    [loop] for(int iteration=0;iteration<384;++iteration) {
        if(!GiInside(cell) || GiLoad(cell,0).a<0.5) return false;
        if(all(cell==target)) return true;
        float crossing=min(next.x,min(next.y,next.z));
        if(crossing>1) return true;
        if(next.x<=next.y && next.x<=next.z) {cell.x+=step.x;next.x+=inverse.x;}
        else if(next.y<=next.z) {cell.y+=step.y;next.y+=inverse.y;}
        else {cell.z+=step.z;next.z+=inverse.z;}
    }
    return false;
}
bool GiSurfaceOrigin(float3 position,float3 normal,out float3 origin) {
    origin=position;
    float3 start=(position-GiOriginEnabled.xyz)/GiCellSize.xyz;
    int3 cell=(int3)floor(start);
    if(!GiInside(cell)) return false;
    if(GiLoad(cell,0).a>=0.5) return true;
    float4 surfacePlane=GiLoad(cell,0);
    if(surfacePlane.a< -0.5) return false;
    float offset=GiLoad(cell,1).r;
    float minimumCell=min(GiCellSize.x,min(GiCellSize.y,GiCellSize.z));
    float pointScale=max(dot(abs(surfacePlane.rgb),abs(position)),max(abs(offset),1e-3));
    float pointTolerance=9.53674316e-7*pointScale+minimumCell*1e-6;
    if(dot(surfacePlane.rgb,surfacePlane.rgb)<0.5 || abs(dot(surfacePlane.rgb,position)-offset)>pointTolerance) return false;
    // Only the receiver's own uniform plane footprint may be crossed. Mixed cells and another plane
    // conservatively block transport, including a distinct roof sharing the receiver's occupied voxel.
    float3 delta=normal/GiCellSize.xyz;
    int3 step=int3(delta.x>=0?1:-1,delta.y>=0?1:-1,delta.z>=0?1:-1);
    float3 inverse=float3(abs(delta.x)<1e-20?1e30:1/abs(delta.x),abs(delta.y)<1e-20?1e30:1/abs(delta.y),abs(delta.z)<1e-20?1e30:1/abs(delta.z));
    float3 edge=float3(step.x>0?cell.x+1:cell.x,step.y>0?cell.y+1:cell.y,step.z>0?cell.z+1:cell.z);
    float3 next=abs(edge-start)*inverse;
    if(abs(delta.x)<1e-20) next.x=1e30;
    if(abs(delta.y)<1e-20) next.y=1e30;
    if(abs(delta.z)<1e-20) next.z=1e30;
    float maximum=2.01*length(GiCellSize.xyz);
    float epsilon=minimumCell*1e-4;
    [loop] for(int iteration=0;iteration<384;++iteration) {
        float crossing=min(next.x,min(next.y,next.z));
        if(crossing>maximum) return false;
        if(next.x<=next.y && next.x<=next.z) {cell.x+=step.x;next.x+=inverse.x;}
        else if(next.y<=next.z) {cell.y+=step.y;next.y+=inverse.y;}
        else {cell.z+=step.z;next.z+=inverse.z;}
        if(!GiInside(cell)) return false;
        float4 plane=GiLoad(cell,0);
        if(plane.a<0.5) {
            if(plane.a< -0.5) return false;
            float enteredOffset=GiLoad(cell,1).r;
            if(dot(plane.rgb,surfacePlane.rgb)<0) {plane.rgb=-plane.rgb;enteredOffset=-enteredOffset;}
            float offsetTolerance=2.38418579e-7*max(max(abs(offset),abs(enteredOffset)),1e-3)+minimumCell*1e-6;
            if(any(abs(plane.rgb-surfacePlane.rgb)>9.53674316e-7) || abs(enteredOffset-offset)>offsetTolerance) return false;
            continue;
        }
        float3 candidate=position+normal*(crossing+epsilon);
        int3 physicalCell=(int3)floor((candidate-GiOriginEnabled.xyz)/GiCellSize.xyz);
        if(GiInside(physicalCell) && GiLoad(physicalCell,0).a>=0.5) {origin=candidate;return true;}
    }
    return false;
}
float3 SampleSceneGi(float3 position,float3 normal) {
    if(GiOriginEnabled.w<0.5) return 0;
    float3 biased;
    if(!GiSurfaceOrigin(position,normal,biased)) return 0;
    float3 grid=(biased-GiOriginEnabled.xyz)/GiCellSize.xyz;
    if(any(grid<0) || any(grid>=GiCellCount.xyz)) return 0;
    float3 centerGrid=grid-0.5;
    int3 lower=(int3)floor(centerGrid);
    float3 fraction=frac(centerGrid);
    float3 directionWeight=abs(normal)/max(abs(normal.x)+abs(normal.y)+abs(normal.z),1e-8);
    float3 result=0;float total=0;
#if defined(HELENGINE_GI_TRACK_READERS)
    uint acceptedCells[8];uint acceptedCount=0;
#endif
    [loop] for(int tap=0;tap<8;++tap) {
        int x=tap&1,y=(tap>>1)&1,z=(tap>>2)&1;
        int3 cell=lower+int3(x,y,z);
        if(!GiInside(cell)) continue;
        float4 probe=GiLoad(cell,0);
        if(probe.a<0.5) continue;
        float3 center=GiOriginEnabled.xyz+(float3(cell)+0.5)*GiCellSize.xyz;
        if(!GiVisible(biased,center)) continue;
        float weight=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y)*(z?fraction.z:1-fraction.z);
        float3 irradiance=GiLoad(cell,normal.x>=0?0:1).rgb*directionWeight.x
            +GiLoad(cell,normal.y>=0?2:3).rgb*directionWeight.y
            +GiLoad(cell,normal.z>=0?4:5).rgb*directionWeight.z;
        result+=irradiance*weight;total+=weight;
#if defined(HELENGINE_GI_TRACK_READERS)
        if(weight>0) acceptedCells[acceptedCount++]=(uint)(cell.x+GiCellCount.x*(cell.y+GiCellCount.y*cell.z));
#endif
    }
#if defined(HELENGINE_GI_TRACK_READERS)
    if(total>1e-8) {
        [loop] for(uint accepted=0;accepted<acceptedCount;++accepted) InterlockedOr(GiReaderUsage[acceptedCells[accepted]],1);
    }
#endif
    return total>1e-8?result/total:float3(0,0,0);
}
)";
}
