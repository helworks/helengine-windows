#pragma once
namespace helengine::windows {
    /// <summary>Integrates bounded GTAO horizons analytically, then filters visibility across compatible surface samples without history.</summary>
    inline constexpr const char* CameraGtaoShader=R"(
// Independent SM5 implementation of Jimenez et al., Practical Real-Time Strategies for Accurate Indirect Occlusion,
// Algorithm 1; horizon falloff follows the approach discussed in Intel XeGTAO (MIT).
// https://github.com/GameTechDev/XeGTAO and https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf
// The antiderivative integrates cos(theta-n)*abs(sin(theta)) over each signed slice half.
float GtaoArc(float angle,float normalAngle) {
    return (cos(normalAngle)+2*angle*sin(normalAngle)-cos(2*angle-normalAngle))*0.25;
}
float3 GtaoPosition(int2 pixel) {
    float4 value=GeometryPositionTexture.Load(int3(pixel,0));
    return value.xyz/max(value.w,0.00001);
}
float GtaoPS(float4 position:SV_POSITION):SV_Target {
    int2 pixel=int2(position.xy);
    float4 geometry=GeometryPositionTexture.Load(int3(pixel,0));
    float3 center=geometry.xyz/max(geometry.w,0.00001);
    // Evaluate derivatives in uniform flow, before any geometry/background branch.
    float3 dx=ddx(center),dy=ddy(center);
    if(geometry.w<=0 || Occlusion.x<=0 || Occlusion.z<=0) return 1;
    float3 normal=GeometryNormalTexture.Load(int3(pixel,0)).xyz;
    float3 eye=CameraPosition.xyz-center;
    if(dot(normal,normal)<0.000001 || dot(eye,eye)<0.000001) return 1;
    normal=normalize(normal); float3 view=normalize(eye);
    if(dot(normal,view)<=0.0001) return 1;
    dx-=view*dot(dx,view);dy-=view*dot(dy,view);
    float pixelSize=max(min(length(dx),length(dy)),0.00001);
    float radius=min(Occlusion.z,Occlusion.x/pixelSize);
    if(radius<1) return 1;
    // A stable four-by-four pattern distributes slice directions; the spatial filter needs no TAA.
    float rotation=frac(dot(float2(pixel&3),float2(0.25,0.0625)))*3.14159265;
    float visible=0,unblocked=0;
    [unroll] for(int slice=0;slice<3;++slice) {
        float angle=rotation+(slice+0.5)*1.04719755;
        float2 direction=float2(cos(angle),sin(angle));
        float3 tangent=dx*direction.x+dy*direction.y;
        float tangentSquared=dot(tangent,tangent);
        if(tangentSquared<0.00000001) continue;
        tangent*=rsqrt(tangentSquared);
        float3 axis=normalize(cross(tangent,view));
        float3 projected=normal-axis*dot(normal,axis);
        float projectedLength=length(projected);
        if(projectedLength<0.00001) continue;
        float normalAngle=atan2(dot(projected,tangent),dot(projected,view));
        float2 low=float2(cos(normalAngle+1.57079633),cos(normalAngle-1.57079633));
        float2 horizon=low;
        [unroll] for(int step=0;step<4;++step) {
            float fraction=(step+1)*0.25;
            float2 offset=round(direction*max(1.0,radius*fraction*fraction));
            [unroll] for(int side=0;side<2;++side) {
                int2 neighbor=pixel+int2(offset)*(side==0 ? 1:-1);
                if(any(neighbor<0) || any(neighbor>=int2(Size.xy))) continue;
                float4 sampleGeometry=GeometryPositionTexture.Load(int3(neighbor,0));
                if(sampleGeometry.w<=0) continue;
                float3 delta=sampleGeometry.xyz/sampleGeometry.w-center;
                // Reject coplanar samples before the angular test; bias is in scene units.
                if(dot(normal,delta)<=Occlusion.y) continue;
                float distance=length(delta);
                if(distance>=Occlusion.x || distance<0.00001) continue;
                float weight=saturate((1-distance/Occlusion.x)*2)*sampleGeometry.w;
                float cosine=dot(view,normalize(delta-normal*Occlusion.y));
                float candidate=lerp(side==0 ? low.x:low.y,cosine,weight);
                if(side==0) horizon.x=max(horizon.x,candidate);
                else horizon.y=max(horizon.y,candidate);
            }
        }
        float hPositive=acos(clamp(horizon.x,-1,1));
        float hNegative=-acos(clamp(horizon.y,-1,1));
        hPositive=normalAngle+clamp(hPositive-normalAngle,-1.57079633,1.57079633);
        hNegative=normalAngle+clamp(hNegative-normalAngle,-1.57079633,1.57079633);
        visible+=projectedLength*(GtaoArc(hPositive,normalAngle)+GtaoArc(hNegative,normalAngle));
        float baselinePositive=normalAngle+1.57079633,baselineNegative=normalAngle-1.57079633;
        unblocked+=projectedLength*(GtaoArc(baselinePositive,normalAngle)+GtaoArc(baselineNegative,normalAngle));
    }
    return saturate(visible/max(unblocked,0.00001));
}
// Symmetric plane-distance rejection preserves sloped surfaces and rejects both sides of depth/normal discontinuities.
float GtaoFilterPS(float4 position:SV_POSITION):SV_Target {
    int2 pixel=int2(position.xy);
    float4 centerGeometry=GeometryPositionTexture.Load(int3(pixel,0));
    if(centerGeometry.w<=0) return 1;
    float3 center=centerGeometry.xyz/centerGeometry.w;
    float3 normal=normalize(GeometryNormalTexture.Load(int3(pixel,0)).xyz);
    float total=AmbientVisibilityTexture.Load(int3(pixel,0))*4,weights=4;
    float tolerance=max(Occlusion.y*2,Occlusion.x*0.025);
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        if(x==0 && y==0) continue;
        int2 neighbor=pixel+int2(x,y);
        if(any(neighbor<0) || any(neighbor>=int2(Size.xy))) continue;
        float4 sampleGeometry=GeometryPositionTexture.Load(int3(neighbor,0));
        if(sampleGeometry.w<=0) continue;
        float3 sampleNormal=normalize(GeometryNormalTexture.Load(int3(neighbor,0)).xyz);
        float3 delta=sampleGeometry.xyz/sampleGeometry.w-center;
        float planeDistance=max(abs(dot(normal,delta)),abs(dot(sampleNormal,delta)));
        float weight=(x==0 || y==0 ? 2.0:1.0)*saturate(1-planeDistance/max(tolerance,0.00001));
        weight*=pow(saturate(dot(normal,sampleNormal)),16)*min(centerGeometry.w,sampleGeometry.w);
        total+=AmbientVisibilityTexture.Load(int3(neighbor,0))*weight;weights+=weight;
    }
    return total/weights;
}
)";
}
