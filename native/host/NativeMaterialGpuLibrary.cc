#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialFragmentContext.hh"
#include "NativeMaterialBakedAoContext.hh"
#include "NativeMaterialFragmentImages.hh"
#include "NativeMaterialMappingCompatibility.hh"
#include "NativeMaterialImageUpload.hh"
#include "NativeMaterialSharedGpuImages.hh"
#include "ResourceAvailability.hh"
#include "NativeMaterialRamp.hh"
#include "NativeMaterialMappingValidation.hh"
#include "NativeMaterialInstance.hh"
#include "FillProjection.hh"
#include "GPU_state.hh"
#include "DNA_node_types.h"
#include "BLI_listbase.h"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_image_hosted.hh"
#include "NativeMaterialImageResource.hh"
#include "DNA_material_types.h"
#include <bit>
#include <cstring>
#include <type_traits>
#include <QJsonDocument>
#include <set>
#include <algorithm>
#include <optional>
namespace bts {
void NativeMaterialGpuLibrary::install(std::string resource,std::string hash,std::shared_ptr<NativeMaterialGraph> graph){
 if(resource.empty()||hash.empty()||!graph)throw std::invalid_argument("Invalid native material source identity");

 require_native_source_policy(graph->description);
 const auto canonical=native_world_normal_surface_identity(native_vector_math_mapping_layout_key(graph->resource_content_hash.empty()?hash:graph->resource_content_hash,graph->description),graph->world_normal_surface_contract);const auto alias=native_world_normal_surface_identity(native_vector_math_mapping_layout_key(hash,graph->description),graph->world_normal_surface_contract);auto shared=content_[canonical].lock();if(!shared){shared=std::make_shared<Entry>();shared->graph=std::move(graph);shared->image_requests=native_material_image_upload_requests(*shared->graph);shared->content_hash=canonical;shared->cache_identity=resource;for(const auto &channel:shared->graph->channels)if(channel.tree)shared->dependencies[channel.channel]=native_material_dependencies(*channel.tree,shared->graph->description.parameters.size());
  if(shared->graph->world_normal_tree&&shared->graph->world_normal_error.isEmpty())try{shared->world_dependencies=native_material_dependencies(*shared->graph->world_normal_tree,shared->graph->parameters.size());shared->world_layout=native_world_normal_source_request({shared->graph,shared->graph->world_normal_tree});auto images=NativeFragmentImageContext::capture({shared->graph,shared->graph->world_normal_tree});shared->world_image_required=!images.bindings().empty();shared->world_image_signature=images.signature();}catch(const std::exception &error){shared->world_error=QString::fromUtf8(error.what());}
  shared->revision=++clock_;content_[canonical]=shared;}content_[alias]=shared;
 resources_.insert_or_assign(std::move(resource),std::move(shared));std::erase_if(content_,[](const auto &entry){return entry.second.expired();});
}
void NativeMaterialGpuLibrary::alias(const std::string &source,const std::string &destination){if(destination.empty())throw std::invalid_argument("Invalid native material alias");const auto found=resources_.find(source);if(found==resources_.end())throw std::invalid_argument("Native material alias source is missing");resources_.insert_or_assign(destination,found->second);}
void NativeMaterialGpuLibrary::retain_alias_from(const NativeMaterialGpuLibrary &other,const std::string &key){const auto found=other.resources_.find(key);if(found==other.resources_.end())throw std::logic_error("Frozen native source alias missing");resources_[key]=found->second;}
void NativeMaterialGpuLibrary::register_content_alias(const std::string &resource,const std::string &hash){if(hash.empty())throw std::invalid_argument("Missing native content alias");const auto source=resources_.find(resource);if(source==resources_.end())throw std::invalid_argument("Native content alias source is missing");const auto alias=native_world_normal_surface_identity(native_vector_math_mapping_layout_key(hash,source->second->graph->description),source->second->graph->world_normal_surface_contract);const auto known=content_.find(alias);if(known!=content_.end())if(auto shared=known->second.lock();shared&&shared!=source->second)throw std::invalid_argument("Native content alias already identifies another source");content_[alias]=source->second;}
void NativeMaterialGpuLibrary::erase(const std::string &key){resources_.erase(key);std::erase_if(content_,[](const auto &entry){return entry.second.expired();});}
void NativeMaterialGpuLibrary::clear(){resources_.clear();content_.clear();}
bool NativeMaterialGpuLibrary::contains(const std::string &key)const{return resources_.contains(key);}
bool NativeMaterialGpuLibrary::uses_object(const std::string &key)const{for(const auto &[channel,dependencies]:entry(key).dependencies)if(dependencies.object)return true;return false;}
bool NativeMaterialGpuLibrary::uses_ao(const std::string &key)const{for(const auto &[channel,dependencies]:entry(key).dependencies)if(dependencies.ao)return true;return false;}
std::vector<int> NativeMaterialGpuLibrary::view_channels(const std::string &key)const{std::vector<int> result;for(const auto &[channel,dependencies]:entry(key).dependencies)if(dependencies.view)result.push_back(channel);return result;}
std::vector<int> NativeMaterialGpuLibrary::view_channels(const std::string &key,const NativeMaterialDescription &instance)const{std::vector<int> result;for(int channel:effective_channels(key,instance))if(dependencies(key,channel,instance).view)result.push_back(channel);return result;}
const NativeMaterialGpuLibrary::Entry &NativeMaterialGpuLibrary::entry(const std::string &key)const{const auto found=resources_.find(key);if(found==resources_.end())throw std::runtime_error("Native material source is not prepared yet");auto &value=*found->second;
 if(value.cold.cold()){
  value.pixels.restore();value.cold.restore();
  for(const auto &[image,resource]:value.graph->image_resources)if(!BKE_image_assign_hosted_file_buffer(image,resource->buffer()))throw std::runtime_error("Cannot restore native image buffer");
 }
 if(value.height&&value.height->cold.cold()){
  value.height->pixels.restore();value.height->cold.restore();
  for(const auto &[image,resource]:value.height->resource.prepared.source->image_resources)if(!BKE_image_assign_hosted_file_buffer(image,resource->buffer()))throw std::runtime_error("Cannot restore native Height buffer");
 }
 return value;}
void NativeMaterialGpuLibrary::evict_except(const std::set<std::string> &resident){
 std::set<Entry *> keep,visited;for(const auto &key:resident)if(auto it=resources_.find(key);it!=resources_.end())keep.insert(it->second.get());
 for(auto &[key,owner]:resources_){auto &e=*owner;if(keep.contains(&e)||!visited.insert(&e).second||e.cold.cold())continue;
  e.programs.clear();e.world_programs.clear();e.source_upload.reset();e.world_upload.reset();e.height_upload.reset();
  const auto spill_graph=[](NativeMaterialGraph &graph,ShaderTreeCache &cold,ColdImageCache &pixels){
  std::vector<blender::bNodeTree *> trees;for(auto &m:graph.main->materials)if(m.nodetree)trees.push_back(m.nodetree);for(auto &t:graph.main->nodetrees)trees.push_back(&t);
  std::vector<blender::Image *> images;for(auto &image:graph.main->images)images.push_back(&image);
  cold.spill(*graph.main,trees,images);
  for(auto &image:graph.main->images){const bool shared=std::any_of(graph.image_resources.begin(),graph.image_resources.end(),[&](const auto &v){return v.first==&image;})||std::any_of(graph.tiled_image_resources.begin(),graph.tiled_image_resources.end(),[&](const auto &v){return v.first==&image;});if(shared)BKE_image_free_buffers(&image);else pixels.spill(image);}
  for(const auto &[image,resource]:graph.image_resources)resource->spill();
  for(const auto &[image,tiles]:graph.tiled_image_resources)for(const auto &[tile,resource]:tiles->tiles())resource->spill();
  };
  if(e.height){e.height->program.reset();if(e.height->resource.prepared.source!=e.graph)spill_graph(*e.height->resource.prepared.source,e.height->cold,e.height->pixels);}
  spill_graph(*e.graph,e.cold,e.pixels);
  e.source_image_readiness={};e.world_image_readiness={};e.world_prepared=false;
 }
}
std::uint64_t NativeMaterialGpuLibrary::revision(const std::string &key)const{return entry(key).revision;}
const NativeMaterialGraph &NativeMaterialGpuLibrary::source_graph(const std::string &key)const{return *entry(key).graph;}
const NativeMaterialDescription &NativeMaterialGpuLibrary::description(const std::string &key)const{return entry(key).graph->description;}
bool NativeMaterialGpuLibrary::height_ready(const std::string &key)const{return bool(entry(key).height);}
std::vector<int> NativeMaterialGpuLibrary::effective_channels(const std::string &key,const NativeMaterialDescription &instance)const{
 validate_instance(key,instance);auto channels=entry(key).graph->description.channels;
 if(instance.height_policy==NativeHeightPolicy::RawAuthoredFloatV1&&std::find(channels.begin(),channels.end(),4)==channels.end())channels.push_back(4);
 return channels;
}
NativeMaterialGpuLibrary::HeightCandidate::~HeightCandidate()=default;
NativeMaterialGpuLibrary::HeightChange::~HeightChange(){if(changed_&&!committed_)source_->height.swap(previous_);}
std::unique_ptr<NativeMaterialGpuLibrary::HeightCandidate> NativeMaterialGpuLibrary::stage_height(const std::string &key,NativeMaterialHeightResource resource){
 const auto found=resources_.find(key);if(found==resources_.end())throw std::invalid_argument("Raw authored Height source is not prepared");const auto &source=*found->second;
 const auto identity=content_.find(native_world_normal_surface_identity(native_vector_math_mapping_layout_key(resource.source_hash.toStdString(),source.graph->description),source.graph->world_normal_surface_contract));
 if(identity==content_.end()||identity->second.lock()!=found->second||!resource.prepared.source||!resource.prepared.tree)throw std::invalid_argument("Raw authored Height does not belong to this immutable source");
 if(resource.prepared.source->world_normal_surface_contract!=source.graph->world_normal_surface_contract)throw std::invalid_argument("Raw authored Height surface contract differs from its immutable source");
 if(resource.prepared.source->world_normal_source_displacement!=source.graph->world_normal_source_displacement)throw std::invalid_argument("Raw authored Height source displacement provenance changed");
 if(native_material_description_json(resource.prepared.source->description)!=native_material_description_json(source.graph->description))throw std::invalid_argument("Raw authored Height source interface changed");
 auto candidate=std::unique_ptr<HeightCandidate>(new HeightCandidate);candidate->source_=found->second;
 if(!source.height){candidate->value_=std::make_unique<HeightState>(std::move(resource));auto &prepared=candidate->value_->resource.prepared;std::array<blender::bNodeTree *,1> roots{prepared.tree};candidate->upload_=std::make_unique<NativeMaterialImageUpload>(prepared.source,roots,false);}
 return candidate;
}
bool NativeMaterialGpuLibrary::step_height_candidate(const std::string &key,HeightCandidate &candidate,std::size_t packet_bytes){
 const auto found=resources_.find(key);if(found==resources_.end()||found->second!=candidate.source_)throw std::invalid_argument("Raw authored Height source changed during GPU staging");
 if(!candidate.value_)return bool(candidate.source_->height);
 if(candidate.upload_){if(candidate.upload_->step(packet_bytes))candidate.upload_.reset();return false;}
 if(!candidate.value_->program)candidate.value_->program=std::make_unique<NativeMaterialGpuProgram>(*candidate.value_->resource.prepared.tree,candidate.source_->graph->parameters.size(),4,candidate.value_->resource.prepared.source);
 return true;
}
std::unique_ptr<NativeMaterialGpuLibrary::HeightChange> NativeMaterialGpuLibrary::install_height_candidate(const std::string &key,std::unique_ptr<HeightCandidate> candidate){
 const auto found=resources_.find(key);if(!candidate||found==resources_.end()||found->second!=candidate->source_)throw std::invalid_argument("Raw authored Height prepared source changed before publication");
 auto change=std::unique_ptr<HeightChange>(new HeightChange);change->source_=found->second;
 // Allocation and actual source-identity checks precede this scoped swap. A
 // rejected document/queue transaction restores the prior optional cache.
 if(candidate->upload_||(candidate->value_&&!candidate->value_->program))throw std::logic_error("Raw authored Height images/program are not ready");
 if(!change->source_->height){if(!candidate->value_)throw std::logic_error("Raw authored Height candidate lost its ready program");change->changed_=true;change->source_->height.swap(candidate->value_);change->previous_=std::move(candidate->value_);}
 return change;
}
void NativeMaterialGpuLibrary::prepare_height(const std::string &key,NativeMaterialHeightResource resource){
 auto candidate=stage_height(key,std::move(resource));
 // The legacy immediate bridge is procedural-only. Image owners must yield
 // between packets through the UI/Open staging API.
 if(candidate->upload_&&candidate->upload_->progress().total)throw std::logic_error("Image-backed Raw Height requires stepped GPU preparation");
 while(!step_height_candidate(key,*candidate)){}
 auto change=install_height_candidate(key,std::move(candidate));change->commit();
}
void NativeMaterialGpuLibrary::probe_height(const std::string &key,const NativeMaterialDescription &instance,NativeMaterialGpuContext context){
 validate_instance(key,instance);if(instance.height_policy!=NativeHeightPolicy::RawAuthoredFloatV1)throw std::invalid_argument("Native Height admission requires the explicit raw policy");
 const auto &required=dependencies(key,4,instance);
 if(required.object&&(!context.objects||!context.objects->size()))throw std::invalid_argument("Raw authored Height has no candidate object table");
 // A finite diagnostic packet exercises the exact surface owner ABI and the
 // FULL actual pending table allocation. Its output is not an appearance
 // reference or a substitute for the independent real mesh/UDIM proof.
 FillSurfacePoint point;point.valid=true;
 if(context.objects&&context.objects->size()){
  const auto &rows=context.objects->rows();point.match_object=std::bit_cast<std::uint32_t>(rows[0][0]);
  std::array<float,3> center{};for(int c=0;c<3;++c)center[c]=(rows[9][c]+rows[10][c])*.5f;
  for(int c=0;c<3;++c)point.position[c]=rows[1][c]*center[0]+rows[2][c]*center[1]+rows[3][c]*center[2]+rows[4][c];
 }
 static_assert(sizeof(FillSurfacePoint)%sizeof(std::uint32_t)==0);
 static_assert(std::is_trivially_copyable_v<FillSurfacePoint>);
 constexpr auto words=sizeof(FillSurfacePoint)/sizeof(std::uint32_t),texels=(words+3)/4;
 std::array<std::uint32_t,texels*4> packed{};std::memcpy(packed.data(),&point,sizeof(point));
 const auto bytes=resource_sum(sizeof(packed),sizeof(float)*4);
 // Private regression fault point. It runs only for this new isolated policy
 // preparation; ordinary native rendering never consults it.
 if(qEnvironmentVariableIntValue("BTS_NATIVE_HEIGHT_FAIL_PROBE_ALLOCATION")==1)throw std::runtime_error("Injected pending native Height probe allocation failure");
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 Texture surface(blender::GPU_texture_create_2d_array("Pending Height surface ABI probe",texels,1,1,1,blender::gpu::TextureFormat::UINT_32_32_32_32,blender::GPU_TEXTURE_USAGE_SHADER_READ,nullptr),blender::GPU_texture_free);
 Texture output(blender::GPU_texture_create_2d("Pending Height dispatch probe",1,1,1,blender::gpu::TextureFormat::SFLOAT_32_32_32_32,blender::GPU_TEXTURE_USAGE_SHADER_READ|blender::GPU_TEXTURE_USAGE_SHADER_WRITE,nullptr),blender::GPU_texture_free);
 if(!surface||!output)throw std::runtime_error("Pending native Height admission texture allocation failed");
 blender::GPU_texture_update(surface.get(),blender::GPU_DATA_UINT,packed.data());
 context.surface=surface.get();context.surface_size={1,1};context.surface_stride=words;
 context.color_to_aces=default_context(key).color_to_aces;
 render(key,4,instance,output.get(),context,{0,0,1,1});
 blender::GPU_memory_barrier(blender::GPU_BARRIER_TEXTURE_FETCH);
}
NativeMaterialGpuLibrary::Statistics NativeMaterialGpuLibrary::statistics()const{Statistics result;result.resources=resources_.size();std::set<const Entry *> sources;for(const auto &[id,value]:resources_)if(sources.insert(value.get()).second){result.programs+=value->programs.size()+value->world_programs.size()+bool(value->height);result.world_programs+=value->world_programs.size();result.world_dispatches+=value->world_dispatches;result.dispatches+=value->dispatches;result.texels+=value->texels;}result.sources=sources.size();return result;}
NativeMaterialGpuContext NativeMaterialGpuLibrary::default_context(const std::string &key)const{NativeMaterialGpuContext context;context.color_to_aces=entry(key).graph->color_to_aces;return context;}
const NativeMaterialDependencies &NativeMaterialGpuLibrary::dependencies(const std::string &key,int channel)const{const auto &source=entry(key);const auto found=source.dependencies.find(channel);if(found==source.dependencies.end()){for(const auto &candidate:source.graph->channels)if(candidate.channel==channel)throw std::runtime_error(candidate.error.toStdString());throw std::invalid_argument("Native material channel is not supported");}return found->second;}
const NativeMaterialDependencies &NativeMaterialGpuLibrary::dependencies(const std::string &key,int channel,const NativeMaterialDescription &instance)const{
 if(channel==4&&instance.height_policy==NativeHeightPolicy::RawAuthoredFloatV1){validate_instance(key,instance);return entry(key).height->resource.dependencies;}
 return dependencies(key,channel);
}
NativeMaterialDependencies NativeMaterialGpuLibrary::requirements(const std::string &key,const NativeMaterialDescription &instance)const{
 NativeMaterialDependencies result;
 for(int channel:effective_channels(key,instance)){
  const auto &value=dependencies(key,channel,instance);result.object|=value.object;result.surface|=value.surface;result.view|=value.view;result.ao|=value.ao;result.uv|=value.uv;result.facing|=value.facing;
 }
 return result;
}
void NativeMaterialGpuLibrary::validate_instance(const std::string &key,const NativeMaterialDescription &instance)const{
 const auto &source=entry(key);validate_native_material_instance(*source.graph,instance);
 if(instance.height_policy==NativeHeightPolicy::RawAuthoredFloatV1&&!source.height)throw std::invalid_argument("Raw authored Height is not prepared for this immutable source");
}
void NativeMaterialGpuLibrary::validate_mapping_edit(const std::string &key,const NativeMaterialDescription *before,const NativeMaterialDescription &after)const{
 validate_instance(key,after);const auto &source=entry(key);
 for(std::size_t i=0;i<after.parameters.size();++i){const auto &p=after.parameters[i];if(p.mapping_transform.identity())continue;
  if(before&&i<before->parameters.size()&&before->parameters[i].identifier==p.identifier&&before->parameters[i].mapping_transform==p.mapping_transform)continue;
  validate_native_mapping_override(*source.graph,p,true);
  const bool used=std::any_of(source.dependencies.begin(),source.dependencies.end(),[&](const auto &entry){const auto &indices=entry.second.parameters;return std::find(indices.begin(),indices.end(),i)!=indices.end();});
  const bool world_used=after.height_policy==NativeHeightPolicy::None&&source.graph->world_normal_tree&&source.world_error.isEmpty()&&source.graph->world_normal_error.isEmpty()&&std::find(source.world_dependencies.parameters.begin(),source.world_dependencies.parameters.end(),i)!=source.world_dependencies.parameters.end();
  if(!used&&!world_used&&!(after.height_policy==NativeHeightPolicy::RawAuthoredFloatV1&&source.height&&std::find(source.height->resource.dependencies.parameters.begin(),source.height->resource.dependencies.parameters.end(),i)!=source.height->resource.dependencies.parameters.end()))throw std::invalid_argument("Extra Mapping transform unavailable: input is unused by supported native channels");
 }
}
std::string NativeMaterialGpuLibrary::signature(const std::string &key,int channel,const NativeMaterialDescription &instance)const{
 validate_instance(key,instance);const auto &source=entry(key);
 std::string value;if(channel==4&&instance.height_policy!=NativeHeightPolicy::None){static constexpr char domain[]="\0BTS:authored-height-raw-v1\0";value.append(domain,sizeof(domain)-1);}value+=source.cache_identity;value.append(reinterpret_cast<const char *>(&source.revision),sizeof(source.revision));for(auto index:dependencies(key,channel,instance).parameters){const auto &parameter=instance.parameters[index];for(int c=0;c<parameter.components();++c){const float encoded=float(parameter.value[c]);value.append(reinterpret_cast<const char *>(&encoded),sizeof(encoded));}if(parameter.mapping_transform_supported()){for(const auto &row:parameter.mapping_transform.rows())value.append(reinterpret_cast<const char *>(row.data()),sizeof(row));}if(!parameter.ramp.isEmpty()){const auto ramp=QJsonDocument(parameter.ramp).toJson(QJsonDocument::Compact);value.append(ramp.constData(),ramp.size());}}return value;
}
bool NativeMaterialGpuLibrary::world_normal_uses_object(const std::string &key)const{return entry(key).world_dependencies.object;}
bool NativeMaterialGpuLibrary::world_normal_perturbed(const std::string &key)const{return entry(key).graph->world_normal_perturbed&&entry(key).graph->world_normal_error.isEmpty()&&entry(key).world_error.isEmpty();}
NativeWorldNormalContextState NativeMaterialGpuLibrary::world_normal_context_state(const std::string &key)const{
 const auto &s=entry(key);if(!s.graph->world_normal_tree||!s.world_error.isEmpty()||!s.graph->world_normal_error.isEmpty())return NativeWorldNormalContextState::Unsupported;
 if(s.world_image_required&&s.world_prepared&&!s.world_image_readiness.ready())return NativeWorldNormalContextState::PendingImages;
 if(s.world_programs.empty()){if(s.world_layout.generated_stream)return NativeWorldNormalContextState::PendingGenerated;if(s.world_layout.resident_view_context)return NativeWorldNormalContextState::PendingView;if(s.world_layout.resident_baked_ao_context)return NativeWorldNormalContextState::PendingBakedAo;if(s.world_image_required)return NativeWorldNormalContextState::PendingImages;}return NativeWorldNormalContextState::Ready;
}
bool NativeMaterialGpuLibrary::world_normal_requires_generated(const std::string &key)const{return entry(key).world_layout.generated_stream;}
bool NativeMaterialGpuLibrary::world_normal_requires_view(const std::string &key)const{return entry(key).world_layout.resident_view_context;}
bool NativeMaterialGpuLibrary::world_normal_requires_images(const std::string &key)const{return entry(key).world_image_required;}
bool NativeMaterialGpuLibrary::world_normal_requires_baked_ao(const std::string &key)const{return entry(key).world_layout.resident_baked_ao_context;}
void NativeMaterialGpuLibrary::validate_world_normal_ao(const std::string &key,const NativeMaterialBakedAoContext *ao)const{if(world_normal_requires_baked_ao(key)){if(!ao)native_baked_ao_reject(1001,"immutable requested AO snapshot is pending");ao->validate();}}
std::string NativeMaterialGpuLibrary::world_normal_ao_signature(const std::string &key,int tile,const NativeMaterialBakedAoContext *ao)const{if(!world_normal_requires_baked_ao(key))return {};if(!ao)native_baked_ao_reject(tile,"immutable requested AO snapshot is pending");return ao->signature(tile);}

void NativeMaterialGpuLibrary::validate_world_normal_view(const std::string &key,const NativeMaterialViewContext *view)const{if(world_normal_requires_view(key)){if(!view)throw NativeFragmentContextError(NativeFragmentContextCode::View,"Native fragment requires a real validated view: camera is pending");view->validate();}}

QString NativeMaterialGpuLibrary::world_normal_error(const std::string &key)const{const auto &s=entry(key);if(!s.world_error.isEmpty())return s.world_error;if(!s.graph->world_normal_error.isEmpty())return s.graph->world_normal_error;return world_normal_context_state(key)==NativeWorldNormalContextState::PendingGenerated?QStringLiteral("Native Generated is pending authoritative ORCO capture: BTS_Coord_Generated"):world_normal_context_state(key)==NativeWorldNormalContextState::PendingView?QStringLiteral("Native fragment is pending a real validated view"):world_normal_context_state(key)==NativeWorldNormalContextState::PendingBakedAo?QStringLiteral("Native fragment is pending a retained project AO map"):world_normal_context_state(key)==NativeWorldNormalContextState::PendingImages?QStringLiteral("Native fragment is pending retained source-image upload/UV proof"):QString{};}
void NativeMaterialGpuLibrary::validate_world_normal_geometry(const std::string &key,const NativeFragmentGeometry &geometry,const NativeMaterialViewContext *view,const NativeMaterialBakedAoContext *ao)const{
 const auto &s=entry(key);if(world_normal_context_state(key)==NativeWorldNormalContextState::Unsupported)throw std::invalid_argument(world_normal_error(key).toStdString());
 native_world_normal_source_layout({s.graph,s.graph->world_normal_tree},&geometry,view,ao);
}
std::string NativeMaterialGpuLibrary::world_normal_uv(const std::string &key)const{
 const auto &source=entry(key);if(!source.graph->world_normal_tree)throw std::invalid_argument("Native world Normal tree is unavailable");
 if(world_normal_context_state(key)==NativeWorldNormalContextState::Unsupported)throw std::invalid_argument(world_normal_error(key).toStdString());
 return source.world_layout.uv_name;
}
bool NativeMaterialGpuLibrary::prepare_world_normal(const std::string &key,const NativeFragmentGeometry *supplied,const NativeMaterialViewContext *view,const NativeMaterialBakedAoContext *ao,bool tangent_output){
 auto &source=const_cast<Entry &>(entry(key));const auto program_key=source.world_layout.uv_name+(tangent_output?";tangent":"");if(source.world_programs.contains(program_key)){try{if(source.world_image_readiness.validate_available([&]{for(const auto &[uv,program]:source.world_programs)program->validate_images();})){source.world_upload.reset();return source.world_error.isEmpty();}}catch(const NativeFragmentContextError &error){source.world_error=QString::fromUtf8(error.what());return false;}}
 if(world_normal_context_state(key)==NativeWorldNormalContextState::Unsupported)return false;
 if((source.world_layout.generated_stream&&!supplied)||(source.world_layout.resident_view_context&&!view)||(source.world_layout.resident_baked_ao_context&&!ao)||(source.world_layout.default_image_uv&&!source.world_layout.uv_name.empty()&&!supplied))return false;
 auto layout=native_world_normal_source_layout({source.graph,source.graph->world_normal_tree},supplied,view,ao);layout.tangent_output=tangent_output;layout.fill_projection=tangent_output;
 try{
  if(!source.world_image_readiness.step(source.world_image_required,[&]{if(!source.world_upload){std::array<blender::bNodeTree *,1> roots{source.graph->world_normal_tree};source.world_upload=std::make_unique<NativeMaterialImageUpload>(source.graph,roots,false);}if(!source.world_upload->step())return false;source.world_upload.reset();return true;}))return false;
  source.world_image_readiness.require_ready(source.world_image_required);
  if(source.world_programs.contains(program_key)){for(const auto &[uv,program]:source.world_programs)program->validate_images();return true;}
  auto program=std::make_unique<NativeMaterialFragmentGpuProgram>(PreparedNativeFragmentNormal{source.graph,source.graph->world_normal_tree},layout);program->validate_images();source.world_programs.emplace(program_key,std::move(program));source.world_prepared=true;return true;
 }catch(const NativeFragmentImagePending &){source.world_upload.reset();source.world_image_readiness.reset();return false;}catch(const std::exception &e){source.world_upload.reset();source.world_error=QString::fromUtf8(e.what());return false;}
}

std::string NativeMaterialGpuLibrary::world_normal_signature(const std::string &key,const NativeMaterialDescription &instance,const NativeMaterialViewContext *view)const{
 validate_instance(key,instance);validate_world_normal_view(key,view);const auto &source=entry(key);std::string result=source.cache_identity+";WORLD-pair-v1;"+std::to_string(source.revision)+source.world_image_signature;
 for(auto index:source.world_dependencies.parameters){const auto &p=instance.parameters[index];for(int c=0;c<p.components();++c){const float value=float(p.value[c]);result.append(reinterpret_cast<const char *>(&value),sizeof(value));}if(p.mapping_transform_supported())for(const auto &row:p.mapping_transform.rows())result.append(reinterpret_cast<const char *>(row.data()),sizeof(row));if(!p.ramp.isEmpty()){const auto ramp=QJsonDocument(p.ramp).toJson(QJsonDocument::Compact);result.append(ramp.constData(),ramp.size());}}if(source.world_layout.resident_view_context)result+=view->signature();return result;
}
void NativeMaterialGpuLibrary::render_world_normal(const std::string &key,const NativeMaterialDescription &instance,const NativeMaterialFragmentGpuGeometry &geometry,int tile,blender::gpu::Texture *front,blender::gpu::Texture *back,const std::string &uv,const NativeMaterialViewContext *view,const NativeBakedAoInput *ao,bool tangent_output,const NativeMaterialProjection *projection){
 validate_instance(key,instance);validate_world_normal_view(key,view);if(world_normal_requires_baked_ao(key)){if(!ao)native_baked_ao_reject(tile,"retained draw binding is pending");ao->validate(tile);}auto &source=const_cast<Entry &>(entry(key));
 if(instance.height_policy!=NativeHeightPolicy::None)throw std::invalid_argument("Raw authored Height and native world Normal are mutually exclusive");
 if(!source.graph->world_normal_tree)throw std::invalid_argument(source.graph->world_normal_error.isEmpty()?"Native source has no effective Normal perturbation":source.graph->world_normal_error.toStdString());
 const auto found=source.world_programs.find(uv+(tangent_output?";tangent":""));if(found==source.world_programs.end())throw std::invalid_argument(source.world_error.isEmpty()?"Native world Normal program is not prepared":source.world_error.toStdString());auto &program=found->second;
 std::vector<std::array<float,4>> rows;std::vector<NativeMaterialMapping> mappings;
 for(const auto &p:instance.parameters){std::array<float,4> row{0,0,0,1};if(p.type==NativeParameterType::Scalar)row={float(p.value[0]),float(p.value[0]),float(p.value[0]),1};else for(int c=0;c<p.components();++c)row[c]=float(p.value[c]);rows.push_back(row);mappings.push_back(p.mapping_transform);}
 program->update_ramps(instance);program->draw_tile(front,geometry,tile,rows,mappings,false,view,ao,projection);if(back)program->draw_tile(back,geometry,tile,rows,mappings,true,view,ao,projection);source.dispatches+=back?2:1;source.world_dispatches+=back?2:1;
}
void NativeMaterialGpuLibrary::prepare(const std::string &resource,int channel){
 auto &source=const_cast<Entry &>(entry(resource));if(source.programs.contains(channel))return;const auto found=std::find_if(source.graph->channels.begin(),source.graph->channels.end(),[&](const auto &value){return value.channel==channel;});if(found==source.graph->channels.end())throw std::invalid_argument("Native material channel is not supported");if(!found->tree)throw std::runtime_error(found->error.toStdString());source.programs.emplace(channel,std::make_unique<NativeMaterialGpuProgram>(*found->tree,source.graph->parameters.size(),channel,source.graph));
}
bool NativeMaterialGpuLibrary::prepare_images(const std::string &resource){
 auto &source=const_cast<Entry &>(entry(resource));if(!source.source_image_error.isEmpty())return false;
 try{
  const auto validate=[&](const auto &graph,const auto &requests){native_tiled_requests_validate(requests,[&](const auto &request){retained_native_tiled_gpu_image(graph,request.image,request.representation);});};
  const bool ordinary_ready=source.source_image_readiness.validate_available([&]{validate(source.graph,source.image_requests);});NativeFragmentImageReadiness height_readiness;const bool height_ready=!source.height||height_readiness.validate_available([&]{validate(source.height->resource.prepared.source,source.height->image_requests);});
  if(ordinary_ready)source.source_upload.reset();if(height_ready)source.height_upload.reset();if(ordinary_ready&&height_ready)return true;
  if(!ordinary_ready&&!source.source_upload)source.source_upload=std::make_unique<NativeMaterialImageUpload>(source.graph);
  if(!height_ready&&!source.height_upload){auto &prepared=source.height->resource.prepared;std::array<blender::bNodeTree *,1> roots{prepared.tree};source.height_upload=std::make_unique<NativeMaterialImageUpload>(prepared.source,roots,false);}
 }catch(const std::exception &error){source.source_image_error=source.graph->description.material+": "+QString::fromUtf8(error.what());source.source_upload.reset();source.height_upload.reset();}return false;
}
void NativeMaterialGpuLibrary::step_image_uploads(std::size_t packet_bytes){
 // One action globally per preview frame. A retired/inactive source still drains
 // its finite upload, so pending status cannot spin forever waiting on a layer.
 for(auto &[key,entry]:resources_){auto *pending=entry->source_upload?&entry->source_upload:entry->height_upload?&entry->height_upload:nullptr;if(!pending)continue;try{if((*pending)->step(packet_bytes))pending->reset();}catch(const std::exception &error){entry->source_image_error=entry->graph->description.material+": "+QString::fromUtf8(error.what());entry->source_upload.reset();entry->height_upload.reset();}return;}
}
bool NativeMaterialGpuLibrary::image_uploads_pending()const{for(const auto &[key,source]:resources_)if(source->source_upload||source->height_upload)return true;return false;}
QString NativeMaterialGpuLibrary::image_error(const std::string &key)const{const auto found=resources_.find(key);return found==resources_.end()?QString{}:found->second->source_image_error;}

void NativeMaterialGpuLibrary::render(const std::string &resource,int channel,const NativeMaterialDescription &instance,blender::gpu::Texture *target,const NativeMaterialGpuContext &context,std::array<int,4> region){
 auto &source=const_cast<Entry &>(entry(resource));validate_instance(resource,instance);if(!prepare_images(resource))throw NativeFragmentImagePending("Native TILED source recovery is pending; target is unchanged");
 std::vector<std::array<float,4>> parameters;parameters.reserve(instance.parameters.size());std::vector<NativeMaterialMapping> mappings;mappings.reserve(instance.parameters.size());
 for(std::size_t i=0;i<instance.parameters.size();++i){const auto &value=instance.parameters[i];std::array<float,4> encoded{0,0,0,1};if(value.type==NativeParameterType::Scalar)encoded={float(value.value[0]),float(value.value[0]),float(value.value[0]),1};else for(int c=0;c<value.components();++c)encoded[c]=float(value.value[c]);parameters.push_back(encoded);mappings.push_back(value.mapping_transform);}
 NativeMaterialGpuProgram *program=nullptr;
 if(channel==4&&instance.height_policy==NativeHeightPolicy::RawAuthoredFloatV1){if(!source.height->program)source.height->program=std::make_unique<NativeMaterialGpuProgram>(*source.height->resource.prepared.tree,source.graph->parameters.size(),4,source.height->resource.prepared.source);program=source.height->program.get();}
 else {prepare(resource,channel);program=source.programs.at(channel).get();}
 program->update_ramps(instance);program->dispatch(target,parameters,context,region,mappings);++source.dispatches;source.texels+=std::uint64_t(region[2])*region[3];
}
}
