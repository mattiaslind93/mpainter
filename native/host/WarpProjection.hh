#pragma once
#include "PlanarProjection.hh"
namespace bts {
inline std::shared_ptr<const WarpGrid> warp_authored_grid(const FillProjection &p){static const auto initial=std::make_shared<const WarpGrid>();return p.warp?p.warp:initial;}
inline bool warp_geometry_enabled(const FillProjection &p){return p.mode==5&&p.warp_geometry&&p.warp_geometry->enabled;}
inline bool warp_fit_pose_matches(const FillProjection &p){return p.warp_fit&&p.warp_fit->authored==warp_authored_grid(p)&&p.warp_fit->offset==p.offset3&&p.warp_fit->rotation==p.rotation3&&p.warp_fit->scale==p.scale3&&p.warp_fit->physical==p.physical&&(!p.physical||(p.warp_fit->physical_size==fill_physical_size(p)&&p.warp_fit->cm_per_native==p.cm_per_native));}
inline std::shared_ptr<const WarpGrid> fill_warp_grid(const FillProjection &p){return warp_geometry_enabled(p)&&warp_fit_pose_matches(p)?p.warp_fit->fitted.grid:warp_authored_grid(p);}
inline WarpFitFrame warp_projection_frame(const FillProjection &p,const WarpVector &center,double extent){
 WarpFitFrame frame;for(int c=0;c<3;++c)frame.origin[c]=center[c]+p.offset3[c]*extent;const auto size=fill_physical_size(p);
 for(int column=0;column<3;++column){WarpVector axis{};const int physical_axis=column==1?2:column==2?1:0;axis[column]=p.scale3[column]*(p.physical?size[physical_axis]/p.cm_per_native:extent);
   for(int rotation=0;rotation<3;++rotation){const double angle=p.rotation3[rotation]*std::numbers::pi/180,c=std::cos(angle),sn=std::sin(angle);const int i=(rotation+1)%3,j=(rotation+2)%3;const auto before=axis;axis[i]=c*before[i]-sn*before[j];axis[j]=sn*before[i]+c*before[j];}frame.axes[column]=axis;
 }frame.validate();return frame;
}
inline FillProjection rebase_warp_view_edit(FillProjection p){if(warp_geometry_enabled(p)&&p.warp_fit&&warp_authored_grid(p)!=p.warp_fit->authored){p.warp=rebase_warp_edit(*p.warp_fit->authored,*p.warp_fit->fitted.grid,*warp_authored_grid(p));p.warp_fit.reset();}return p;}
inline FillProjection toggle_warp_geometry(FillProjection p,bool enabled){const auto next=change_warp_geometry_mode(warp_authored_grid(p),p.warp_geometry,warp_fit_pose_matches(p)?p.warp_fit->fitted.grid:nullptr,enabled);p.warp=next.grid;p.warp_geometry=next.settings;if(enabled)p.warp_fit.reset();return p;}
inline std::optional<WarpCoordinate> fill_warp_coordinate(const FillProjection &p,const PlanarFrame &frame){
 const auto grid=fill_warp_grid(p);WarpVector metric;const double depth_scale=std::abs(frame.denominators[1]);if(depth_scale<1e-12)return {};for(int c=0;c<3;++c)metric[c]=std::pow(frame.denominators[c]/depth_scale,2);
 // Each channel visits the same immutable surface sample. Retain the grid owner
 // with the cache key so pointer reuse can never return an old patch solution.
 struct Cache {std::shared_ptr<const WarpGrid> grid;WarpVector point{},metric{};std::optional<WarpCoordinate> coordinate;};static thread_local Cache cache;
 if(cache.grid!=grid||cache.point!=frame.position||cache.metric!=metric){cache={grid,frame.position,metric,grid->project(frame.position,metric)};}return cache.coordinate;
}
inline float fill_warp_coverage(const FillProjection &p,const PlanarFrame &frame,const WarpCoordinate &coordinate){
 // A fitted Hermite patch approximates the triangulated source surface and can
 // cross it between control points. Keep a symmetric depth envelope for both
 // the live fit and its frozen off-state; legacy authored projectors stay one-sided.
 const bool fitted_envelope=p.warp_geometry&&(p.warp_geometry->enabled||p.warp_geometry->frozen_reference);
 const double reach=fitted_envelope?std::abs(coordinate.depth):-coordinate.depth;if(reach<-.000001||reach>p.warp_depth)return 0;if(p.shape_crop&&(coordinate.u<0||coordinate.u>1||coordinate.v<0||coordinate.v>1))return 0;double alpha=1;
 if(p.depth_culling&&p.depth_hardness<1)alpha*=std::clamp((p.warp_depth-reach)/(p.warp_depth*(1-p.depth_hardness)),0.,1.);
 if(p.backface_culling){WarpVector source;for(int c=0;c<3;++c)source[c]=frame.normal[c]/frame.denominators[c];source=warp_normal(source);double facing=0;for(int c=0;c<3;++c)facing+=source[c]*coordinate.normal[c]*std::copysign(1.,frame.denominators[c]);const double threshold=std::cos(p.backface_angle*std::numbers::pi/180);if(p.backface_angle<1e-6)alpha*=facing>=1-1e-8?1:0;else alpha*=std::clamp((facing-threshold)/std::max(1e-8,(1-threshold)*(1-p.backface_hardness)),0.,1.);}
 return float(alpha);
}
inline std::array<float,4> fill_warp_sample(const FillProjection &p,const FillSurfacePoint &surface,const FillSurfaceAtlas &atlas,bool resource=true){
 if(!surface.valid||atlas.extent<=0)return {};const auto frame=fill_planar_frame(p,surface,atlas);const auto coordinate=fill_warp_coordinate(p,frame);if(!coordinate)return {};const float coverage=fill_warp_coverage(p,frame,*coordinate);if(coverage<=0)return {};if(!resource||(!p.atom&&!p.texture))return {1,1,1,coverage};
 WarpVector du=coordinate->du,dv=coordinate->dv;for(int c=0;c<3;++c){du[c]*=frame.denominators[c];dv[c]*=frame.denominators[c];}const double a=warp_dot(du,du),b=warp_dot(du,dv),c=warp_dot(dv,dv),det=a*c-b*b;if(det<1e-18)return {};const double footprint_u=surface.footprint*std::sqrt(c/det),footprint_v=surface.footprint*std::sqrt(a/det);
 auto mapping=p;mapping.physical=false;std::array<float,4> result{};const int samples=p.filtering==0?4:1;
 for(int sample=0;sample<samples;++sample){const double dx=samples==1?0:(sample&1?.25:-.25),dy=samples==1?0:(sample&2?.25:-.25);const auto uv=fill_uv_map(mapping,coordinate->u+dx*footprint_u,coordinate->v+dy*footprint_v);const auto pixel=fill_planar_resource(p,uv[0],uv[1]);for(int c=0;c<3;++c)result[c]+=pixel[c]*pixel[3]/samples;result[3]+=pixel[3]/samples;}
 if(result[3]>1e-8f)for(int c=0;c<3;++c)result[c]/=result[3];result[3]*=coverage;return result;
}
}
