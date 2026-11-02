#pragma once
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace helengine::windows {
    /// <summary>Fits two stable directional shadow regions using the caller's scalar and arithmetic policy.</summary>
    template<class Scalar, class Math>
    class Win32ShadowCascadeFitter {
    public:
        /// <summary>Stores a world-space vector without changing the engine numeric policy.</summary>
        using Point = std::array<Scalar, 3>;
        /// <summary>Stores an affine model box's eight world corners, indexed by XYZ bit flags.</summary>
        using Box = std::array<Point, 8>;
        /// <summary>Stores a world clipping plane with nonnegative signed distance inside.</summary>
        using Plane = std::array<Scalar, 4>;
        /// <summary>Stores one output camera's normalized geometry and layer membership.</summary>
        struct Camera {
            /// <summary>Defines the original perspective frustum in world space.</summary>
            std::array<Plane, 6> Planes;
            /// <summary>Defines frustum corners before the shared depth slices are applied.</summary>
            Box Corners;
            /// <summary>Retains the camera's authored receiver layers.</summary>
            unsigned Layers;
        };
        /// <summary>Stores geometry bounds with their authored output-layer eligibility.</summary>
        struct Geometry {
            /// <summary>Contains the actual transformed model bounds, including thin walls.</summary>
            Box Corners;
            /// <summary>Determines which original camera may receive this geometry.</summary>
            unsigned Layers;
            /// <summary>Identifies the drawable independently from changing orientation.</summary>
            const void* Identity = nullptr;
            /// <summary>Identifies the runtime geometry resource for structural replacement.</summary>
            const void* ModelIdentity = nullptr;
            /// <summary>Tracks local bounds, scale magnitudes and parent translation while excluding rotation.</summary>
            std::array<Scalar,12> StructuralState{};
        };
        /// <summary>Stores one fitted square projection and measured texel density.</summary>
        struct Cascade {
            /// <summary>Transforms world positions to Direct3D clip space using row vectors.</summary>
            std::array<Scalar, 16> Matrix;
            /// <summary>Retains the square side in world units for diagnostics and stabilization.</summary>
            Scalar Extent = 0;
            /// <summary>Retains the world-space distance between adjacent texel centers.</summary>
            Scalar WorldTexel = 0;
            /// <summary>Retains the original receiver points for focused coverage verification.</summary>
            std::vector<Point> Receivers;
        };
        /// <summary>Stores both fitted tiles and their authoritative scene-camera depth contract.</summary>
        struct Result {
            /// <summary>Contains the near and far projections in atlas order.</summary>
            std::array<Cascade, 2> Cascades;
            /// <summary>Measures positive depth along the original scene camera, including its translation.</summary>
            Plane DepthPlane;
            /// <summary>Contains near, split and far camera depths, independent of reflection faces.</summary>
            std::array<Scalar, 3> Depths{};
            /// <summary>Distinguishes a valid empty frame from invalid camera inputs.</summary>
            bool HasReceivers = false;
            /// <summary>Retains the camera and structural geometry state which defines the stabilization domain.</summary>
            std::vector<Scalar> StabilitySignature;
            /// <summary>Retains drawable and model identities to detect source membership changes.</summary>
            std::vector<const void*> SourceIdentities;
        };
        /// <summary>Subtracts world vectors using the selected scalar evaluation rules.</summary>
        static Point Subtract(const Point& left, const Point& right) {
            return {left[0]-right[0], left[1]-right[1], left[2]-right[2]};
        }
        /// <summary>Computes a scalar product without crossing the GPU precision boundary.</summary>
        static Scalar Dot(const Point& left, const Point& right) {
            return left[0]*right[0]+left[1]*right[1]+left[2]*right[2];
        }
        /// <summary>Computes the right-handed cross product used by the light basis.</summary>
        static Point Cross(const Point& left, const Point& right) {
            return {left[1]*right[2]-left[2]*right[1], left[2]*right[0]-left[0]*right[2], left[0]*right[1]-left[1]*right[0]};
        }
        /// <summary>Rejects degenerate directions instead of constructing an arbitrary projection.</summary>
        static Point Normalize(const Point& value) {
            Scalar length = Math::Sqrt(Dot(value,value));
            if(length <= Scalar(0.000001)) throw std::invalid_argument("Shadow fitting requires a nonzero direction.");
            return {value[0]/length,value[1]/length,value[2]/length};
        }
        /// <summary>Evaluates a world-space clipping plane.</summary>
        static Scalar Distance(const Plane& plane, const Point& point) {
            return plane[0]*point[0]+plane[1]*point[1]+plane[2]*point[2]+plane[3];
        }
        /// <summary>Builds inward planes from an affine model box, retaining zero-thickness geometry.</summary>
        static std::array<Plane,6> BoxPlanes(const Box& box) {
            std::array<Plane,6> planes{};
            std::array<Point,3> axes{Subtract(box[1],box[0]),Subtract(box[2],box[0]),Subtract(box[4],box[0])};
            unsigned nonzero=0;
            for(const auto& axis:axes) if(Dot(axis,axis)>Scalar(0.000000000001)) ++nonzero;
            if(nonzero==0) axes={Point{1,0,0},Point{0,1,0},Point{0,0,1}};
            else if(nonzero==1) {
                unsigned present=0;while(Dot(axes[present],axes[present])<=Scalar(0.000000000001)) ++present;
                const auto direction=Normalize(axes[present]);
                const Point up=Math::Abs(direction[1])>Scalar(.99)?Point{0,0,1}:Point{0,1,0};
                axes[(present+1)%3]=Normalize(Cross(up,direction));axes[(present+2)%3]=Cross(direction,axes[(present+1)%3]);
            } else if(nonzero==2) {
                for(unsigned axis=0;axis<3;++axis) if(Dot(axes[axis],axes[axis])<=Scalar(0.000000000001)) axes[axis]=Normalize(Cross(axes[(axis+1)%3],axes[(axis+2)%3]));
            }
            for(unsigned axis=0;axis<3;++axis) {
                auto normal=Normalize(Cross(axes[(axis+1)%3],axes[(axis+2)%3]));
                if(Dot(normal,axes[axis])<Scalar(0)) for(auto& component:normal) component=-component;
                planes[axis*2]={normal[0],normal[1],normal[2],-Dot(normal,box[0])};
                planes[axis*2+1]={-normal[0],-normal[1],-normal[2],Dot(normal,box[1u<<axis])};
            }
            return planes;
        }
        /// <summary>Clips one segment against a convex receiver volume and retains both boundary points.</summary>
        template<std::size_t PlaneCount>
        static void ClipEdge(const Point& start,const Point& end,const std::array<Plane,PlaneCount>& planes,std::vector<Point>& output) {
            Scalar lower=0,upper=1;
            for(const auto& plane:planes) {
                Scalar first=Distance(plane,start),last=Distance(plane,end);
                if(first<Scalar(0) && last<Scalar(0)) return;
                if((first<Scalar(0)) != (last<Scalar(0))) {
                    Scalar amount=first/(first-last);
                    if(first<Scalar(0)) lower=std::max(lower,amount);else upper=std::min(upper,amount);
                    if(lower>upper) return;
                }
            }
            for(auto amount:{lower,upper}) output.push_back({start[0]+(end[0]-start[0])*amount,start[1]+(end[1]-start[1])*amount,start[2]+(end[2]-start[2])*amount});
        }
        /// <summary>Intersects model and camera boxes by clipping both sets of edges, including containment cases.</summary>
        static void Intersect(const Box& box,const Camera& camera,const Plane& depth,Scalar nearDepth,Scalar farDepth,std::vector<Point>& points) {
            std::array<Plane,8> cameraPlanes{},boxPlanes{};
            std::copy(camera.Planes.begin(),camera.Planes.end(),cameraPlanes.begin());
            cameraPlanes[6]={depth[0],depth[1],depth[2],depth[3]-nearDepth};
            cameraPlanes[7]={-depth[0],-depth[1],-depth[2],farDepth-depth[3]};
            const auto bounds=BoxPlanes(box);
            std::copy(bounds.begin(),bounds.end(),boxPlanes.begin());boxPlanes[6]=cameraPlanes[6];boxPlanes[7]=cameraPlanes[7];
            for(unsigned vertex=0;vertex<8;++vertex) for(unsigned bit=1;bit<=4;bit*=2) if((vertex&bit)==0) {
                ClipEdge(box[vertex],box[vertex|bit],cameraPlanes,points);
                ClipEdge(camera.Corners[vertex],camera.Corners[vertex|bit],boxPlanes,points);
            }
            // Slice/frustum intersections may lie on no original edge: intersect slice-crossing box edges first,
            // then clip their convex face segments using the same camera planes.
            const unsigned faces[6][4]={{0,2,6,4},{1,5,7,3},{0,4,5,1},{2,3,7,6},{0,1,3,2},{4,6,7,5}};
            for(auto boundary:{nearDepth,farDepth}) for(const auto& face:faces) {
                std::array<Point,4> section{};unsigned sectionCount=0;
                for(unsigned edge=0;edge<4;++edge) {
                    const auto& first=box[face[edge]];const auto& last=box[face[(edge+1)%4]];
                    Scalar firstDepth=Distance(depth,first),lastDepth=Distance(depth,last);
                    if((firstDepth<boundary)!=(lastDepth<boundary)) {
                        Scalar amount=(boundary-firstDepth)/(lastDepth-firstDepth);
                        section[sectionCount++]={first[0]+(last[0]-first[0])*amount,first[1]+(last[1]-first[1])*amount,first[2]+(last[2]-first[2])*amount};
                    }
                }
                if(sectionCount==2) ClipEdge(section[0],section[1],cameraPlanes,points);
            }
        }
        /// <summary>Builds a stabilized projection with caster depth independent of receiver-slice membership.</summary>
        static Cascade FitRegion(std::vector<Point> receivers,const std::vector<Geometry>& casters,const Point& lightForward,Scalar previousExtent,Scalar rotationExtent) {
            Cascade result{};result.Receivers=std::move(receivers);
            if(result.Receivers.empty()) throw std::invalid_argument("Each directional shadow cascade requires receiver geometry.");
            const Point z=Normalize(lightForward);
            const Point up=Math::Abs(z[1])>Scalar(.99)?Point{0,0,1}:Point{0,1,0};
            const Point x=Normalize(Cross(up,z)),y=Cross(z,x);
            Scalar minimumX=Dot(result.Receivers[0],x),maximumX=minimumX,minimumY=Dot(result.Receivers[0],y),maximumY=minimumY;
            Scalar minimumZ=Dot(result.Receivers[0],z),maximumZ=minimumZ;
            for(const auto& point:result.Receivers) {
                minimumX=std::min(minimumX,Dot(point,x));maximumX=std::max(maximumX,Dot(point,x));
                minimumY=std::min(minimumY,Dot(point,y));maximumY=std::max(maximumY,Dot(point,y));
                minimumZ=std::min(minimumZ,Dot(point,z));maximumZ=std::max(maximumZ,Dot(point,z));
            }
            for(const auto& caster:casters) for(const auto& point:caster.Corners) {
                minimumZ=std::min(minimumZ,Dot(point,z));maximumZ=std::max(maximumZ,Dot(point,z));
            }
            // Quantized grow-only scale prevents animated bounds from pumping the scene's texel density.
            // Two texels of guard absorb center snapping while retaining every receiver at tile boundaries.
            Scalar required=std::max(rotationExtent,std::max(Scalar(.0625),std::max(Scalar(maximumX-minimumX),Scalar(maximumY-minimumY))));
            result.Extent=std::max(previousExtent,Scalar(Math::Ceiling((required*Scalar(1024)/Scalar(1020))*Scalar(4))/Scalar(4)));
            result.WorldTexel=result.Extent/Scalar(1024);
            Scalar centerX=Math::Floor(((minimumX+maximumX)*Scalar(.5))/result.WorldTexel+Scalar(.5))*result.WorldTexel;
            Scalar centerY=Math::Floor(((minimumY+maximumY)*Scalar(.5))/result.WorldTexel+Scalar(.5))*result.WorldTexel;
            minimumZ-=Scalar(.05);maximumZ+=Scalar(.05);
            Scalar inverseDepth=Scalar(1)/(maximumZ-minimumZ),inverseHalf=Scalar(2)/result.Extent;
            result.Matrix={x[0]*inverseHalf,y[0]*inverseHalf,z[0]*inverseDepth,0,
                x[1]*inverseHalf,y[1]*inverseHalf,z[1]*inverseDepth,0,
                x[2]*inverseHalf,y[2]*inverseHalf,z[2]*inverseDepth,0,
                -centerX*inverseHalf,-centerY*inverseHalf,-minimumZ*inverseDepth,1};
            return result;
        }
        /// <summary>Rebaselines retained split/scale state after camera or structural scene changes, excluding rotations.</summary>
        static bool RetainStability(const std::vector<Geometry>& geometry,const std::array<Camera,2>& cameras,const Plane& depth,const Result& previous,Result& result) {
            result.StabilitySignature.reserve(102+geometry.size()*13);result.SourceIdentities.reserve(geometry.size()*2);
            result.StabilitySignature.insert(result.StabilitySignature.end(),depth.begin(),depth.end());
            for(const auto& camera:cameras) {
                for(const auto& plane:camera.Planes) result.StabilitySignature.insert(result.StabilitySignature.end(),plane.begin(),plane.end());
                for(const auto& corner:camera.Corners) result.StabilitySignature.insert(result.StabilitySignature.end(),corner.begin(),corner.end());
                result.StabilitySignature.push_back(Scalar(camera.Layers));
            }
            for(const auto& bounds:geometry) {
                result.SourceIdentities.push_back(bounds.Identity);result.SourceIdentities.push_back(bounds.ModelIdentity);
                result.StabilitySignature.push_back(Scalar(bounds.Layers));
                result.StabilitySignature.insert(result.StabilitySignature.end(),bounds.StructuralState.begin(),bounds.StructuralState.end());
            }
            if(!previous.HasReceivers || previous.SourceIdentities!=result.SourceIdentities || previous.StabilitySignature.size()!=result.StabilitySignature.size()) return false;
            for(std::size_t index=0;index<result.StabilitySignature.size();++index) {
                const Scalar current=result.StabilitySignature[index],prior=previous.StabilitySignature[index];
                const Scalar magnitude=std::max(Scalar(1),std::max(Scalar(Math::Abs(current)),Scalar(Math::Abs(prior))));
                // Scale extraction from a rotated IEEE32 source can differ by a few rounding bits.
                if(Math::Abs(current-prior)>magnitude*Scalar(.00001)) return false;
            }
            // Retain the original baseline so individually tiny changes eventually trigger rebaselining.
            result.StabilitySignature=previous.StabilitySignature;
            return true;
        }
        /// <summary>Fits a shared pair of receiver slices from both original output cameras.</summary>
        static Result Fit(const std::vector<Geometry>& geometry,const std::array<Camera,2>& cameras,const Plane& depth,Scalar nearClip,Scalar farClip,const Point& lightForward,const Result& previous) {
            if(nearClip<=Scalar(0) || farClip<=nearClip) throw std::invalid_argument("Cascade fitting requires valid camera clipping planes.");
            Result result{};result.DepthPlane=depth;
            const bool retain=RetainStability(geometry,cameras,depth,previous,result);
            if(geometry.empty()) return result;
            std::vector<Point> visible;visible.reserve(geometry.size()*32);
            for(const auto& bounds:geometry) for(const auto& camera:cameras) if(bounds.Layers&camera.Layers) {
                Intersect(bounds.Corners,camera,depth,nearClip,farClip,visible);
            }
            if(visible.empty()) return result;
            result.HasReceivers=true;
            Scalar minimum=Distance(depth,visible[0]),maximum=minimum;
            for(const auto& point:visible) {minimum=std::min(minimum,Distance(depth,point));maximum=std::max(maximum,Distance(depth,point));}
            // Half-unit depth buckets and retained outer coverage avoid split drift from rotating small props.
            minimum=std::max(nearClip,Scalar(Math::Floor(minimum*Scalar(2))/Scalar(2)));
            maximum=std::min(farClip,Scalar(Math::Ceiling(maximum*Scalar(2))/Scalar(2)));
            if(retain && previous.Depths[2]>previous.Depths[0]) {minimum=std::max(nearClip,std::min(minimum,previous.Depths[0]));maximum=std::min(farClip,std::max(maximum,previous.Depths[2]));}
            if(maximum-minimum<Scalar(.01)) maximum=minimum+Scalar(.01);
            result.Depths={minimum,(minimum+maximum)*Scalar(.5),maximum};
            for(unsigned cascade=0;cascade<2;++cascade) {
                std::vector<Point> receivers;receivers.reserve(geometry.size()*32);
                Scalar rotationExtent=0;
                // Overlap the split by a texel-sized guard to cover exact split and rounding boundaries.
                Scalar guard=retain?std::max(Scalar(.01),Scalar(previous.Cascades[cascade].WorldTexel*Scalar(2))):Scalar(.01);
                for(const auto& bounds:geometry) for(const auto& camera:cameras) if(bounds.Layers&camera.Layers) {
                    const auto previousCount=receivers.size();
                    Intersect(bounds.Corners,camera,depth,Scalar(result.Depths[cascade]-guard),Scalar(result.Depths[cascade+1]+guard),receivers);
                    if(receivers.size()!=previousCount) {
                        // The model-box bounding sphere has rotation-invariant diameter, including props at depth six.
                        Scalar diameter=0;
                        for(unsigned corner=0;corner<4;++corner) {
                            const auto diagonal=Subtract(bounds.Corners[corner],bounds.Corners[corner^7]);
                            diameter=std::max(diameter,Scalar(Math::Sqrt(Dot(diagonal,diagonal))));
                        }
                        rotationExtent=std::max(rotationExtent,diameter);
                    }
                }
                // A scene may occupy only one half of its conservative depth range. Both tiles remain valid.
                if(receivers.empty()) receivers=visible;
                result.Cascades[cascade]=FitRegion(std::move(receivers),geometry,lightForward,retain?previous.Cascades[cascade].Extent:Scalar(0),rotationExtent);
            }
            return result;
        }
    };
}
