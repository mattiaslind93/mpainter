#pragma once
#include "SurfaceCurvature.hh"
#include "GpuBakeRays.hh"
namespace bts {
// Keep welding, polygon boundaries, normal weighting and connected components
// shared with the CPU oracle. Only field evaluation moves to the GPU.
inline void gpu_calculate_pointiness(SurfaceCurvature &surface,float radius,GpuBakeRayStats *statistics=nullptr){
 using namespace blender;
 if(!GPU_context_active_get())throw std::logic_error("Pointiness requires an active GPU context");
 if(!std::isfinite(radius)||radius<0)throw std::invalid_argument("Pointiness smoothing radius must be nonnegative and finite");
 if(statistics)*statistics={};
 const auto count=surface.vertices.size();if(!count)return;
 if(count>std::size_t(std::numeric_limits<int>::max()))throw std::length_error("Pointiness vertex indices exceed shader representation");
 if(GPU_max_compute_shader_storage_blocks()<6)throw std::length_error("Pointiness requires six compute SSBO bindings");
 struct alignas(16) Vertex {std::array<float,4> position,normal;std::array<std::uint32_t,4> neighbors;};
 std::vector<Vertex> vertices;std::vector<std::uint32_t> neighbors;
 vertices.reserve(count);
 for(const auto &v:surface.vertices){
  if(neighbors.size()+v.neighbors.size()>std::numeric_limits<std::uint32_t>::max())throw std::length_error("Pointiness neighbor buffer too large");
  vertices.push_back({{v.p[0],v.p[1],v.p[2],v.area},{v.n[0],v.n[1],v.n[2],0},{std::uint32_t(neighbors.size()),std::uint32_t(v.neighbors.size()),std::uint32_t(v.component),0}});
  neighbors.insert(neighbors.end(),v.neighbors.begin(),v.neighbors.end());
 }
 if(neighbors.empty())neighbors.push_back(0);
 std::vector<std::array<int,4>> tree;
 if(radius>0){
  surface.tree.clear();surface.tree.reserve(count);std::vector<int> order(count);std::iota(order.begin(),order.end(),0);surface.build(order,0,int(count),0);
  for(const auto &n:surface.tree)tree.push_back({n.vertex,n.left,n.right,n.axis});
 }else tree.push_back({0,-1,-1,0});
 for(auto bytes:{vertices.size()*sizeof(Vertex),neighbors.size()*sizeof(std::uint32_t),tree.size()*sizeof(tree[0]),count*sizeof(float)})
  if(bytes>GPU_max_storage_buffer_size())throw std::length_error("Pointiness buffer exceeds driver limit");
 gpu::shader::ShaderCreateInfo info("pyGPU_Shader");
 info.typedef_source_generated="struct PointVertex {vec4 position;vec4 normal;uvec4 neighbors;};\n";
 info.local_group_size(64,1)
  .storage_buf(0,gpu::shader::Qualifier::read,"PointVertex","vertices[]")
  .storage_buf(1,gpu::shader::Qualifier::read,"uint","neighbors[]")
  .storage_buf(2,gpu::shader::Qualifier::read_write,"float","raw[]")
  .storage_buf(3,gpu::shader::Qualifier::read_write,"float","blurred[]")
  .storage_buf(4,gpu::shader::Qualifier::write,"float","output_values[]")
  .storage_buf(5,gpu::shader::Qualifier::read,"ivec4","tree[]")
  .push_constant(gpu::shader::Type::int_t,"vertex_count")
  .push_constant(gpu::shader::Type::int_t,"base_vertex")
  .push_constant(gpu::shader::Type::int_t,"pass")
  .push_constant(gpu::shader::Type::float_t,"radius");
 info.compute_source_generated=R"GLSL(
void main(){
 uint i=uint(base_vertex)+gl_GlobalInvocationID.x;if(i>=uint(vertex_count))return;
 PointVertex v=vertices[i];uint begin=v.neighbors.x,count=v.neighbors.y;
 if(pass==0){
  precise vec3 direction=vec3(0.0);
  for(uint k=0u;k<count;++k){precise vec3 edge=vertices[neighbors[begin+k]].position.xyz-v.position.xyz;precise float len=sqrt(dot(edge,edge));if(len>0.0)direction+=edge/len;}
  if(count==0u){raw[i]=0.5;return;}
  precise float cosine=clamp(dot(v.normal.xyz,direction)/float(count),-1.0,1.0);
  // The atan2 identity evaluates acos accurately near neutral Pointiness.
  precise float sine=sqrt(max(0.0,(1.0-cosine)*(1.0+cosine)));
  raw[i]=atan(sine,cosine)/3.14159265358979323846;
 }else if(pass==1){
  float sum=raw[i];for(uint k=0u;k<count;++k)sum+=raw[neighbors[begin+k]];
  blurred[i]=sum/float(count+1u);output_values[i]=blurred[i];
 }else{
  // The balanced int-indexed tree is at most 32 levels. Right then left
  // reproduces the CPU's preorder and therefore its accumulation order.
  int stack[64];int size=1;stack[0]=0;double sum=0.0,total=0.0;
  while(size>0){
   ivec4 node=tree[stack[--size]];PointVertex other=vertices[node.x];
   vec3 delta=other.position.xyz-v.position.xyz;float d2=dot(delta,delta);
   if(d2<radius*radius&&other.neighbors.z==v.neighbors.z){
    double falloff=double(1.0-d2/(radius*radius));double weight=falloff*falloff*double(max(other.position.w,1e-20));
    sum+=double(blurred[node.x])*weight;total+=weight;
   }
   if(node.z>=0&&v.position[node.w]+radius>=other.position[node.w])stack[size++]=node.z;
   if(node.y>=0&&v.position[node.w]-radius<=other.position[node.w])stack[size++]=node.y;
  }
  output_values[i]=total>0.0?float(sum/total):blurred[i];
 }
}
)GLSL";
 using Shader=std::unique_ptr<gpu::Shader,decltype(&GPU_shader_free)>;
 using Buffer=std::unique_ptr<gpu::StorageBuf,decltype(&GPU_storagebuf_free)>;
 Shader shader(GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free);
 if(!shader)throw std::runtime_error("Pointiness compute shader failed");
 const auto buffer=[](std::size_t bytes,const void *data,const char *name){return Buffer(GPU_storagebuf_create_ex(bytes,data,GPU_USAGE_STATIC,name),GPU_storagebuf_free);};
 auto vb=buffer(vertices.size()*sizeof(Vertex),vertices.data(),"Pointiness vertices");
 auto nb=buffer(neighbors.size()*sizeof(std::uint32_t),neighbors.data(),"Pointiness neighbors");
 auto raw=buffer(count*sizeof(float),nullptr,"Pointiness angles"),blur=buffer(count*sizeof(float),nullptr,"Pointiness blur"),out=buffer(count*sizeof(float),nullptr,"Pointiness output");
 auto tb=buffer(tree.size()*sizeof(tree[0]),tree.data(),"Pointiness radius tree");
 if(!vb||!nb||!raw||!blur||!out||!tb)throw std::bad_alloc();
 const std::uint64_t batch=std::min<std::uint64_t>(65536,std::uint64_t(GPU_max_work_group_count(0))*64);
 if(!batch)throw std::runtime_error("Pointiness compute dispatch unavailable");
 for(int pass=0;pass<(radius>0?3:2);++pass){
  GPU_shader_bind(shader.get());int binding=0;for(auto *b:{vb.get(),nb.get(),raw.get(),blur.get(),out.get(),tb.get()})GPU_storagebuf_bind(b,binding++);
  GPU_shader_uniform_1i(shader.get(),"vertex_count",int(count));GPU_shader_uniform_1i(shader.get(),"pass",pass);GPU_shader_uniform_1f(shader.get(),"radius",radius);
  for(std::size_t base=0;base<count;base+=batch){
   GPU_shader_uniform_1i(shader.get(),"base_vertex",int(base));GPU_compute_dispatch(shader.get(),unsigned((std::min<std::uint64_t>(batch,count-base)+63)/64),1,1);
   if(statistics){++statistics->batches;++statistics->dispatches;}
  }
  GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);
  for(auto *b:{vb.get(),nb.get(),raw.get(),blur.get(),out.get(),tb.get()})GPU_storagebuf_unbind(b);GPU_shader_unbind();
 }
 GPU_storagebuf_sync_to_host(out.get());
 GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);if(!fence)throw std::runtime_error("Pointiness fence failed");
 const auto status=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,10'000'000'000ULL);glDeleteSync(fence);
 if(status!=GL_ALREADY_SIGNALED&&status!=GL_CONDITION_SATISFIED)throw std::runtime_error("Pointiness fence timed out");
 std::vector<float> values(count);GPU_storagebuf_read(out.get(),values.data());
 for(float value:values)if(!std::isfinite(value))throw std::runtime_error("Pointiness produced a nonfinite value");
 for(std::size_t i=0;i<count;++i)surface.vertices[i].curvature=values[i];
}
}
