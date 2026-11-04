#include "SkewField.hh"
#include "SkewEdges.hh"
#include <cassert>
#include <iostream>
int main(){using namespace bts;SkewField field;field.resolution=64;assert(field.sample(1001,.5,.5)==0);std::vector<float> rgba(64*64*4);for(std::size_t i=0;i<rgba.size();i+=4){rgba[i]=.25;rgba[i+3]=.5;}field.pixels.assign_channel(64,2,rgba);assert(field.sample(1001,.5,.5)==.25f&&field.sample(1002,.5,.5)==0);const auto before=field;field.pixels.assign_channel(64,2,rgba,1002);assert(before.sample(1002,.5,.5)==0&&field.sample(1002,.5,.5)==.25f);field.settings.enabled=false;assert(field.sample(1001,.5,.5)==0);field.settings.enabled=true;assert(field.effective(1001,.5,.5,0,2)==0);field.settings.edge_protection=false;assert(field.effective(1001,.5,.5,0,2)==.25f);field.settings.vector_density=0;bool bad=false;try{field.settings.validate();}catch(...){bad=true;}assert(bad);
 SceneSnapshot scene;UvFaceTriangle a;a.positions={{{0,0,0},{1,0,0},{1,1,0}}};a.vertex_ids={1,2,3};a.boundary_edges=3;a.normals={{{0,0,1},{0,0,1},{0,0,1}}};a.has_normals=true;UvFaceTriangle b=a;b.positions={{{0,0,0},{1,1,0},{0,1,0}}};b.vertex_ids={1,3,4};b.boundary_edges=6;scene.uv_triangles={a,b};assert(skew_hard_edges(scene).size()==4); // Internal tessellation diagonal omitted.
 scene.uv_triangles[0].boundary_edges=7;scene.uv_triangles[1].boundary_edges=7;assert(skew_hard_edges(scene).size()==4); // Continuous vertex normals across a UV seam.
 scene.uv_triangles[1].corners={{{4,8},{4.5f,9},{5,9}}};assert(skew_hard_edges(scene).size()==4);
 scene.uv_triangles[1].normals={{{0,1,0},{0,1,0},{0,1,0}}};assert(skew_hard_edges(scene).size()==5); // Real split normals protect the shared geometry edge.
 std::cout<<"Skew scalar zero/coverage/COW/UDIM/eligibility and hard geometry edges independent of UV seams passed\n";
}
