// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FilledPathRaster.hh"
#include "GpuBrushRaster.hh"
#include "TextureMemory.hh"
#include "GpuPathMath.hh"
namespace bts {
// Rasterize only requested immutable authoring pages. Shared charts/geometry
// stay on the GPU across material channels and path-history consumers.
class GpuFilledPath {
  friend class GpuPaintPages;
  using Texture = std::unique_ptr<blender::gpu::Texture, decltype(&blender::GPU_texture_free)>;
  using Shader = std::unique_ptr<blender::gpu::Shader, decltype(&blender::GPU_shader_free)>;
  using Row = std::array<float, 4>;
  struct GeometryEntry {
    std::uint64_t bytes = 0, used = 0;
    std::weak_ptr<const FilledPathGeometry> source;
    Texture image{nullptr, blender::GPU_texture_free};
  };
  struct GridEntry {
    std::uint64_t bytes = 0, used = 0;
    std::weak_ptr<const FilledPathGrid> source;
    Texture image{nullptr, blender::GPU_texture_free};
    std::map<std::uint32_t, std::array<int, 2>> ranges;
  };
  std::map<const FilledPathGeometry *, GeometryEntry> geometry_;
  std::map<const FilledPathGrid *, GridEntry> grids_;
  std::uint64_t clock_ = 0;
  static std::uint64_t row_bytes(std::uint64_t rows)
  {
    const auto width = std::min<std::uint64_t>(1024, std::max<std::uint64_t>(1, rows));
    return width * std::max<std::uint64_t>(1, (rows + width - 1) / width) * 16;
  }
  std::uint64_t owned() const
  {
    std::uint64_t bytes = 0;
    for (const auto &[key, entry] : geometry_)
      bytes += entry.bytes;
    for (const auto &[key, entry] : grids_)
      bytes += entry.bytes;
    return bytes;
  }
  void reserve(std::uint64_t requested,
               const FilledPathGeometry *geometry,
               const FilledPathGrid *grid)
  {
    const auto free = texture_free_vram.load(),
               budget = resource_sum(owned(),
                                     (texture_vram_query_valid.load() || free != 0) ?
                                         free :
                                         texture_available_ram_bytes());
    while (owned() + requested > budget) {
      auto g = geometry_.end();
      for (auto i = geometry_.begin(); i != geometry_.end(); ++i)
        if (i->first != geometry && (g == geometry_.end() || i->second.used < g->second.used))
          g = i;
      auto p = grids_.end();
      for (auto i = grids_.begin(); i != grids_.end(); ++i)
        if (i->first != grid && (p == grids_.end() || i->second.used < p->second.used))
          p = i;
      if (g == geometry_.end() && p == grids_.end())
        break;
      if (p == grids_.end() || (g != geometry_.end() && g->second.used < p->second.used))
        geometry_.erase(g);
      else
        grids_.erase(p);
    }
  }
  Shader shader_{nullptr, blender::GPU_shader_free};
  static Texture upload(std::vector<Row> rows)
  {
    using namespace blender;
    const int width = std::min<int>(1024, std::max<std::size_t>(1, rows.size())),
              height = int((rows.size() + width - 1) / width);
    if (height > GPU_max_texture_size())
      throw std::length_error("Filled-path GPU metadata exceeds device texture dimensions");
    rows.resize(std::size_t(width) * std::max(1, height));
    Texture result(GPU_texture_create_2d("BTS shared filled-path metadata",
                                         width,
                                         std::max(1, height),
                                         1,
                                         gpu::TextureFormat::SFLOAT_32_32_32_32,
                                         GPU_TEXTURE_USAGE_GENERAL,
                                         rows.front().data()),
                   GPU_texture_free);
    if (!result)
      throw std::bad_alloc();
    return result;
  }
  static blender::gpu::shader::ShaderCreateInfo program_info()
  {
    using namespace blender;
    using namespace gpu::shader;
    ShaderCreateInfo info("pyGPU_Shader");
    info.local_group_size(16, 16)
        .sampler(0, ImageType::Float2D, "geometry_img")
        .sampler(1, ImageType::Float2D, "indices_img")
        .sampler(2, ImageType::Float2D, "coverage_img")
        .image(0,
               gpu::TextureFormat::SFLOAT_32_32_32_32,
               Qualifier::write,
               ImageReadWriteType::image2D,
               "result_img")
        .push_constant(Type::int2_t, "page_origin")
        .push_constant(Type::int2_t, "triangle_range")
        .push_constant(Type::int2_t, "tile_offset")
        .push_constant(Type::int_t, "paint_size")
        .push_constant(Type::int_t, "mask_mode")
        .push_constant(Type::float3_t, "paint_value");
    info.compute_source_generated = std::string(R"GLSL(
vec4 geometry(int i){ivec2 s=textureSize(geometry_img,0);return texelFetch(geometry_img,ivec2(i%s.x,i/s.x),0);}
int triangle_index(int i){ivec2 s=textureSize(indices_img,0);return int(texelFetch(indices_img,ivec2(i%s.x,i/s.x),0).x);}
float coverage(ivec2 p){
 if(mask_mode==0)return 1;
 ivec2 size=textureSize(coverage_img,0),output_size=ivec2(paint_size);
 bvec2 area=bvec2(size.x>=output_size.x&&size.x%output_size.x==0,size.y>=output_size.y&&size.y%output_size.y==0);
 vec2 location=(vec2(p)+.5)*vec2(size)/vec2(output_size)-.5,fraction=fract(location);ivec2 first=ivec2(floor(location)),count=ivec2(2);
 if(area.x){count.x=size.x/output_size.x;first.x=p.x*count.x;}if(area.y){count.y=size.y/output_size.y;first.y=p.y*count.y;}
 precise vec4 sum=vec4(0),correction=vec4(0);
 for(int y=0;y<count.y;y++)for(int x=0;x<count.x;x++){float weight=(area.x?1.0/count.x:(x==0?1-fraction.x:fraction.x))*(area.y?1.0/count.y:(y==0?1-fraction.y:fraction.y));precise vec4 term=texelFetch(coverage_img,clamp(first+ivec2(x,y),ivec2(0),size-1),0)*weight-correction;precise vec4 next=sum+term;correction=(next-sum)-term;sum=next;}
 float allowed=clamp(sum.r+1-sum.a,0.0,1.0);return mask_mode==2?1-allowed:allowed;
}

)GLSL") + gpu_path_math + R"GLSL(
bool contains(int shape,vec2 px,vec2 py,vec2 pz){
 vec4 origin=geometry(shape),normal=geometry(shape+1),u=geometry(shape+2),v=geometry(shape+3),bounds=geometry(shape+4);
 vec4 origin_lo=geometry(shape+5),normal_lo=geometry(shape+6),u_lo=geometry(shape+7),v_lo=geometry(shape+8),bounds_lo=geometry(shape+9);
 vec2 dx=ds_sub(px,vec2(origin.x,origin_lo.x)),dy=ds_sub(py,vec2(origin.y,origin_lo.y)),dz=ds_sub(pz,vec2(origin.z,origin_lo.z));
 vec2 qx=ds_dot(dx,dy,dz,u.xyz,u_lo.xyz),qy=ds_dot(dx,dy,dz,v.xyz,v_lo.xyz),depth=ds_dot(dx,dy,dz,normal.xyz,normal_lo.xyz);
 if(ds_less(qx,vec2(bounds.x,bounds_lo.x))||ds_less(qy,vec2(bounds.y,bounds_lo.y))||ds_less(vec2(bounds.z,bounds_lo.z),qx)||ds_less(vec2(bounds.w,bounds_lo.w),qy)||ds_less(depth,vec2(u.w,u_lo.w))||ds_less(vec2(v.w,v_lo.w),depth))return false;
 bool inside=false;int start=int(origin.w),count=int(normal.w);vec4 b=geometry(start+count-1);
 for(int i=0;i<count;++i){vec4 a=geometry(start+i);vec2 ax=a.xz,ay=a.yw,bx=b.xz,by=b.yw;
  if(ds_less(qy,ay)!=ds_less(qy,by)){
   vec2 cross_value=ds_sub(ds_mul(ds_sub(bx,ax),ds_sub(qy,ay)),ds_mul(ds_sub(qx,ax),ds_sub(by,ay)));
   if(ds_less(ay,by)?ds_less(vec2(0),cross_value):ds_less(cross_value,vec2(0)))inside=!inside;
  }b=a;
 }return inside;
}

void main(){
 ivec2 p=ivec2(gl_GlobalInvocationID.xy),at=p+page_origin;vec4 result=vec4(0);
 if(all(lessThan(at,ivec2(paint_size)))){
 vec2 uvx=ds_add(ds_div(vec2(float(at.x)+.5,0),vec2(float(paint_size),0)),vec2(float(tile_offset.x),0));
 vec2 uvy=ds_add(ds_div(vec2(float(at.y)+.5,0),vec2(float(paint_size),0)),vec2(float(tile_offset.y),0));bool covered=false;
 for(int i=0;i<triangle_range.y&&!covered;++i){int tri=triangle_index(triangle_range.x+i)*5;
  vec4 ab=geometry(tri),c=geometry(tri+1);precise vec2 du=ab.zw-ab.xy,dv=c.xy-ab.xy;
  vec2 dx=ds_sub(uvx,vec2(ab.x,0)),dy=ds_sub(uvy,vec2(ab.y,0));
  vec2 determinant=ds_sub(ds_mul(vec2(du.x,0),vec2(dv.y,0)),ds_mul(vec2(du.y,0),vec2(dv.x,0)));
  vec2 w1=ds_div(ds_sub(ds_mul(dx,vec2(dv.y,0)),ds_mul(dy,vec2(dv.x,0))),determinant);
  vec2 w2=ds_div(ds_sub(ds_mul(vec2(du.x,0),dy),ds_mul(vec2(du.y,0),dx)),determinant);
  if(ds_less(w1,vec2(-1e-9,0))||ds_less(w2,vec2(-1e-9,0))||ds_less(vec2(1,1e-9),ds_add(w1,w2)))continue;
  vec2 w0=ds_sub(ds_sub(vec2(1,0),w1),w2);vec3 p0=geometry(tri+2).xyz,p1=geometry(tri+3).xyz,p2=geometry(tri+4).xyz;
  vec2 px=ds_add(ds_add(ds_mul(vec2(p0.x,0),w0),ds_mul(vec2(p1.x,0),w1)),ds_mul(vec2(p2.x,0),w2));
  vec2 py=ds_add(ds_add(ds_mul(vec2(p0.y,0),w0),ds_mul(vec2(p1.y,0),w1)),ds_mul(vec2(p2.y,0),w2));
  vec2 pz=ds_add(ds_add(ds_mul(vec2(p0.z,0),w0),ds_mul(vec2(p1.z,0),w1)),ds_mul(vec2(p2.z,0),w2));
  for(int k=0;k<int(c.w)&&!covered;++k)covered=contains(int(geometry(int(c.z)+k).x),px,py,pz);
 }
 if(covered){float alpha=coverage(at);result=vec4(paint_value*alpha,alpha);}
 }imageStore(result_img,p,result);
}
)GLSL";
    return info;
  }
  void prepare()
  {
    if (shader_)
      return;
    trace_authoring_compilation();
    auto info = program_info();
    gpu_derived::stabilize_hosted_resource_owners(info);
    shader_ = Shader(blender::GPU_shader_create_from_info_python(
                         reinterpret_cast<GPUShaderCreateInfo *>(&info)),
                     blender::GPU_shader_free);
    if (!shader_)
      throw std::runtime_error("Filled-path GPU shader compilation failed");
  }

