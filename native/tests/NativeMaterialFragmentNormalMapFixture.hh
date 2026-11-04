#pragma once
#include "NativeMaterialFragmentGpuFixture.hh"
#include "../host/NativeMaterialFragmentGpuCompiler.hh"
#include <algorithm>
#include <array>
#include <utility>
#include <cmath>
#include <limits>

namespace bts::native_fragment_normal_map_test {
// Real source group. PreparedNativeFragmentNormal alone creates the AOV.
inline DetachedNativeMaterial source(bool directx=false,bool nested_bump=false,
                                    bool alternate_basis=false,int space=blender::SHD_SPACE_TANGENT){
 using namespace blender;auto result=native_fragment_gpu_test::source();
 auto &tree=*result.material->nodetree;bNodeTree *group=nullptr;bNode *instance=nullptr;
 for(auto &n:tree.nodes)if(STREQ(n.idname,"ShaderNodeGroup")){instance=&n;group=reinterpret_cast<bNodeTree *>(n.id);break;}
 native_raw_height_test::require(group&&instance,"NormalMap source group missing");
 bNode *pbr=nullptr,*bump=nullptr,*inputs=nullptr;
 for(auto &n:group->nodes){if(STREQ(n.idname,"ShaderNodeBsdfPrincipled"))pbr=&n;if(STREQ(n.idname,"ShaderNodeBump"))bump=&n;if(STREQ(n.idname,"NodeGroupInput"))inputs=&n;}
 native_raw_height_test::require(pbr&&bump&&inputs,"NormalMap source nodes missing");
 const auto strength=std::string(group->tree_interface.add_socket("Normal Strength","","NodeSocketFloat",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier);
 const auto color=std::string(group->tree_interface.add_socket("Normal Color","","NodeSocketColor",NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier);
 auto *normal=bke::node_add_node(nullptr,*group,UString("ShaderNodeNormalMap"));normal->custom1=alternate_basis?1:0;
 auto &storage=*static_cast<NodeShaderNormalMap *>(normal->storage);STRNCPY(storage.uv_map,"FixtureUV");storage.space=space;storage.convention=directx?SHD_NORMAL_MAP_CONVENTION_DIRECTX:SHD_NORMAL_MAP_CONVENTION_OPENGL;storage.base=SHD_NORMAL_MAP_BASE_ORIGINAL;
 BKE_ntree_update_after_single_tree_change(*result.main,*group);BKE_ntree_update_after_single_tree_change(*result.main,tree);
 const auto link=[&](bNode &a,const char *output,bNode &b,const char *input){bke::node_add_link(*group,a,native_raw_height_test::socket(a,output,false),b,native_raw_height_test::socket(b,input,true));};
 link(*inputs,strength.c_str(),*normal,"Strength");link(*inputs,color.c_str(),*normal,"Color");
 auto &target=native_raw_height_test::socket(*pbr,"Normal",true);if(target.link)bke::node_remove_link(group,*target.link);
 if(nested_bump){link(*normal,"Normal",*bump,"Normal");link(*bump,"Normal",*pbr,"Normal");}else link(*normal,"Normal",*pbr,"Normal");
 static_cast<bNodeSocketValueFloat *>(native_raw_height_test::socket(*instance,strength.c_str(),true).default_value)->value=.65f;
 auto &rgba=*static_cast<bNodeSocketValueRGBA *>(native_raw_height_test::socket(*instance,color.c_str(),true).default_value);rgba.value[0]=.2f;rgba.value[1]=.7f;rgba.value[2]=1;rgba.value[3]=1;
 BKE_ntree_update_after_single_tree_change(*result.main,*group);BKE_ntree_update_after_single_tree_change(*result.main,tree);return result;
}
// Independent double-vector oracle, no shader source/evaluator/helper reuse.
using V=std::array<double,3>;using T=std::array<double,4>;
inline V cross(const V&a,const V&b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline V unit(V a){const auto q=a[0]*a[0]+a[1]*a[1]+a[2]*a[2];if(q<=1e-35)return {1,0,0};const auto length=std::sqrt(q);for(auto &v:a)v/=length;return a;}
// Interpolate transformed corner tangent and original world Ni separately.
// Ni is deliberately NOT normalized after interpolation.
inline std::pair<T,V> interpolate(const std::array<NativeFragmentVertex,3>&v,
                                 const std::array<double,3>&weights,
                                 const std::array<float,9>&world_to_object){
 T tangent{};V ni{};
 for(int corner=0;corner<3;++corner){V transformed{};
  for(int out=0;out<3;++out)for(int in=0;in<3;++in)transformed[out]+=double(v[corner].tangent[in])*world_to_object[out*3+in];
  transformed=unit(transformed);
  for(int c=0;c<3;++c){tangent[c]+=weights[corner]*transformed[c];ni[c]+=weights[corner]*v[corner].initial_normal[c];}
  tangent[3]+=weights[corner]*v[corner].tangent[3];
 }return {tangent,ni};
}
inline V expected(T tangent,V ni,const V &color,double strength,bool front,bool negative_scale,bool directx){
 if(tangent==T{0,0,0,1})return ni; // exact stock sentinel, raw original Ni.
 if(!front)for(auto &v:tangent)v=-v;
 const auto bitangent=cross(ni,V{tangent[0],tangent[1],tangent[2]});
 V normal{2*color[0]-1,2*color[1]-1,2*color[2]-1};if(directx)normal[1]=-normal[1];
 normal[0]*=strength;normal[1]*=strength;normal[2]=1+std::clamp(strength,0.,1.)*(normal[2]-1);
 V result{};for(int c=0;c<3;++c)result[c]=normal[0]*tangent[c]+normal[1]*tangent[3]*(negative_scale?-1 : 1)*bitangent[c]+normal[2]*ni[c];return unit(result);
}
// Source-only focused metadata assertions, to be called by the existing native
// diagnostic after review. Fixed algebraic witnesses do not reuse GLSL code.
inline int verify_metadata(){
 int checks=0;const auto check=[&](bool ok,const char *why){native_raw_height_test::require(ok,why);++checks;};
 const auto close=[&](V a,V b){for(int c=0;c<3;++c)check(std::abs(a[c]-b[c])<1e-12,"Independent NormalMap algebra witness failed");};
 const double l=std::sqrt(1.52);const V ni{0,0,1},color{.8,.3,1};
 close(expected({1,0,0,1},ni,color,1,true,false,false),{.6/l,-.4/l,1/l});
 close(expected({1,0,0,1},ni,color,1,false,false,false),{-.6/l,-.4/l,1/l});
 close(expected({1,0,0,1},ni,color,1,true,true,false),{.6/l,.4/l,1/l});
 close(expected({1,0,0,-1},ni,color,1,true,false,false),{.6/l,.4/l,1/l});
 close(expected({0,1,0,1},ni,color,1,true,false,false),{.4/l,.6/l,1/l});
 close(expected({1,0,0,1},ni,color,1,true,false,true),{.6/l,.4/l,1/l});
 close(expected({1,0,0,1},ni,color,0,true,false,false),ni);
 close(expected({0,0,0,1},{.1,0,.8},color,1,false,true,false),{.1,0,.8});
 std::array<NativeFragmentVertex,3> corners{};for(auto &v:corners){v.tangent={1,1,0,1};v.initial_normal={0,0,1};}
 corners[1].initial_normal={.6f,0,.8f};const std::array<float,9> nonuniform{.5f,0,0,0,1,0,0,0,2};
 const auto [t,n]=interpolate(corners,{.5,.5,0},nonuniform);check(std::abs(t[0]-1/std::sqrt(5.))<1e-12&&std::abs(t[1]-2/std::sqrt(5.))<1e-12,"NormalMap inverse-transpose must occur before interpolation");
 check(std::abs(n[0]-.5*double(corners[1].initial_normal[0]))<1e-12&&std::abs(n[2]-(.5+.5*double(corners[1].initial_normal[2])))<1e-12,"NormalMap Ni was renormalized after interpolation");
 std::vector<NativeFragmentVertex> mesh(12);const std::array<unsigned char,4> front{0,1,0,1};
 for(int triangle=0;triangle<4;++triangle){auto *v=mesh.data()+3*triangle;v[0].raster_uv={0,0};v[1].raster_uv={1,0};v[2].raster_uv={0,1};if(triangle<2)std::swap(v[1],v[2]);for(int c=0;c<3;++c)v[c].position[0]=float(triangle);}
 const auto grouped=prepare_native_fragment_facing(mesh,front);check(grouped.batches.size()==4,"Physical facing did not preserve four independent groups");
 for(int group=0;group<4;++group){const auto &b=grouped.batches[group];check(b.first==unsigned(3*group)&&b.count==3&&b.invert_front_face==bool((group>=2)!=(group%2!=0)),"Physical facing batch differs from explicit raster/physical contract");check(grouped.vertices[b.first].position[0]==group,"Physical-facing partition changed geometry identity");}
 auto damaged=mesh;damaged[1].raster_uv=damaged[0].raster_uv;damaged[4].raster_uv[0]=std::numeric_limits<float>::quiet_NaN();
 const auto filtered=prepare_native_fragment_facing(damaged,front);check(filtered.vertices.size()==6&&filtered.batches.size()==2,"One degenerate raster triangle rejected physical-facing geometry");check(filtered.vertices[0].position[0]==2&&filtered.vertices[3].position[0]==3,"Raster filtering changed retained triangle ownership");
 {TextureRamQueryScope no_ram(+[]()->std::uint64_t{return 0;});(void)prepare_native_fragment_facing(mesh,front);}
 return checks;
}

}
