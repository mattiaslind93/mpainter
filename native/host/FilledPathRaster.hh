#include "ResourceBudget.hh"
#pragma once
#include "RibbonRaster.hh"
#include "SceneSnapshot.hh"
#include "PaintRasterSource.hh"
#include "PaintRecording.hh"
#include "PathTriangleBounds.hh"
namespace bts {
namespace filled_path_detail {
using V=PathVector;
struct Shape {
 V origin{},normal{},u{},v{};std::vector<std::array<double,2>> boundary;
 std::array<double,2> low{1e100,1e100},high{-1e100,-1e100};double depth_low=1e100,depth_high=-1e100,depth_margin=0;
 std::array<double,2> project(V p)const{const auto delta=path_delta(p,origin);return {ribbon_detail::dot(delta,u),ribbon_detail::dot(delta,v)};}
 bool contains(V p)const{
   const auto q=project(p);if(q[0]<low[0]||q[0]>high[0]||q[1]<low[1]||q[1]>high[1])return false;
   const double depth=ribbon_detail::dot(path_delta(p,origin),normal);if(depth<depth_low-depth_margin||depth>depth_high+depth_margin)return false;
   // Even/odd winding is explicit for self-crossing contours. UV seams do not
   // enter this test: the boundary and every candidate texel share world space.
   bool inside=false;for(std::size_t i=0,j=boundary.size()-1;i<boundary.size();j=i++){
     const auto &a=boundary[i],&b=boundary[j];if((a[1]>q[1])!=(b[1]>q[1])&&q[0]<(b[0]-a[0])*(q[1]-a[1])/(b[1]-a[1])+a[0])inside=!inside;
   }return inside;
 }
};
inline std::optional<Shape> shape(const SurfacePath &path,double extent,const SurfacePathContact &contact,const SymmetryTransform &copy){
 auto projected=path;for(auto &p:projected.points){p.position=copy.point(p.position);p.normal=copy.vector(p.normal);if(p.custom){p.tangent_in=copy.vector(p.tangent_in);p.tangent_out=copy.vector(p.tangent_out);}}
 // Sampling precision belongs to the contour, independently of the brush size.
 projected.brush.diameter=2;projected.brush.spacing=50;projected.brush.pressure_size=false;
 auto projection=path.brush.projection;projection.alignment=BrushAlignment::TangentWrap;projection.cull=false;projection.orientation=copy.orientation;projection.symmetry_ray=copy.orientation<0;
 const auto dense=dense_surface_path(projected,extent,[&](const SurfacePathPoint &p)->std::optional<SurfacePathPoint>{const auto hit=contact(p,100,false,projection);if(!hit)return p;auto result=p;for(int c=0;c<3;++c)result.position[c]=hit->position[c];return result;});
 if(dense.size()<3)return {};Shape result;result.origin=dense.front().point.position;V average{};
 for(std::size_t i=0;i<dense.size();++i){const auto &a=dense[i].point,&b=dense[(i+1)%dense.size()].point;result.normal=ribbon_detail::sum(result.normal,ribbon_detail::cross(path_delta(a.position,result.origin),path_delta(b.position,result.origin)));average=ribbon_detail::sum(average,a.normal);}
 const double area=path_length(result.normal);if(area<=extent*extent*1e-10)result.normal=average;result.normal=ribbon_detail::unit(result.normal);if(path_length(result.normal)<.5)throw std::invalid_argument("Filled path has no stable surface normal");if(ribbon_detail::dot(result.normal,average)<0)for(double &c:result.normal)c=-c;
 // A single chart handles shallow folds, disconnected surfaces and UV seams.
 // Reject contours that turn through the chart rather than painting a back side.
 for(const auto &sample:dense)if(ribbon_detail::dot(sample.point.normal,result.normal)<.05)throw std::invalid_argument("Filled path folds beyond one surface chart; split it into smaller contours");
 double longest=0;for(const auto &sample:dense){const auto tangent=path_plane_vector(path_delta(sample.point.position,result.origin),result.normal);if(path_length(tangent)>longest){longest=path_length(tangent);result.u=ribbon_detail::unit(tangent);}}
 result.v=ribbon_detail::cross(result.normal,result.u);
 for(const auto &sample:dense){const auto p=result.project(sample.point.position);result.boundary.push_back(p);for(int c=0;c<2;++c){result.low[c]=std::min(result.low[c],p[c]);result.high[c]=std::max(result.high[c],p[c]);}const double depth=ribbon_detail::dot(path_delta(sample.point.position,result.origin),result.normal);result.depth_low=std::min(result.depth_low,depth);result.depth_high=std::max(result.depth_high,depth);}
 // Allow curvature between boundary samples, while excluding distant parallel
 // surfaces. Opposite-facing faces are rejected separately during rasterization.
 if((result.high[0]-result.low[0])*(result.high[1]-result.low[1])<=extent*extent*1e-10)throw std::invalid_argument("Filled path has no projected area; move a vertex or add another point");result.depth_margin=std::max(result.high[0]-result.low[0],result.high[1]-result.low[1])*.5;
 // Remove dense samples that are collinear in the chart. This keeps straight
 // outlines cheap at normal texture sizes without changing their winding.
 std::vector<std::array<double,2>> simple;for(const auto &p:result.boundary){while(simple.size()>1){const auto a=simple[simple.size()-2],b=simple.back();const double dx=p[0]-a[0],dy=p[1]-a[1],length=std::hypot(dx,dy);if(length<1e-12||std::abs((b[0]-a[0])*dy-(b[1]-a[1])*dx)>extent*1e-4*length)break;simple.pop_back();}simple.push_back(p);}result.boundary=std::move(simple);return result;
}
inline std::vector<const Shape *> candidates_for(const std::vector<Shape> &shapes,const std::array<V,3> &position,V normal){ std::vector<const Shape *> candidates;for(const auto &shape:shapes){if(ribbon_detail::dot(normal,shape.normal)<.05)continue;std::array<double,2> low{1e100,1e100},high{-1e100,-1e100};double near=1e100,far=-1e100;for(const auto &p:position){const auto q=shape.project(p);for(int c=0;c<2;++c){low[c]=std::min(low[c],q[c]);high[c]=std::max(high[c],q[c]);}const double z=ribbon_detail::dot(path_delta(p,shape.origin),shape.normal);near=std::min(near,z);far=std::max(far,z);}if(high[0]<shape.low[0]||high[1]<shape.low[1]||low[0]>shape.high[0]||low[1]>shape.high[1]||near>shape.depth_high+shape.depth_margin||far<shape.depth_low-shape.depth_margin)continue;candidates.push_back(&shape);}return candidates;
}
inline ribbon_detail::Raster raster(const SurfacePath &path,const std::vector<Shape> &shapes,const SceneSnapshot &scene,std::size_t material,int resolution,const std::set<int> &tiles,const std::shared_ptr<const QuickMaskField> &mask,PaintPixels::Bounds clip={}){
 ribbon_detail::Raster output;std::uint64_t work=0,covered=0;
 for(const auto &triangle:scene.uv_triangles){if(triangle.material!=material)continue;std::array<V,3> position;for(int i=0;i<3;++i)for(int k=0;k<3;++k)position[i][k]=triangle.positions[i][k];const auto normal=ribbon_detail::unit(ribbon_detail::cross(path_delta(position[1],position[0]),path_delta(position[2],position[0])));
 const auto candidates=candidates_for(shapes,position,normal);if(candidates.empty())continue;
 const auto &a=triangle.corners[0],&b=triangle.corners[1],&c=triangle.corners[2];const double ux=b[0]-a[0],uy=b[1]-a[1],vx=c[0]-a[0],vy=c[1]-a[1],det=ux*vy-uy*vx;if(std::abs(det)<1e-20)continue;
 for(int tile:tiles){const int tile_u=(tile-1001)%10,tile_v=(tile-1001)/10;const auto bound=[&](double v){return int(std::clamp(v,0.,double(resolution)));};int x0=bound(std::floor((std::min({a[0],b[0],c[0]})-tile_u)*resolution)),x1=bound(std::ceil((std::max({a[0],b[0],c[0]})-tile_u)*resolution)),y0=bound(std::floor((std::min({a[1],b[1],c[1]})-tile_v)*resolution)),y1=bound(std::ceil((std::max({a[1],b[1],c[1]})-tile_v)*resolution));if(!clip.empty()){x0=std::max(x0,clip.x0);y0=std::max(y0,clip.y0);x1=std::min(x1,clip.x1);y1=std::min(y1,clip.y1);}if(x0>=x1||y0>=y1)continue;std::uint64_t count=0;for(const auto *s:candidates)count+=s->boundary.size();const auto samples=std::uint64_t(x1-x0)*(y1-y0);work=resource_sum(work,resource_product(samples,count));
 for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){const double u=(x+.5)/resolution+tile_u,v=(y+.5)/resolution+tile_v,w1=((u-a[0])*vy-(v-a[1])*vx)/det,w2=(ux*(v-a[1])-uy*(u-a[0]))/det;if(w1< -1e-9||w2< -1e-9||w1+w2>1+1e-9)continue;V p{};for(int k=0;k<3;++k)p[k]=position[0][k]*(1-w1-w2)+position[1][k]*w1+position[2][k]*w2;bool inside=false;for(const auto *shape:candidates)if(shape->contains(p)){inside=true;break;}if(!inside)continue;const float alpha=mask?mask->sample_uv(u,v,resolution):1;if(alpha<=0)continue;auto &row=output[tile];const auto key=std::uint64_t(y)*resolution+x;if(row.contains(key))continue;if((++covered&4095)==0)require_texture_ram(resource_product(covered,sizeof(ribbon_detail::Texel)+sizeof(std::uint64_t)+4*sizeof(void*)),"Filled path raster texels");auto &texel=row[key];std::copy_n(path.brush.values.base_color.begin(),3,texel.rgba.begin());texel.rgba[3]=alpha;
 }}
 }return output;
}
}
// Prepared world-space charts and triangle metadata, shared by every channel.
// No texels are sampled while constructing a source or recording path edits.
struct FilledPathGeometry {
 SurfacePath path;std::vector<filled_path_detail::Shape> shapes;SceneSnapshot scene;
 std::size_t material=0;std::shared_ptr<const QuickMaskField> mask;
 std::vector<std::vector<int>> candidates;
 std::vector<std::array<double,4>> uv_bounds;
};
struct FilledPathGrid {
 std::shared_ptr<const FilledPathGeometry> geometry;int size=0,tile=1001;
 std::map<std::uint32_t,std::vector<int>> pages;
};
struct FilledPathSource final:PaintRasterSource {
 std::shared_ptr<const FilledPathGrid> grid;int mode=0,user=-1;
 std::array<float,3> value(int channel)const {
  const auto &brush=grid->geometry->path.brush;std::array<float,3> result{};
  if(mode==1)result.fill(brush.values.ambient_occlusion);
  else if(mode==2)result.fill(brush.user_values[user]);
  else if(mode==3)result.fill(brush.displacement_value);
  else switch(channel){
   case 0:std::copy_n(brush.values.base_color.begin(),3,result.begin());break;
   case 1:result.fill(brush.values.metallic);break;
   case 2:result.fill(brush.values.roughness);break;
   case 3:result=brush.values.normal;break;
   case 4:result.fill(brush.values.bump);break;
  }return result;
 }
 PaintPixels evaluate_cpu()const override {
  const auto &g=*grid->geometry;
  PaintPixels result;
  // Export/save may materialize a large contour, but its temporary texel map
  // must remain page-sized. The GPU uses these same conservative page bins.
  for(const auto &[key,indices]:grid->pages){
   SceneSnapshot scene;scene.uv_triangles.reserve(indices.size());
   for(int index:indices)scene.uv_triangles.push_back(g.scene.uv_triangles[index]);
   const int x=int(key%65536)*64,y=int(key/65536)*64;
   const auto raster=filled_path_detail::raster(g.path,g.shapes,scene,g.material,grid->size,{grid->tile},g.mask,{x,y,x+64,y+64});
   const auto pixels=ribbon_detail::pixels(raster,grid->size,g.path,mode,user);
   for(int c=0;c<5;++c){const auto page=pixels.tile(grid->tile).page_data(c,key);if(!page.empty())result.assign_page(grid->size,c,key,page);}
  }return result;
 }
 std::vector<const PaintPixels *> cpu_inputs()const override {
  std::vector<const PaintPixels *> inputs;
  if(grid->geometry->mask)for(const auto &[tile,pixels]:grid->geometry->mask->pixels.tiles())inputs.push_back(&pixels);
  return inputs;
 }
};
inline SurfacePathInk record_filled_path(const SurfacePath &path,std::vector<filled_path_detail::Shape> shapes,
 const SceneSnapshot &scene,std::size_t material,int resolution,const std::array<int,8> &user_sizes,
 const std::set<int> &tiles,std::shared_ptr<const QuickMaskField> mask,int displacement_resolution)
{
 auto geometry=std::make_shared<FilledPathGeometry>();geometry->path=path;geometry->shapes=std::move(shapes);
 geometry->material=material;geometry->mask=std::move(mask);
 for(const auto &triangle:scene.uv_triangles){
  if(triangle.material!=material)continue;
  std::array<PathVector,3> positions;for(int i=0;i<3;++i)for(int c=0;c<3;++c)positions[i][c]=triangle.positions[i][c];
  const auto normal=ribbon_detail::unit(ribbon_detail::cross(path_delta(positions[1],positions[0]),path_delta(positions[2],positions[0])));
  const auto candidates=filled_path_detail::candidates_for(geometry->shapes,positions,normal);
  if(candidates.empty())continue;
  const auto &a=triangle.corners[0],&b=triangle.corners[1],&c=triangle.corners[2];
  if(std::abs(double(b[0]-a[0])*(c[1]-a[1])-double(b[1]-a[1])*(c[0]-a[0]))<1e-20)continue;
  std::array<PathClipVertex,3> clip_triangle;
  for(int i=0;i<3;++i){clip_triangle[i].position=positions[i];for(int c=0;c<2;++c)clip_triangle[i].uv[c]=triangle.corners[i][c];}
  std::optional<std::array<double,4>> uv_bounds;
  for(const auto *shape:candidates){
   std::vector<PathClipPlane> planes;
   const auto axis=[&](PathVector direction,double low,double high){
    const double offset=ribbon_detail::dot(shape->origin,direction),margin=1e-6*std::max({1.,std::abs(offset),std::abs(low),std::abs(high)});
    planes.push_back({direction,offset+high+margin});for(double &v:direction)v=-v;
    planes.push_back({direction,-offset-low+margin});
   };
   axis(shape->u,shape->low[0],shape->high[0]);axis(shape->v,shape->low[1],shape->high[1]);
   axis(shape->normal,shape->depth_low-shape->depth_margin,shape->depth_high+shape->depth_margin);
   if(const auto bounds=path_triangle_bounds(clip_triangle,planes)){
    if(!uv_bounds)uv_bounds=bounds;else for(int c=0;c<2;++c){(*uv_bounds)[c]=std::min((*uv_bounds)[c],(*bounds)[c]);(*uv_bounds)[c+2]=std::max((*uv_bounds)[c+2],(*bounds)[c+2]);}
   }
  }
  if(!uv_bounds)continue;
  geometry->uv_bounds.push_back(*uv_bounds);
  geometry->scene.uv_triangles.push_back(triangle);auto &indices=geometry->candidates.emplace_back();
  for(const auto *shape:candidates)indices.push_back(int(shape-geometry->shapes.data()));
 }
 std::map<std::pair<int,int>,std::shared_ptr<const FilledPathGrid>> grids;
 const auto render=[&](int size,int mode,int user=-1){
  TilePaintPixels result;
  for(int tile:tiles){
   auto &grid=grids[{size,tile}];
   if(!grid){auto value=std::make_shared<FilledPathGrid>();value->geometry=geometry;value->size=size;value->tile=tile;
    const int tile_u=(tile-1001)%10,tile_v=(tile-1001)/10;
    for(std::size_t i=0;i<geometry->scene.uv_triangles.size();++i){const auto &bounds=geometry->uv_bounds[i];
     const auto bound=[&](double v){return int(std::clamp(v,0.,double(size)));};
     const int x0=bound(std::floor((bounds[0]-tile_u)*size));
     const int x1=bound(std::ceil((bounds[2]-tile_u)*size));
     const int y0=bound(std::floor((bounds[1]-tile_v)*size));
     const int y1=bound(std::ceil((bounds[3]-tile_v)*size));
     if(x0>=x1||y0>=y1)continue;
     for(int y=y0/64;y<=(y1-1)/64;++y)for(int x=x0/64;x<=(x1-1)/64;++x)value->pages[(std::uint32_t(y)<<16)|x].push_back(int(i));
    }grid=std::move(value);
   }
   PaintPixels::PageVersions versions;
   for(int c=0;c<5;++c)if(mode==0?path.brush.channels[c]:c==2)
    for(const auto &[key,indices]:grid->pages)versions[c][key]=1;
   auto source=std::make_shared<FilledPathSource>();source->grid=grid;source->mode=mode;source->user=user;
   result.assign_tile(PaintPixels::from_source(size,std::move(source),versions),tile);
  }return result;
 };
 SurfacePathInk output;output.primary.paint=render(resolution,0);
 if(path.brush.ao)output.primary.ao=render(resolution,1);
 if(path.brush.displacement)output.displacement=render(displacement_resolution?displacement_resolution:resolution,3);
 for(int slot=0;slot<8;++slot)if(path.brush.users[slot])output.users[slot]=render(user_sizes[slot],2,slot);
 return output;
}