 public:
  void evict()
  {
    geometry_.clear();
    grids_.clear();
  }
  void warm_up()
  {
    prepare();
  }
  void render(blender::gpu::Texture *target,
              const FilledPathSource &source,
              int channel,
              std::uint32_t page,
              blender::gpu::Texture *mask)
  {
    using namespace blender;
    performance::Scope timing("gpu_filled_path_page");
    prepare();
    std::erase_if(geometry_, [](const auto &entry) { return entry.second.source.expired(); });
    std::erase_if(grids_, [](const auto &entry) { return entry.second.source.expired(); });
    const auto &grid = *source.grid;
    const auto &g = *grid.geometry;
    std::uint64_t geometry_rows = g.scene.uv_triangles.size() * 5 + g.shapes.size() * 10,
                  index_rows = 0;
    if (!geometry_.contains(&g)) {
      for (const auto &shape : g.shapes)
        geometry_rows += shape.boundary.size();
      for (const auto &indices : g.candidates)
        geometry_rows += indices.size();
    }
    if (!grids_.contains(&grid))
      for (const auto &[key, indices] : grid.pages)
        index_rows += indices.size();
    const auto geometry_bytes = geometry_.contains(&g) ? geometry_.at(&g).bytes :
                                                         row_bytes(geometry_rows),
               index_bytes = grids_.contains(&grid) ? grids_.at(&grid).bytes :
                                                      row_bytes(index_rows);
    reserve((geometry_.contains(&g) ? 0 : geometry_bytes) +
                (grids_.contains(&grid) ? 0 : index_bytes),
            &g,
            &grid);
    auto &geometry = geometry_[&g];
    geometry.used = ++clock_;
    if (!geometry.image) {
      const auto &triangles = g.scene.uv_triangles;
      std::vector<Row> rows(triangles.size() * 5 + g.shapes.size() * 10);
      std::vector<int> shapes;
      for (std::size_t i = 0; i < g.shapes.size(); ++i) {
        const auto &s = g.shapes[i];
        const int at = int(triangles.size() * 5 + i * 10);
        shapes.push_back(at);
        rows[at] = {
            float(s.origin[0]), float(s.origin[1]), float(s.origin[2]), float(rows.size())};
        rows[at + 1] = {
            float(s.normal[0]), float(s.normal[1]), float(s.normal[2]), float(s.boundary.size())};
        rows[at + 2] = {
            float(s.u[0]), float(s.u[1]), float(s.u[2]), float(s.depth_low - s.depth_margin)};
        rows[at + 3] = {
            float(s.v[0]), float(s.v[1]), float(s.v[2]), float(s.depth_high + s.depth_margin)};
        rows[at + 4] = {float(s.low[0]), float(s.low[1]), float(s.high[0]), float(s.high[1])};
        const auto low = [](double value) { return float(value - double(float(value))); };
        rows[at + 5] = {low(s.origin[0]), low(s.origin[1]), low(s.origin[2]), 0};
        rows[at + 6] = {low(s.normal[0]), low(s.normal[1]), low(s.normal[2]), 0};
        rows[at + 7] = {low(s.u[0]), low(s.u[1]), low(s.u[2]), low(s.depth_low - s.depth_margin)};
        rows[at + 8] = {low(s.v[0]), low(s.v[1]), low(s.v[2]), low(s.depth_high + s.depth_margin)};
        rows[at + 9] = {low(s.low[0]), low(s.low[1]), low(s.high[0]), low(s.high[1])};
        for (const auto &point : s.boundary)
          rows.push_back({float(point[0]), float(point[1]), low(point[0]), low(point[1])});
      }
      for (std::size_t i = 0; i < triangles.size(); ++i) {
        const auto &t = triangles[i];
        const int at = int(i * 5);
        rows[at] = {t.corners[0][0], t.corners[0][1], t.corners[1][0], t.corners[1][1]};
        rows[at + 1] = {
            t.corners[2][0], t.corners[2][1], float(rows.size()), float(g.candidates[i].size())};
        for (int k = 0; k < 3; ++k)
          rows[at + 2 + k] = {t.positions[k][0], t.positions[k][1], t.positions[k][2], 0};
        for (int shape : g.candidates[i])
          rows.push_back({float(shapes[shape]), 0, 0, 0});
      }
      if (rows.size() > 16777216)
        throw std::length_error("Filled-path GPU metadata exceeds exact index addressing");
      geometry.image = upload(std::move(rows));
      geometry.source = grid.geometry;
      geometry.bytes = geometry_bytes;
    }
    auto &indices = grids_[&grid];
    indices.used = ++clock_;
    if (!indices.image) {
      std::vector<Row> rows;
      for (const auto &[key, triangles] : grid.pages) {
        indices.ranges[key] = {int(rows.size()), int(triangles.size())};
        for (int triangle : triangles)
          rows.push_back({float(triangle), 0, 0, 0});
      }
      indices.image = upload(std::move(rows));
      indices.source = source.grid;
      indices.bytes = index_bytes;
    }
    const int origin[2] = {int(page % 65536) * 64, int(page / 65536) * 64},
              tile[2] = {(grid.tile - 1001) % 10, (grid.tile - 1001) / 10};
    const auto value = source.value(channel);
    GPU_shader_bind(shader_.get());
    GPU_shader_uniform_2iv(shader_.get(), "page_origin", origin);
    GPU_shader_uniform_2iv(shader_.get(), "tile_offset", tile);
    GPU_shader_uniform_2iv(shader_.get(), "triangle_range", indices.ranges.at(page).data());
    GPU_shader_uniform_1i(shader_.get(), "paint_size", grid.size);
    GPU_shader_uniform_1i(shader_.get(), "mask_mode", g.mask ? (g.mask->inverted ? 2 : 1) : 0);
    GPU_shader_uniform_3fv(shader_.get(), "paint_value", value.data());
    GPU_texture_bind(geometry.image.get(), 0);
    GPU_texture_bind(indices.image.get(), 1);
    GPU_texture_bind(mask, 2);
    GPU_texture_image_bind(target, 0);
    GPU_compute_dispatch(shader_.get(), 4, 4, 1);
    trace_authoring_dispatch(shader_.get());
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    GPU_texture_image_unbind(target);
    GPU_shader_unbind();
  }
};
}  // namespace bts
