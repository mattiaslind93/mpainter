#pragma once
#include "RibbonRaster.hh"
#include "GpuBrushRaster.hh"
#include "TextureMemory.hh"
#include "GpuRibbonShader.hh"
namespace bts {
// Rasterize only requested immutable authoring pages. Shared charts/geometry
// stay on the GPU across material channels and path-history consumers.
class GpuRibbonPath {
 friend class GpuPaintPages;
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 using Shader=std::unique_ptr<blender::gpu::Shader,decltype(&blender::GPU_shader_free)>;
 using Row=std::array<float,4>;
 struct GeometryEntry {std::uint64_t bytes=0,used=0;std::weak_ptr<const RibbonPathGeometry> source;Texture image{nullptr,blender::GPU_texture_free};};
 struct GridEntry {std::uint64_t bytes=0,used=0;std::weak_ptr<const RibbonPathGrid> source;Texture image{nullptr,blender::GPU_texture_free};std::map<std::uint32_t,std::array<int,2>> ranges;};
 std::map<const RibbonPathGeometry *,GeometryEntry> geometry_;
 std::map<const RibbonPathGrid *,GridEntry> grids_;
 struct ImageEntry {std::weak_ptr<const RibbonImage> source;Texture image{nullptr,blender::GPU_texture_free};std::uint64_t bytes=0,used=0;};
 std::map<const RibbonImage *,ImageEntry> images_;std::uint64_t image_allocations_=0;
 std::uint64_t clock_=0;
 static std::uint64_t row_bytes(std::uint64_t rows){const auto width=std::min<std::uint64_t>(1024,std::max<std::uint64_t>(1,rows));return width*std::max<std::uint64_t>(1,(rows+width-1)/width)*16;}
 std::uint64_t owned()const{std::uint64_t bytes=0;for(const auto &[key,entry]:geometry_)bytes+=entry.bytes;for(const auto &[key,entry]:grids_)bytes+=entry.bytes;for(const auto &[key,entry]:images_)bytes+=entry.bytes;return bytes;}
 void reserve(std::uint64_t requested,const RibbonPathGeometry *geometry,const RibbonPathGrid *grid,const RibbonImage *image){
  const auto free=texture_free_vram.load(),budget=resource_sum(owned(),(texture_vram_query_valid.load()||free!=0)?free:texture_available_ram_bytes());
  while(owned()+requested>budget){
   auto g=geometry_.end();for(auto i=geometry_.begin();i!=geometry_.end();++i)if(i->first!=geometry&&(g==geometry_.end()||i->second.used<g->second.used))g=i;
   auto p=grids_.end();for(auto i=grids_.begin();i!=grids_.end();++i)if(i->first!=grid&&(p==grids_.end()||i->second.used<p->second.used))p=i;
   auto r=images_.end();for(auto i=images_.begin();i!=images_.end();++i)if(i->first!=image&&(r==images_.end()||i->second.used<r->second.used))r=i;
   if(g==geometry_.end()&&p==grids_.end()&&r==images_.end())break;
   const auto age=[](const auto &it,const auto &map){return it==map.end()?UINT64_MAX:it->second.used;};
   if(age(g,geometry_)<=age(p,grids_)&&age(g,geometry_)<=age(r,images_))geometry_.erase(g);
   else if(age(p,grids_)<=age(r,images_))grids_.erase(p);else images_.erase(r);
  }
 }
 Shader shader_{nullptr,blender::GPU_shader_free};
 static Texture upload(std::vector<Row> rows){
  using namespace blender;
  const int width=std::min<int>(1024,std::max<std::size_t>(1,rows.size())),height=int((rows.size()+width-1)/width);
  if(height>GPU_max_texture_size())throw std::length_error("Ribbon GPU metadata exceeds device texture dimensions");
  rows.resize(std::size_t(width)*std::max(1,height));
  Texture result(GPU_texture_create_2d("BTS shared ribbon metadata",width,std::max(1,height),1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,rows.front().data()),GPU_texture_free);
  if(!result)throw std::bad_alloc();return result;
 }
 static blender::gpu::shader::ShaderCreateInfo program_info(){
  using namespace blender;using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16,16).sampler(0,ImageType::Float2D,"geometry_img").sampler(1,ImageType::Float2D,"indices_img").sampler(2,ImageType::Float2D,"coverage_img").sampler(3,ImageType::Float2D,"source_img")
   .image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,ImageReadWriteType::image2D,"result_img")
   .push_constant(Type::int2_t,"page_origin").push_constant(Type::int2_t,"triangle_range")
   .push_constant(Type::int2_t,"tile_offset").push_constant(Type::int_t,"paint_size")
   .push_constant(Type::int_t,"mask_mode").push_constant(Type::float3_t,"paint_value").push_constant(Type::int_t,"patch_phases").push_constant(Type::int_t,"color_channel").push_constant(Type::int_t,"configuration").push_constant(Type::float4_t,"offsets");
  info.compute_source_generated=gpu_ribbon_shader();
  return info;
 }
 void prepare(){
  if(shader_)return;trace_authoring_compilation();auto info=program_info();gpu_derived::stabilize_hosted_resource_owners(info);
  shader_=Shader(blender::GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),blender::GPU_shader_free);
  if(!shader_)throw std::runtime_error("Ribbon GPU shader compilation failed");
 }
 public:
 void warm_up(){prepare();}
 std::uint64_t image_allocations()const{return image_allocations_;}
 void render(blender::gpu::Texture *target,const RibbonPathSource &source,int channel,std::uint32_t page,blender::gpu::Texture *mask){
  using namespace blender;performance::Scope timing("gpu_ribbon_path_page");prepare();
  std::erase_if(geometry_,[](const auto &entry){return entry.second.source.expired();});
  std::erase_if(grids_,[](const auto &entry){return entry.second.source.expired();});
  std::erase_if(images_,[](const auto &entry){return entry.second.source.expired();});
  const auto &grid=*source.grid;const auto &g=*grid.geometry;
  std::uint64_t geometry_rows=g.triangles.size()*10+g.fields.size()*4,index_rows=0;
  if(!geometry_.contains(&g)){for(const auto &field:g.fields)geometry_rows+=field.pieces.size()*18;for(const auto &indices:g.candidates)geometry_rows+=indices.size();}
  if(!grids_.contains(&grid))for(const auto &[key,indices]:grid.pages)index_rows+=indices.size();
  const auto geometry_bytes=geometry_.contains(&g)?geometry_.at(&g).bytes:row_bytes(geometry_rows),index_bytes=grids_.contains(&grid)?grids_.at(&grid).bytes:row_bytes(index_rows);
  const auto &bitmap=g.path.ribbon.image;const auto image_bytes=std::uint64_t(bitmap->width)*bitmap->height*16;
  reserve((geometry_.contains(&g)?0:geometry_bytes)+(grids_.contains(&grid)?0:index_bytes)+(images_.contains(bitmap.get())?0:image_bytes),&g,&grid,bitmap.get());
  auto &image=images_[bitmap.get()];image.used=++clock_;
  if(!image.image){image.image=Texture(GPU_texture_create_2d("BTS shared ribbon image",bitmap->width,bitmap->height,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,bitmap->pixels.front().data()),GPU_texture_free);if(!image.image)throw std::bad_alloc();image.source=bitmap;image.bytes=image_bytes;++image_allocations_;}
  auto &geometry=geometry_[&g];geometry.used=++clock_;
  if(!geometry.image){
   std::vector<Row> rows(g.triangles.size()*10);std::vector<int> fields;std::vector<std::vector<int>> pieces;
   const auto write=[&](int at,const std::array<double,4> &values){for(int c=0;c<4;++c){rows[at][c]=float(values[c]);rows[at+1][c]=float(values[c]-double(rows[at][c]));}};
   const auto append=[&](const std::array<double,4> &values){const int at=int(rows.size());rows.resize(rows.size()+2);write(at,values);};
   for(const auto &field:g.fields){fields.push_back(int(rows.size()));const auto &l=field.layout;
    append({l.period,l.width,l.start,l.total});append({double(l.count),double(l.preserve),l.natural,l.period>0?(l.total-l.start)/l.period:0});
    auto &indices=pieces.emplace_back();for(const auto &p:field.pieces){indices.push_back(int(rows.size()));
     append({p.a[0],p.a[1],p.a[2],p.length});append({p.b[0],p.b[1],p.b[2],p.arc});
     append({p.dir[0],p.dir[1],p.dir[2],p.width_a});append({p.side[0],p.side[1],p.side[2],p.width_b});
     append({p.normal[0],p.normal[1],p.normal[2],p.flow_a});append({p.start_plane[0],p.start_plane[1],p.start_plane[2],p.flow_b});
     append({p.end_plane[0],p.end_plane[1],p.end_plane[2],double(p.first)});
     append({p.start_side[0],p.start_side[1],p.start_side[2],double(p.last)});append({p.end_side[0],p.end_side[1],p.end_side[2],p.arc+p.length});
    }
   }
   for(std::size_t i=0;i<g.triangles.size();++i){const auto &t=g.triangles[i];const int at=int(i*10);
    for(int k=0;k<3;++k){rows[at+k]={float(t.uv[k][0]),float(t.uv[k][1]),float(t.uv[k][0]-double(float(t.uv[k][0]))),float(t.uv[k][1]-double(float(t.uv[k][1])))};write(at+3+k*2,{t.position[k][0],t.position[k][1],t.position[k][2],0});}
    rows[at+9]={float(rows.size()),float(g.candidates[i].size()),0,0};
    for(const auto &candidate:g.candidates[i])rows.push_back({float(fields[candidate[0]]),float(pieces[candidate[0]][candidate[1]]),float(candidate[1]),float(g.fields[candidate[0]].pieces.size())});
   }
   if(rows.size()>16777216)throw std::length_error("Ribbon GPU metadata exceeds exact index addressing");
   geometry.image=upload(std::move(rows));geometry.source=grid.geometry;geometry.bytes=geometry_bytes;
  }
  auto &indices=grids_[&grid];indices.used=++clock_;
  if(!indices.image){std::vector<Row> rows;for(const auto &[key,triangles]:grid.pages){indices.ranges[key]={int(rows.size()),int(triangles.size())};for(int triangle:triangles)rows.push_back({float(triangle),0,0,0});}indices.image=upload(std::move(rows));indices.source=source.grid;indices.bytes=index_bytes;}
  const int origin[2]={int(page%65536)*64,int(page/65536)*64},tile[2]={(grid.tile-1001)%10,(grid.tile-1001)/10};
  const auto value=source.value(channel);GPU_shader_bind(shader_.get());
  const auto &ribbon=g.path.ribbon;
  const int configuration=int(ribbon.corner)|(int(ribbon.alpha)<<2)|(int(g.path.closed)<<4)|(ribbon.orientation<<5)|(int(ribbon.stretch)<<7)|(int(ribbon.ratio)<<8)|(int(ribbon.remove_clipped)<<9)|(int(ribbon.mirror)<<10)|(int(ribbon.flip)<<11);
  const float offsets[4]={float(ribbon.start_offset),float(ribbon.start_offset-double(float(ribbon.start_offset))),float(ribbon.end_offset),float(ribbon.end_offset-double(float(ribbon.end_offset)))};
  GPU_shader_uniform_1i(shader_.get(),"patch_phases",g.path.closed?4:1);GPU_shader_uniform_1i(shader_.get(),"configuration",configuration);GPU_shader_uniform_4fv(shader_.get(),"offsets",offsets);GPU_shader_uniform_1i(shader_.get(),"color_channel",source.mode==0&&channel==0);
  GPU_shader_uniform_2iv(shader_.get(),"page_origin",origin);GPU_shader_uniform_2iv(shader_.get(),"tile_offset",tile);
  GPU_shader_uniform_2iv(shader_.get(),"triangle_range",indices.ranges.at(page).data());GPU_shader_uniform_1i(shader_.get(),"paint_size",grid.size);
  GPU_shader_uniform_1i(shader_.get(),"mask_mode",g.mask?(g.mask->inverted?2:1):0);GPU_shader_uniform_3fv(shader_.get(),"paint_value",value.data());
  GPU_texture_bind(geometry.image.get(),0);GPU_texture_bind(indices.image.get(),1);GPU_texture_bind(mask,2);GPU_texture_bind(image.image.get(),3);GPU_texture_image_bind(target,0);
  GPU_compute_dispatch(shader_.get(),4,4,1);trace_authoring_dispatch(shader_.get());GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_texture_image_unbind(target);GPU_shader_unbind();
 }
};
}
