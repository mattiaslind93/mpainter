// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Explicit RETAINED-pass Image/Group compiler-source ownership diagnostic.
// No renderer/Image binding, zero-ref orphan/GC, UI Cancel or latency claim.
#include "HostedGpuPassReadinessTest.hh"
#include "BKE_image.hh"
#include "DNA_image_types.h"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "DNA_material_types.h"
#include "gpu_codegen.hh"
#include "eevee_material.hh"
#include "eevee_shader.hh"
#include <algorithm>
#include <functional>
#include <vector>

namespace bts::hosted_eevee_image_test {
using namespace blender;
using hosted_gpupass_test::require;
struct Evidence {
 int callbacks=0,attributes=0,generated_sources=0,generated_fragment_bytes=0;
 bool actual_surface=false,image_in_surface=false,owned_interface=false;
 bool interface_retained=false,generated_fragment=false,real_graphics_stages=false;
 bool exact_Image_binding=false,sampler_names_owned=false;int image_bindings=0;
};
struct Forwarding {
 eevee::ShaderModule &module;Evidence &evidence;Image *expected_image;
};
inline void codegen(void *data,GPUMaterial *material,GPUCodegenOutput *output){
 auto &forward=*static_cast<Forwarding *>(data);auto &e=forward.evidence;++e.callbacks;
 e.actual_surface=GPU_material_has_surface_output(material)&&!output->surface.empty();
 e.image_in_surface=output->surface.serialized.find("node_tex_image_linear(")!=std::string::npos&&output->surface.serialized.find("node_emission(")!=std::string::npos;
 auto &info=*reinterpret_cast<gpu::shader::ShaderCreateInfo *>(output->create_info);
 auto &owned=static_cast<gpu::shader::GPUCodegenCreateInfo &>(info);
 e.attributes=GPU_material_attributes(material).count();
 e.exact_Image_binding=true;
 for(const auto &texture:GPU_material_textures(material)){
  ++e.image_bindings;e.exact_Image_binding&=texture.ima==forward.expected_image&&texture.iuser_available&&!texture.tiled_mapping_name[0];
 }
 // Pointer identity is checked while live; do not dereference an unowned view
 // after fixture deletion merely to diagnose a regressed lifetime.
 e.sampler_names_owned=true;int copied_samplers=0;
 for(const auto &resource:info.batch_resources_)if(resource.bind_type==gpu::shader::ShaderCreateInfo::Resource::SAMPLER){
  bool owned_name=false;for(const auto &name:owned.name_buffer.sampler_names)owned_name|=resource.sampler.name.c_str()==name->data();
  e.sampler_names_owned&=owned_name;++copied_samplers;
 }
 e.sampler_names_owned&=copied_samplers==1;
 e.owned_interface=owned.interface_generated&&!owned.interface_generated->inouts.is_empty();
 // Exact production EEVEE amendment: no source replacement, compute adapter,
 // copy/move or caller repair of the CreateInfo, or serialized stale graph.
 forward.module.material_create_info_amend(material,output);
 e.interface_retained=owned.interface_generated&&std::find(info.vertex_out_interfaces_.begin(),info.vertex_out_interfaces_.end(),owned.interface_generated)!=info.vertex_out_interfaces_.end();
 e.generated_sources=info.generated_sources.size();
 for(const auto &source:info.generated_sources)if(source.filename=="eevee_nodetree_frag_lib.glsl"){
  e.generated_fragment=source.content.find("Closure nodetree_surface(")!=std::string::npos&&source.content.find("node_tex_image_linear(")!=std::string::npos;
  e.generated_fragment_bytes=source.content.size();
 }
 e.real_graphics_stages=!info.vertex_source_.is_empty()&&!info.fragment_source_.is_empty()&&info.compute_source_.is_empty();
}
inline Material *fixture(Main &main,Image *&image,bNodeTree *&group){
 auto *material=BKE_material_add(&main,"Retained pass Image Group fixture");require(material&&material->nodetree,"Private embedded material tree missing");auto &tree=*material->nodetree;
 const float color[4]={.2f,.4f,.8f,1.f};image=BKE_image_add_generated(&main,2,2,"Compiler source-only Image",32,1,IMA_GENTYPE_BLANK,color,false,true,false);require(image,"Private generated Image missing");
 group=bke::node_tree_add_tree(&main,"Compiler source-only Group","ShaderNodeTree");require(group,"Private Group tree missing");
 auto *interface=group->tree_interface.add_socket("Shader","","NodeSocketShader",NODE_INTERFACE_SOCKET_OUTPUT,nullptr);require(interface,"Private shader Group interface missing");const std::string shader_output=interface->identifier;
 auto *attribute=bke::node_add_node(nullptr,*group,UString("ShaderNodeAttribute"));
 auto *texture=bke::node_add_node(nullptr,*group,UString("ShaderNodeTexImage"));
 auto *emission=bke::node_add_node(nullptr,*group,UString("ShaderNodeEmission"));
 auto *output=bke::node_add_node(nullptr,*group,UString("NodeGroupOutput"));
 require(attribute&&attribute->storage&&texture&&texture->storage&&emission&&output,"Private Image Group nodes missing");
 auto &settings=*static_cast<NodeShaderAttribute *>(attribute->storage);settings.type=SHD_ATTRIBUTE_GEOMETRY;STRNCPY(settings.name,"HostedImageGroupOwnershipAttribute");
 texture->id=&image->id;id_us_plus(texture->id);auto &tex=*static_cast<NodeTexImage *>(texture->storage);tex.interpolation=SHD_INTERP_LINEAR;tex.projection=SHD_PROJ_FLAT;output->flag|=NODE_DO_OUTPUT;
 BKE_ntree_update_after_single_tree_change(main,*group);
 const auto socket=[](bNode &node,const char *id,bool input)->bNodeSocket &{for(auto &value:input?node.inputs:node.outputs)if(STREQ(value.identifier,id))return value;throw std::runtime_error("Image Group fixture socket missing");};
 const auto connect=[&](bNodeTree &owner,bNode &from,const char *out,bNode &to,const char *in){bke::node_add_link(owner,from,socket(from,out,false),to,socket(to,in,true));};
 connect(*group,*attribute,"Vector",*texture,"Vector");connect(*group,*texture,"Color",*emission,"Color");connect(*group,*emission,"Emission",*output,shader_output.c_str());BKE_ntree_update_after_single_tree_change(main,*group);
 auto *instance=bke::node_add_node(nullptr,tree,UString("ShaderNodeGroup"));auto *surface=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));require(instance&&surface,"Private Group instance/output missing");instance->id=&group->id;id_us_plus(instance->id);surface->flag|=NODE_DO_OUTPUT;BKE_ntree_update_after_single_tree_change(main,tree);
 connect(tree,*instance,shader_output.c_str(),*surface,"Surface");BKE_ntree_update_after_single_tree_change(main,tree);return material;
}
inline int run(BlenderHost &host,const QString &directory){
 bool paused=false,observer_reference=false;Main *main=nullptr;Material *material=nullptr;Image *image=nullptr;bNodeTree *group=nullptr;GPUPass *pass=nullptr;
 std::vector<AsyncCompilationHandle> barriers;QString error;QElapsedTimer clock;clock.start();QString stage="setup";
 QJsonObject proof{{"kind","retained-pass generated EEVEE Image Group compiler-source lifetime"},{"native_pid",qint64(QCoreApplication::applicationPid())},{"renderer",QString::fromStdString(host.renderer())},{"EEVEE_async_policy_enabled",false},{"static_CreateInfo_substitution",false},{"material_appearance_claim",false},{"private_fixture_Main_Image_group_destroyed_before_compile",false},{"zero_ref_orphan_or_GC_claim",false},{"rendering_or_image_sampling_claim",false},{"UI_cancel_safety_claim",false},{"latency_claim",false},{"CPU_poll_provenance","Source-audited CPU queries only; not an instrumented GL-call count"}};
 const auto wait_cpu=[&](const std::function<bool()> &ready,int deadline){
  QElapsedTimer timer;timer.start();QEventLoop loop;QTimer poll;poll.setInterval(10);bool done=false;QString failure;
  QObject::connect(&poll,&QTimer::timeout,&loop,[&]{
   try{require(timer.elapsed()<deadline,"Image/Group diagnostic CPU wait deadline exceeded");if(ready()){done=true;poll.stop();loop.quit();}}
   catch(const std::exception &e){failure=QString::fromUtf8(e.what());poll.stop();loop.quit();}
   catch(...){failure="Unknown generated-source CPU poll error";poll.stop();loop.quit();}
  });
  poll.start();loop.exec();if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());require(done,"CPU wait ended before readiness");
 };
 try{
  require(qEnvironmentVariableIntValue("BTS_HOSTED_SHADER_WORKERS")==1&&GPU_context_hosted_shader_workers_enabled(),"Image/Group diagnostic requires the real hosted THREAD worker path");
  // Deliberate diagnostic-only setup; existing callbacks may submit later Host
  // phases. Drain all of them before any worker-count snapshot or pool pause.
  std::uint64_t setup_flushes=0;
  wait_cpu([&]{
   if(GPU_shader_compiler_has_pending_work())return false;
   if(host.texture_updates_pending()){host.flush_texture_updates();++setup_flushes;return false;}
   host.with_gpu([&]{GPU_pass_cache_update();});
   return !GPU_shader_compiler_has_pending_work()&&!host.texture_updates_pending()&&!GPU_pass_cache_has_pending_compilation();
  },120000);
  proof["all_Host_startup_drained"]=true;proof["setup_owner_flushes"]=qint64(setup_flushes);
  const auto jobs_before=GPU_hosted_shader_worker_compilation_count();
  const auto contexts_before=shared_shader_context_stats();const int count=GPU_max_parallel_compilations();require(count>0,"THREAD pool has no workers");
  proof["pool_N"]=count;proof["worker_jobs_before_barriers"]=qint64(jobs_before);
  stage="parking actual THREAD pool";GPU_shader_compiler_pause();paused=true;
  host.with_gpu([&]{const auto *info=GPU_shader_create_info_get("compositor_write_output");require(info,"Static parking CreateInfo missing");
   for(int i=0;i<count;++i){auto handle=GPU_shader_async_compilation(info);require(handle!=0,"Parking submission failed");barriers.push_back(handle);}
  });
  // Pause takes effect AFTER a callback publishes ready. N ready handles mean
  // every one of the N PerThread callbacks is paused before its next queue pop.
  // Do not wait_for_all while paused, finalize a barrier, or enter GL here.
  wait_cpu([&]{return std::all_of(barriers.begin(),barriers.end(),[](auto handle){return GPU_shader_async_compilation_is_ready(handle);});},60000);
  const auto jobs_parked=GPU_hosted_shader_worker_compilation_count();require(jobs_parked==jobs_before+count,"Parking window contained missing or unrelated worker jobs");
  proof["all_N_barrier_handles_ready_before_material"]=true;proof["barrier_worker_job_delta"]=qint64(jobs_parked-jobs_before);
  Evidence evidence;stage="actual EEVEE codegen with all workers parked";
  host.with_gpu([&]{
   main=BKE_main_new();require(main,"Private fixture Main missing");material=fixture(*main,image,group);
   {
    eevee::ShaderModule local_module;Forwarding local_thunk{local_module,evidence,image};
    const auto uuid=eevee::shader_uuid_from_material_type(eevee::MAT_PIPE_DEFERRED,eevee::MAT_GEOM_MESH);
    auto result=GPU_material_from_nodetree(material,material->nodetree,&material->gpumaterial,material->id.name,GPU_MAT_EEVEE,uuid,true,codegen,&local_thunk);
    require(result.material&&result.errors.is_empty(),"Real generated material lowering failed");pass=GPU_material_get_pass(result.material);
    require(pass&&GPU_pass_status(pass)==GPU_PASS_QUEUED&&!GPU_pass_shader_get(pass),"Generated material did not queue");
    GPU_pass_acquire(pass);observer_reference=true; // Pin ONLY the pass, never fixture IDs.
    require(evidence.callbacks==1&&evidence.actual_surface&&evidence.image_in_surface&&evidence.attributes>0&&evidence.owned_interface&&evidence.interface_retained&&evidence.generated_fragment&&evidence.generated_fragment_bytes>0&&evidence.real_graphics_stages&&evidence.image_bindings==1&&evidence.exact_Image_binding&&evidence.sampler_names_owned,"Actual EEVEE Image Group fragment/interface/sampler evidence missing");
    require(GPU_hosted_shader_worker_compilation_count()==jobs_parked,"Material worker ran before local owner destruction");
   } // Forwarding thunk and local ShaderModule really expire before resume.
   // Temporary GPUCodegen/output have already returned. Now drop the graph
   // owner FIRST, then every private ID; only an explicit pass reference survives.
   require(observer_reference&&main&&image&&group&&GPU_hosted_shader_worker_compilation_count()==jobs_parked,"Fixture disappeared before retained-pass boundary");
   GPU_material_free(&material->gpumaterial);material=nullptr;
   BKE_main_free(main);main=nullptr;image=nullptr;group=nullptr;
   require(GPU_hosted_shader_worker_compilation_count()==jobs_parked&&GPU_pass_status(pass)==GPU_PASS_QUEUED&&GPU_pass_cache_has_pending_compilation(),"Pass changed while all workers should be parked after private Main teardown");
  });
  proof["actual_codegen_callbacks"]=evidence.callbacks;proof["attributes"]=evidence.attributes;proof["generated_source_entries"]=evidence.generated_sources;proof["generated_fragment_bytes"]=evidence.generated_fragment_bytes;
  proof["actual_generated_fragment_and_owned_interface"]=true;proof["local_module_and_forwarding_thunk_destroyed_before_resume"]=true;proof["source_audited_GPUCodegen_stack_return_boundary_crossed"]=true;proof["material_worker_count_unchanged_before_and_after_local_scope"]=true;
  proof["explicit_observer_pass_ref_acquired_before_material_free"]=true;proof["private_fixture_Main_Image_group_destroyed_before_compile"]=true;proof["original_GPUMaterial_graph_freed_before_private_IDs"]=true;proof["exact_Image_binding_seen_during_lowering"]=evidence.exact_Image_binding;proof["sampler_names_owned_by_CreateInfo"]=evidence.sampler_names_owned;proof["image_bindings"]=evidence.image_bindings;
  stage="real generated material worker";GPU_shader_compiler_resume();paused=false;
  wait_cpu([&]{return !GPU_shader_compiler_has_pending_work();},60000);
  const auto jobs_ready=GPU_hosted_shader_worker_compilation_count();require(jobs_ready==jobs_parked+1,"Generated material did not execute exactly one isolated worker job");
  require(!host.texture_updates_pending()&&GPU_pass_status(pass)==GPU_PASS_QUEUED&&!GPU_pass_shader_get(pass)&&GPU_pass_cache_has_pending_compilation(),"Real generated pass ready-unfinalized query failed");
  proof["material_worker_job_delta"]=qint64(jobs_ready-jobs_parked);proof["ready_unfinalized_observed"]=true;
  stage="owner finalize generated EEVEE pass";
  host.with_gpu([&]{const auto before=GPU_pass_global_compilation_count();GPU_pass_cache_update();
   require(GPU_pass_status(pass)==GPU_PASS_SUCCESS&&GPU_pass_shader_get(pass),"Actual generated EEVEE pass did not compile successfully");
   require(GPU_pass_global_compilation_count()==before+1&&!GPU_pass_cache_has_pending_compilation(),"Owner did not finalize exactly one generated pass");
  });
  proof["GPU_PASS_SUCCESS"]=true;proof["owner_finalized_exactly_one_pass"]=true;
  const auto contexts_after=shared_shader_context_stats();proof["contexts_created_before"]=qint64(contexts_before.created);proof["contexts_created_after"]=qint64(contexts_after.created);proof["context_activations_before"]=qint64(contexts_before.worker_activations);proof["context_activations_after"]=qint64(contexts_after.worker_activations);proof["first_context_thread"]=QString::number(contexts_after.first_worker_thread,16);
  require(contexts_after.worker_activations>0&&contexts_after.first_worker_thread!=reinterpret_cast<std::uintptr_t>(QThread::currentThreadId()),"No actual non-GUI THREAD context");
 }catch(const std::exception &e){error=QString::fromUtf8(e.what());}
 catch(...){error="Unknown real generated EEVEE ownership diagnostic error";}
 // Even a failed parking/material phase must resume every worker, then wait
 // terminally BEFORE GL cleanup. Never wait for queued jobs while still paused.
 if(paused){GPU_shader_compiler_resume();paused=false;}
 try{
  GPU_shader_compiler_wait_for_all();host.with_gpu([&]{
   for(auto &handle:barriers)if(handle)GPU_shader_async_compilation_cancel(handle);
   if(main){for(Material &owner:main->materials)GPU_material_free(&owner.gpumaterial);BKE_main_free(main);main=nullptr;material=nullptr;image=nullptr;group=nullptr;}
   if(observer_reference){GPU_pass_release(pass);observer_reference=false;pass=nullptr;proof["observer_ref_released_before_Host_exit"]=true;}
  });
  host.shutdown();
 }catch(...){qFatal("EEVEE Image Group diagnostic terminal cleanup failed");}
 const auto stats=shared_shader_context_stats();const bool disposed=stats.created>0&&stats.created==stats.disposed_on_gui&&stats.live==0&&stats.pending==0&&stats.worker_releases>=stats.worker_activations;
 if(!disposed&&error.isEmpty())error="EEVEE Image Group worker contexts survived terminal join/disposal";
 proof["last_stage"]=stage;proof["contexts_created"]=qint64(stats.created);proof["disposed_on_GUI"]=qint64(stats.disposed_on_gui);proof["live_after_join"]=qint64(stats.live);proof["pending_GUI_retirements"]=qint64(stats.pending);proof["join_and_owner_disposal_complete"]=disposed;proof["elapsed_ms"]=clock.elapsed();proof["passed"]=error.isEmpty();proof["error"]=error;
 QFile file(directory+"/hosted-eevee-image-lifetime.json");require(file.open(QIODevice::WriteOnly),"Cannot write generated EEVEE ownership proof");file.write(QJsonDocument(proof).toJson());std::printf("BTS_HOSTED_EEVEE_IMAGE_LIFETIME passed=%d disposed=%d\n",int(error.isEmpty()),int(disposed));std::fflush(stdout);return error.isEmpty()?0:1;
}
} // namespace bts::hosted_eevee_image_test