inline SurfacePathInk raster_filled_path(const SurfacePath &path,double extent,int resolution,const std::array<int,8> &user_resolutions,const std::set<int> &tiles,const SurfacePathContact &contact,const SceneSnapshot &scene,std::size_t material,std::shared_ptr<const QuickMaskField> mask={},int displacement_resolution=0){
 path.validate();if(!path.closed||path.points.size()<3||path.brush.stroke_opacity<=0)return {};std::vector<filled_path_detail::Shape> shapes;auto copies=symmetry_transforms(path.symmetry);copies.insert(copies.begin(),SymmetryTransform{});for(const auto &copy:copies)if(auto shape=filled_path_detail::shape(path,extent,contact,copy))shapes.push_back(std::move(*shape));if(record_gpu_paint)return record_filled_path(path,std::move(shapes),scene,material,resolution,user_resolutions,tiles,std::move(mask),displacement_resolution);SurfacePathInk output;std::map<int,ribbon_detail::Raster> cache;const auto render=[&](int size)->const ribbon_detail::Raster &{auto it=cache.find(size);if(it==cache.end())it=cache.emplace(size,filled_path_detail::raster(path,shapes,scene,material,size,tiles,mask)).first;return it->second;};output.primary.paint=ribbon_detail::pixels(render(resolution),resolution,path,0);if(path.brush.ao)output.primary.ao=ribbon_detail::pixels(render(resolution),resolution,path,1);if(path.brush.displacement){const int size=displacement_resolution?displacement_resolution:resolution;output.displacement=ribbon_detail::pixels(render(size),size,path,3);}for(int slot=0;slot<8;++slot)if(path.brush.users[slot])output.users[slot]=ribbon_detail::pixels(render(user_resolutions[slot]),user_resolutions[slot],path,2,slot);return output;
}
}
