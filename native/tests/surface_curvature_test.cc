#include "SurfaceCurvature.hh"
#include "GpuBakeBvh.hh"
#include <cassert>
#include <iostream>
#include <numbers>
using namespace bts;
static SceneSnapshot sphere(int segments,int rings,float radius,bool inward=false){
  SceneSnapshot s;s.minimum={-radius,-radius,-radius};s.maximum={radius,radius,radius};
  const auto vertex=[&](int x,int y){const double u=x*2*std::numbers::pi/segments,v=y*std::numbers::pi/rings;return std::array<float,3>{float(radius*std::sin(v)*std::cos(u)),float(radius*std::sin(v)*std::sin(u)),float(radius*std::cos(v))};};
  for(int y=0;y<rings;++y)for(int x=0;x<segments;++x)for(int half=0;half<2;++half){
    if((y==0&&half==0)||(y==rings-1&&half==1))continue;
    UvFaceTriangle tri;tri.positions=half==0?std::array{vertex(x,y),vertex(x,y+1),vertex(x+1,y)}:std::array{vertex(x+1,y),vertex(x,y+1),vertex(x+1,y+1)};
    tri.has_normals=true;for(int k=0;k<3;++k){tri.normals[k]=normalized_normal(tri.positions[k]);if(inward)for(float &v:tri.normals[k])v=-v;tri.vertex_ids[k]=(std::uint64_t(1)<<32)|std::uint32_t(s.uv_triangles.size()*3+k);}
    s.uv_triangles.push_back(tri);
  }
  return s;
}
static SceneSnapshot tetrahedron(float scale,bool inward=false){
  SceneSnapshot scene;scene.minimum={-scale,-scale,-scale};scene.maximum={scale,scale,scale};
  const std::array<std::array<float,3>,4> points{{{scale,scale,scale},{-scale,-scale,scale},{-scale,scale,-scale},{scale,-scale,-scale}}};
  const std::array<std::array<int,3>,4> faces{{{0,2,1},{0,1,3},{0,3,2},{1,2,3}}};
  for(const auto &face:faces){UvFaceTriangle tri;tri.has_normals=true;
    for(int k=0;k<3;++k){tri.positions[k]=points[face[k]];tri.normals[k]=normalized_normal(tri.positions[k]);if(inward)for(float &v:tri.normals[k])v=-v;
      tri.vertex_ids[k]=(std::uint64_t(1)<<32)|std::uint32_t(scene.uv_triangles.size()*3+k);tri.corners[k]={float(scene.uv_triangles.size()),float(k)};}
    scene.uv_triangles.push_back(tri);
  }
  return scene;
}
static SceneSnapshot warped_quad(bool alternate){
  SceneSnapshot scene;scene.minimum={0,0,0};scene.maximum={1,1,1};
  const std::array<std::array<float,3>,4> points{{{0,0,0},{1,0,0},{1,1,1},{0,1,0}}};
  const auto faces=alternate?std::array<std::array<int,3>,2>{{{0,1,3},{1,2,3}}}:std::array<std::array<int,3>,2>{{{0,1,2},{0,2,3}}};
  for(const auto &face:faces){UvFaceTriangle tri;tri.has_normals=true;tri.polygon_id=std::uint64_t(1)<<32;
    for(int k=0;k<3;++k){const int a=face[k],b=face[(k+1)%3];tri.positions[k]=points[a];tri.normals[k]=normalized_normal({-points[a][1],-points[a][0],1});tri.vertex_ids[k]=(std::uint64_t(1)<<32)|a;
      if(std::abs(a-b)==1||std::abs(a-b)==3)tri.boundary_edges|=1<<k;}
    scene.uv_triangles.push_back(tri);
  }
  return scene;
}
int main(){
  const float convex=std::acos(-std::sqrt(2.f/3))/std::numbers::pi_v<float>;
  for(float scale:{.001f,1.f,1000.f})for(bool inward:{false,true}){
    auto scene=tetrahedron(scale,inward);SurfaceCurvature field(scene,false);field.calculate_pointiness();
    assert(field.vertices.size()==4);
    for(const auto &v:field.vertices)assert(std::abs(v.curvature-(inward?1-convex:convex))<1e-6f);
    const auto first=field.vertices;field.calculate_pointiness();
    for(std::size_t i=0;i<first.size();++i)assert(first[i].curvature==field.vertices[i].curvature);
    const auto duplicate=scene.uv_triangles.front();scene.uv_triangles.push_back(duplicate);
    SurfaceCurvature duplicated(scene,false);duplicated.calculate_pointiness();
    for(const auto &v:duplicated.vertices)assert(std::abs(v.curvature-(inward?1-convex:convex))<1e-6f);
  }
  auto quad_a=warped_quad(false),quad_b=warped_quad(true);SurfaceCurvature a(quad_a,false),b(quad_b,false);a.calculate_pointiness();b.calculate_pointiness();
  for(const auto &v:a.vertices){const auto other=std::find_if(b.vertices.begin(),b.vertices.end(),[&](const auto &w){return v.p==w.p;});assert(other!=b.vertices.end());assert(std::abs(v.curvature-other->curvature)<1e-6f);}
  for(float scale:{1e-5f,1.f,1e5f}){
    const bake_bvh::Triangle triangle{{{0,0,scale},{scale,0,scale},{0,scale,scale}}};
    const float origin[3]{scale*.25f,scale*.25f,0},direction[3]{0,0,1};float distance=0;
    assert(bake_bvh::surface_hit(triangle,origin,direction,distance));assert(std::abs(distance/scale-1)<1e-6f);
  }
  // Analytic mean curvature, invariant to UV duplication, tessellation, scale.
  for(float radius:{.001f,1.f,1000.f})for(int density:{12,32})for(bool inward:{false,true}){
    auto s=sphere(density,density/2,radius,inward);SurfaceCurvature field(s,false);field.calculate(radius*.3f);
    double error=0;for(const auto &v:field.vertices){assert(std::isfinite(v.curvature));error=std::max(error,double(std::abs(v.curvature*radius-(inward?-1.f:1.f))));}
    assert(field.vertices.size()<s.uv_triangles.size());assert(error<.003);std::cout<<"sphere "<<radius<<" density "<<density<<" error "<<error<<'\n';
    field.calculate(radius*.8f);for(const auto &v:field.vertices)assert(std::abs(v.curvature*radius-(inward?-1.f:1.f))<.003f);
  }
  // A plane stays neutral; a different object at the same coordinates cannot weld.
  SceneSnapshot flat;flat.minimum={0,0,0};flat.maximum={1,1,0};
  for(int object=1;object<=2;++object){UvFaceTriangle t;t.positions={{{0,0,0},{1,0,0},{0,1,0}}};t.normals={{{0,0,1},{0,0,1},{0,0,1}}};t.has_normals=true;for(int k=0;k<3;++k)t.vertex_ids[k]=(std::uint64_t(object)<<32)|k;flat.uv_triangles.push_back(t);}
  SurfaceCurvature plane(flat,true);plane.calculate(.2f);assert(plane.vertices.size()==6);for(const auto &v:plane.vertices)assert(std::abs(v.curvature)<1e-8);
  plane.calculate_pointiness();for(const auto &v:plane.vertices)assert(v.curvature==.5f);
  plane.calculate_pointiness(.2f);for(const auto &v:plane.vertices)assert(v.curvature==.5f);
  // Every ray starts on the correct side at large coordinates and grazing normals.
  for(bool inward:{false,true}){bake_bvh::Frame f;assert(bake_bvh::frame(f,{10000,10000,10000},normalized_normal({.9f,0,.1f}),7,2,inward));
    for(int sample=0;sample<256;++sample){std::array<float,4> o,d;bake_bvh::surface_ray(f,sample,256,{0,0,1},inward,1e-6f,o,d);assert(inward?o[2]<10000:o[2]>10000);assert(inward?d[2]<=0:d[2]>=0);}
  }
  std::cout<<"surface curvature and ray offset PASS\n";
}
