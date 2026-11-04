#pragma once
// Explicit generated-EEVEE-source ownership prerequisite only. No ordinary
// EEVEE async policy, material appearance, Main/image-free or latency claim.
#include "HostedGpuPassReadinessTest.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "DNA_material_types.h"
#include "gpu_codegen.hh"
#include "eevee_material.hh"
#include "eevee_shader.hh"
#include <algorithm>
#include <functional>
#include <vector>

namespace bts::hosted_eevee_generated_test {
using namespace blender;
using hosted_gpupass_test::require;
struct Evidence {
 int callbacks=0,attributes=0,generated_sources=0,generated_fragment_bytes=0;
 bool actual_surface=false,checker_in_surface=false,owned_interface=false;
 bool interface_retained=false,generated_fragment=false,real_graphics_stages=false;
};
struct Forwarding {
 eevee::ShaderModule &module;Evidence &evidence;
};
inline void codegen(void *data,GPUMaterial *material,GPUCodegenOutput *output){
 auto &forward=*static_cast<Forwarding *>(data);auto &e=forward.evidence;++e.callbacks;
 e.actual_surface=GPU_material_has_surface_output(material)&&!output->surface.empty();
 e.checker_in_surface=output->surface.serialized.find("node_tex_checker(")!=std::string::npos&&output->surface.serialized.find("node_emission(")!=std::string::npos;
 auto &info=*reinterpret_cast<gpu::shader::ShaderCreateInfo *>(output->create_info);
 auto &owned=static_cast<gpu::shader::GPUCodegenCreateInfo &>(info);
 e.attributes=GPU_material_attributes(material).count();
 e.owned_interface=owned.interface_generated&&!owned.interface_generated->inouts.is_empty();
 // Exact production EEVEE amendment: no source replacement, compute adapter,
 // copy/move of the CreateInfo, caller pinning or serialized stale graph.
 forward.module.material_create_info_amend(material,output);
 e.interface_retained=owned.interface_generated&&std::find(info.vertex_out_interfaces_.begin(),info.vertex_out_interfaces_.end(),owned.interface_generated)!=info.vertex_out_interfaces_.end();
 e.generated_sources=info.generated_sources.size();
 for(const auto &source:info.generated_sources)if(source.filename=="eevee_nodetree_frag_lib.glsl"){
  e.generated_fragment=source.content.find("Closure nodetree_surface(")!=std::string::npos&&source.content.find("node_tex_checker(")!=std::string::npos;
  e.generated_fragment_bytes=source.content.size();
 }
 e.real_graphics_stages=!info.vertex_source_.is_empty()&&!info.fragment_source_.is_empty()&&info.compute_source_.is_empty();
}
inline Material *fixture(Main &main){
 auto *material=BKE_material_add(&main,"Hosted generated EEVEE ownership fixture");require(material&&material->nodetree,"Private embedded material tree missing");auto &tree=*material->nodetree;
 auto *attribute=bke::node_add_node(nullptr,tree,UString("ShaderNodeAttribute"));
 auto *checker=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexChecker"));
 auto *emission=bke::node_add_node(nullptr,tree,UString("ShaderNodeEmission"));
 auto *output=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));
 require(attribute&&attribute->storage&&checker&&emission&&output,"Private generated-source nodes missing");
 auto &settings=*static_cast<NodeShaderAttribute *>(attribute->storage);settings.type=SHD_ATTRIBUTE_GEOMETRY;STRNCPY(settings.name,"HostedGeneratedOwnershipAttribute");output->flag|=NODE_DO_OUTPUT;
 const auto connect=[&](bNode &from,const char *out,bNode &to,const char *in){auto *a=bke::node_find_enabled_output_socket(from,out);auto *b=bke::node_find_enabled_input_socket(to,in);require(a&&b,"Generated-source fixture socket missing");bke::node_add_link(tree,from,*a,to,*b);};
 connect(*attribute,"Vector",*checker,"Vector");connect(*checker,"Color",*emission,"Color");connect(*emission,"Emission",*output,"Surface");BKE_ntree_update_without_main(tree);return material;
}
inline int run(BlenderHost &host,const QString &directory){
 bool paused=false;Main *main=nullptr;Material *material=nullptr;GPUPass *pass=nullptr;
 std::vector<AsyncCompilationHandle> barriers;QString error;QElapsedTimer clock;clock.start();QString stage="setup";
 QJsonObject proof{{"kind","real generated EEVEE CreateInfo ownership prerequisite"},{"native_pid",qint64(QCoreApplication::applicationPid())},{"renderer",QString::fromStdString(host.renderer())},{"EEVEE_async_policy_enabled",false},{"static_CreateInfo_substitution",false},{"material_appearance_claim",false},{"Main_or_image_free_claim",false},{"UI_cancel_safety_claim",false},{"latency_claim",false},{"CPU_poll_provenance","Source-audited CPU queries only; not an instrumented GL-call count"}};
 const auto wait_cpu=[&](const std::function<bool()> &ready,int deadline){
  QElapsedTimer timer;timer.start();QEventLoop loop;QTimer poll;poll.setInterval(10);bool done=false;QString failure;
  QObject::connect(&poll,&QTimer::timeout,&loop,[&]{
   try{require(timer.elapsed()<deadline,"Generated-source diagnostic CPU wait deadline exceeded");if(ready()){done=true;poll.stop();loop.quit();}}
   catch(const std::exception &e){failure=QString::fromUtf8(e.what());poll.stop();loop.quit();}
   catch(...){failure="Unknown generated-source CPU poll error";poll.stop();loop.quit();}
  });
  poll.start();loop.exec();if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());require(done,"CPU wait ended before readiness");
 };
 try{
  require(qEnvironmentVariableIntValue("BTS_HOSTED_SHADER_WORKERS")==1&&GPU_context_hosted_shader_workers_enabled(),"Generated-source diagnostic requires the real hosted THREAD worker path");
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
   main=BKE_main_new();require(main,"Private fixture Main missing");material=fixture(*main);
   {
    eevee::ShaderModule local_module;Forwarding local_thunk{local_module,evidence};
    const auto uuid=eevee::shader_uuid_from_material_type(eevee::MAT_PIPE_DEFERRED,eevee::MAT_GEOM_MESH);
    auto result=GPU_material_from_nodetree(material,material->nodetree,&material->gpumaterial,material->id.name,GPU_MAT_EEVEE,uuid,true,codegen,&local_thunk);
    require(result.material&&result.errors.is_empty(),"Real generated material lowering failed");pass=GPU_material_get_pass(result.material);
    require(pass&&GPU_pass_status(pass)==GPU_PASS_QUEUED&&!GPU_pass_shader_get(pass),"Generated material did not queue");
    require(evidence.callbacks==1&&evidence.actual_surface&&evidence.checker_in_surface&&evidence.attributes>0&&evidence.owned_interface&&evidence.interface_retained&&evidence.generated_fragment&&evidence.generated_fragment_bytes>0&&evidence.real_graphics_stages,"Actual EEVEE generated fragment/interface evidence missing");
    require(GPU_hosted_shader_worker_compilation_count()==jobs_parked,"Material worker ran before local owner destruction");
   } // Forwarding thunk and local ShaderModule really expire before resume.
   // The API also returned across GPU_generate_pass's real stack GPUCodegen /
   // GPUCodegenOutput destruction. Original fixture Main/material/tree remain.
   require(GPU_hosted_shader_worker_compilation_count()==jobs_parked&&GPU_pass_status(pass)==GPU_PASS_QUEUED&&GPU_pass_cache_has_pending_compilation(),"Queued pass changed while every worker should be parked");
  });
  proof["actual_codegen_callbacks"]=evidence.callbacks;proof["attributes"]=evidence.attributes;proof["generated_source_entries"]=evidence.generated_sources;proof["generated_fragment_bytes"]=evidence.generated_fragment_bytes;
  proof["actual_generated_fragment_and_owned_interface"]=true;proof["local_module_and_forwarding_thunk_destroyed_before_resume"]=true;proof["source_audited_GPUCodegen_stack_return_boundary_crossed"]=true;proof["material_worker_count_unchanged_before_and_after_local_scope"]=true;
  proof["original_Main_Material_tree_GPUMaterial_retained"]=true;
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
   if(main){BKE_main_free(main);main=nullptr;material=nullptr;pass=nullptr;}
  });
  host.shutdown();
 }catch(...){qFatal("Generated EEVEE diagnostic terminal cleanup failed");}
 const auto stats=shared_shader_context_stats();const bool disposed=stats.created>0&&stats.created==stats.disposed_on_gui&&stats.live==0&&stats.pending==0&&stats.worker_releases>=stats.worker_activations;
 if(!disposed&&error.isEmpty())error="Generated EEVEE worker contexts survived terminal join/disposal";
 proof["last_stage"]=stage;proof["contexts_created"]=qint64(stats.created);proof["disposed_on_GUI"]=qint64(stats.disposed_on_gui);proof["live_after_join"]=qint64(stats.live);proof["pending_GUI_retirements"]=qint64(stats.pending);proof["join_and_owner_disposal_complete"]=disposed;proof["elapsed_ms"]=clock.elapsed();proof["passed"]=error.isEmpty();proof["error"]=error;
 QFile file(directory+"/hosted-eevee-generated-source.json");require(file.open(QIODevice::WriteOnly),"Cannot write generated EEVEE ownership proof");file.write(QJsonDocument(proof).toJson());std::printf("BTS_HOSTED_EEVEE_GENERATED_SOURCE passed=%d disposed=%d\n",int(error.isEmpty()),int(disposed));std::fflush(stdout);return error.isEmpty()?0:1;
}
} // namespace bts::hosted_eevee_generated_test
