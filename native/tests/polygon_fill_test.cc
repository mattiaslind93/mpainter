#include "PolygonFill.hh"
#include "LayerMask.hh"
#include "PaintRecording.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
int main(){
  bts::SceneSnapshot scene;
  const auto triangle=[&](std::array<std::array<float,2>,3> uv,std::array<std::uint64_t,3> verts,int face,int object=1,int material=0){bts::UvFaceTriangle t;t.corners=uv;t.vertex_ids=verts;for(auto &v:t.vertex_ids)v|=std::uint64_t(object)<<32;t.polygon_id=(std::uint64_t(object)<<32)|face;t.triangle_id=(std::uint64_t(object)<<32)|scene.uv_triangles.size();t.material=material;scene.uv_triangles.push_back(t);};
  triangle({{{.1,.1},{.4,.1},{.4,.4}}},{1,2,3},0);triangle({{{.1,.1},{.4,.4},{.1,.4}}},{1,3,4},0);
  triangle({{{.4,.1},{.7,.1},{.7,.4}}},{2,5,6},1);triangle({{{.4,.1},{.7,.4},{.4,.4}}},{2,6,3},1);
  triangle({{{.75,.1},{.95,.1},{.95,.4}}},{5,7,8},2);triangle({{{.75,.1},{.95,.4},{.75,.4}}},{5,8,6},2);
  triangle({{{.1,.6},{.4,.6},{.4,.9}}},{1,2,3},0,2);triangle({{{.6,.6},{.9,.6},{.9,.9}}},{1,2,3},3,1,1);
  const std::array<std::size_t,1> seed{0};
  const auto select=[&](bts::PolygonFillMode mode){return bts::polygon_fill_selection(scene,0,mode,seed);};
  assert(select(bts::PolygonFillMode::Triangle).size()==1);assert(select(bts::PolygonFillMode::Polygon).size()==2);assert(select(bts::PolygonFillMode::Mesh).size()==6);assert(select(bts::PolygonFillMode::Island).size()==4);
  assert(bts::polygon_fill_mode(bts::PolygonFillMode::Auto,false)==bts::PolygonFillMode::Mesh&&bts::polygon_fill_mode(bts::PolygonFillMode::Auto,true)==bts::PolygonFillMode::Island);
  auto single=scene;single.uv_triangles.resize(6);auto detached=single.uv_triangles.front();detached.vertex_ids={90,91,92};detached.polygon_id=(std::uint64_t(1)<<32)|10;single.uv_triangles.push_back(detached);assert(bts::polygon_fill_selection(single,0,bts::PolygonFillMode::Mesh,seed).size()==6);
  assert(bts::uv_triangle_contains(scene.uv_triangles[0],.35,.2)&&!bts::uv_triangle_contains(scene.uv_triangles[0],.15,.35));
  bts::LayerMask mask;mask.background=0;mask.pixels.stamp(128,bts::polygon_fill_stamp(scene,select(bts::PolygonFillMode::Triangle),1));assert(mask.value(44,25)==1&&mask.value(19,44)==0);
  mask.pixels.stamp(128,bts::polygon_fill_stamp(scene,select(bts::PolygonFillMode::Mesh),1));assert(mask.value(19,44)==1&&mask.value(108,25)==1&&mask.value(25,90)==0);
  mask.pixels.stamp(128,bts::polygon_fill_stamp(scene,select(bts::PolygonFillMode::Island),0));assert(mask.value(19,44)==0&&mask.value(108,25)==1);assert(mask.pixels.page_count(0)==0&&mask.pixels.page_count(2)>0);
  bts::LayerMask recorded;recorded.background=0;
  const auto materializations=bts::PaintPixels::recipe_materializations();
  {bts::PaintRecordingScope gpu;
   recorded.pixels.stamp(128,bts::polygon_fill_stamp(scene,select(bts::PolygonFillMode::Triangle),1));
   recorded.pixels.stamp(128,bts::polygon_fill_stamp(scene,select(bts::PolygonFillMode::Mesh),1));
   recorded.pixels.stamp(128,bts::polygon_fill_stamp(scene,select(bts::PolygonFillMode::Island),0));}
  assert(bts::PaintPixels::recipe_materializations()==materializations);
  assert(recorded.pixels.hash(2)==mask.pixels.hash(2));
  std::puts("polygon fill: native polygon/object identity, seam-bounded island flood, material exclusion, triangle raster, full coverage black overwrite and mask-only pixels PASS");
}
