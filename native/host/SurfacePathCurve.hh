#pragma once
#include "SurfacePath.hh"
#include <functional>
#include <optional>
namespace bts {
using PathVector=std::array<double,3>;
inline double path_length(PathVector v){double length=0;for(double c:v)length+=c*c;return std::sqrt(length);}
inline PathVector path_delta(PathVector a,PathVector b){for(int c=0;c<3;++c)a[c]-=b[c];return a;}
inline PathVector path_plane_vector(PathVector vector,PathVector normal){double dot=0;for(int c=0;c<3;++c)dot+=vector[c]*normal[c];for(int c=0;c<3;++c)vector[c]-=dot*normal[c];return vector;}
inline std::array<PathVector,2> surface_path_handles(const SurfacePath &path,std::size_t index){
  const auto &point=path.points.at(index);if(point.custom)return {point.tangent_in,point.tangent_out};const auto &previous=path.points[index?index-1:path.closed?path.points.size()-1:0],&next=path.points[index+1<path.points.size()?index+1:path.closed?0:index];
  auto incoming=path_delta(previous.position,point.position),outgoing=path_delta(next.position,point.position);
  if(point.smooth){auto tangent=path_plane_vector(path_delta(next.position,previous.position),point.normal);const double length=path_length(tangent);if(length>1e-12){const double before=path_length(incoming)/3,after=path_length(outgoing)/3;for(int c=0;c<3;++c){incoming[c]=-tangent[c]/length*before;outgoing[c]=tangent[c]/length*after;}return {incoming,outgoing};}}
  incoming=path_plane_vector(incoming,point.normal);outgoing=path_plane_vector(outgoing,point.normal);for(int c=0;c<3;++c){incoming[c]/=3;outgoing[c]/=3;}return {incoming,outgoing};
}
inline std::array<PathVector,4> surface_path_controls(const SurfacePath &path,std::size_t segment){
  const auto index=(segment+1)%path.points.size();const auto &a=path.points.at(segment),&b=path.points.at(index);auto controls=std::array{a.position,a.position,b.position,b.position};const auto left=surface_path_handles(path,segment),right=surface_path_handles(path,index);for(int c=0;c<3;++c){controls[1][c]+=left[1][c];controls[2][c]+=right[0][c];}return controls;
}
inline SurfacePathPoint surface_path_at(const SurfacePath &path,std::size_t segment,double t){
  if(!std::isfinite(t)||t<0||t>1||segment>=(path.closed?path.points.size():path.points.size()-1))throw std::invalid_argument("Invalid path curve parameter");
  const auto &a=path.points[segment],&b=path.points[(segment+1)%path.points.size()];SurfacePathPoint point;double normal=0;
  if(a.smooth||b.smooth||a.custom||b.custom){auto controls=surface_path_controls(path,segment); // De Casteljau, as used by Blender curve_bezier.cc.
    for(int level=3;level>0;--level)for(int i=0;i<level;++i)for(int c=0;c<3;++c)controls[i][c]=std::lerp(controls[i][c],controls[i+1][c],t);point.position=controls[0];
  }else for(int c=0;c<3;++c)point.position[c]=std::lerp(a.position[c],b.position[c],t);
  for(int c=0;c<3;++c){point.normal[c]=std::lerp(a.normal[c],b.normal[c],t);normal+=point.normal[c]*point.normal[c];}if(normal<1e-12)throw std::invalid_argument("Path normals oppose; add an intermediate vertex");for(double &v:point.normal)v/=std::sqrt(normal);for(int c=0;c<2;++c)point.uv[c]=std::lerp(a.uv[c],b.uv[c],t);point.pressure=float(std::lerp(a.pressure,b.pressure,t));return point;
}
inline void surface_path_custom_handles(SurfacePath &path,std::size_t index,bool enabled){auto handles=surface_path_handles(path,index);auto &point=path.points.at(index);point.custom=enabled;point.broken=enabled&&!point.smooth;point.tangent_in=enabled?handles[0]:PathVector{};point.tangent_out=enabled?handles[1]:PathVector{};}
inline void surface_path_move_vertex(SurfacePath &path,std::size_t index,const SurfacePathPoint &contact){auto &point=path.points.at(index);point.position=contact.position;point.normal=contact.normal;point.uv=contact.uv;if(point.custom){point.tangent_in=path_plane_vector(point.tangent_in,point.normal);point.tangent_out=path_plane_vector(point.tangent_out,point.normal);}path.validate();}
inline void surface_path_move_handle(SurfacePath &path,std::size_t index,int handle,PathVector value,bool separate,bool scale_both){if(handle<0||handle>1)throw std::invalid_argument("Invalid path handle");if(!path.points.at(index).custom)surface_path_custom_handles(path,index,true);auto &point=path.points[index];const auto original=handle?point.tangent_out:point.tangent_in,opposite=handle?point.tangent_in:point.tangent_out;value=path_plane_vector(value,point.normal);point.broken|=separate;auto other=opposite;const double old_length=path_length(original),new_length=path_length(value),other_length=path_length(opposite)*(scale_both?(old_length>1e-12?new_length/old_length:1):1);
  if(!point.broken&&new_length>1e-12)for(int c=0;c<3;++c)other[c]=-value[c]/new_length*(other_length>1e-12?other_length:new_length);else if(scale_both&&old_length>1e-12)for(double &c:other)c*=new_length/old_length;if(handle){point.tangent_out=value;point.tangent_in=other;}else{point.tangent_in=value;point.tangent_out=other;}path.validate();}
inline void surface_path_reverse(SurfacePath &path){std::reverse(path.points.begin(),path.points.end());for(auto &point:path.points)std::swap(point.tangent_in,point.tangent_out);}
inline std::size_t surface_path_insert(SurfacePath &path,std::size_t segment,double t,const std::function<std::optional<SurfacePathPoint>(const SurfacePathPoint &)> &project){if(t<=1e-6||t>=1-1e-6)throw std::invalid_argument("Choose a position between path vertices");const auto original=surface_path_at(path,segment,t);auto contact=project(original);if(!contact)throw std::invalid_argument("Path insertion has no surface contact");auto point=*contact;point.pressure=original.pressure;const auto next=(segment+1)%path.points.size();const bool curved=path.points[segment].smooth||path.points[next].smooth||path.points[segment].custom||path.points[next].custom;
  if(curved){const auto controls=surface_path_controls(path,segment);std::array<PathVector,3> q;std::array<PathVector,2> r;for(int i=0;i<3;++i)for(int c=0;c<3;++c)q[i][c]=std::lerp(controls[i][c],controls[i+1][c],t);for(int i=0;i<2;++i)for(int c=0;c<3;++c)r[i][c]=std::lerp(q[i][c],q[i+1][c],t);const auto before=surface_path_handles(path,segment),after=surface_path_handles(path,next);auto &a=path.points[segment],&b=path.points[next];a.custom=b.custom=true;a.broken=!a.smooth;b.broken=!b.smooth;a.tangent_in=before[0];a.tangent_out=path_plane_vector(path_delta(q[0],a.position),a.normal);b.tangent_in=path_plane_vector(path_delta(q[2],b.position),b.normal);b.tangent_out=after[1];point.custom=point.smooth=true;point.broken=false;point.tangent_in=path_plane_vector(path_delta(r[0],point.position),point.normal);point.tangent_out=path_plane_vector(path_delta(r[1],point.position),point.normal);}
  path.points.insert(path.points.begin()+segment+1,point);path.validate();return segment+1;
}
struct SurfacePathDisplaySample {SurfacePathPoint point;std::size_t segment=0;double parameter=0;};
}
