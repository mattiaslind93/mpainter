// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialBundleRoundtrip.hh"
#include "NativeMaterialImageUpload.hh"
#include "NativeMaterialResource.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialGpuCompiler.hh"
#include "NativeMaterialStackTest.hh"
#include "NativeMaterialImages.hh"
#include "NativeMaterialArchive.hh"
#include <QSaveFile>
#include "NativeMaterialGpuProgramTest.hh"
#include "NativeMaterialGpuPrototype.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialMappingSourceTest.hh"
#include "BlendMaterial.hh"
#include <QJsonArray>
#include "BKE_image.hh"
#include "BKE_material.hh"
#include "DNA_material_types.h"
#include <charconv>
#include <future>
#include <QString>
#include <QScopeGuard>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_lib_id.hh"
#include "BLI_hash.h"
#include "BLI_listbase.h"
#include "BKE_main.hh"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_node_types.h"
#include "NOD_shader.h"
#include "GPU_material.hh"
#include "GPU_material_hosted.hh"
#include "GPU_shader.hh"
#include "GPU_pass.hh"
#include "GPU_texture.hh"
#include "GPU_compute.hh"
#include "GPU_uniform_buffer.hh"
#include "GPU_storage_buffer.hh"
#include "GPU_state.hh"
#include "GPU_capabilities.hh"
#include "gpu_shader_create_info.hh"
#include "MEM_guardedalloc.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace bts {namespace {
using namespace blender;
using Compilation=NativeMaterialCompilation;
bNodeSocket &socket(bNode &node,const char *identifier,bool input){auto &sockets=input?node.inputs:node.outputs;for(auto &s:sockets)if(STREQ(s.identifier,identifier))return s;throw std::runtime_error("Prototype native socket missing");}
}
void verify_native_material_gpu_prototype(const blender::Main &reference){
 using namespace blender;const auto require=[](bool condition,const char *message){if(!condition)throw std::runtime_error(message);};
 auto *main=BKE_main_new();std::unique_ptr<Main,decltype(&BKE_main_free)> release_main(main,BKE_main_free);auto *tree=bke::node_tree_add_tree(main,"mPainter native channel prototype","ShaderNodeTree");tree->flag|=NTREE_IS_GPU_SHADER_INTERNAL;
 auto *rgb=bke::node_add_node(nullptr,*tree,UString("ShaderNodeRGB"));auto &color=socket(*rgb,"Color",false);const float expected[4]{.23f,.4f,.8f,1};std::copy_n(expected,4,static_cast<bNodeSocketValueRGBA *>(color.default_value)->value);
 auto *aov=bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputAOV"));STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name,"BTS_Color");bke::node_add_link(*tree,*rgb,color,*aov,socket(*aov,"Color",true));
 auto *parameter=bke::node_add_node(nullptr,*tree,UString("ShaderNodeAttribute"));STRNCPY(static_cast<NodeShaderAttribute *>(parameter->storage)->name,"BTS_Param0");
 auto *xyz=bke::node_add_node(nullptr,*tree,UString("ShaderNodeCombineXYZ"));for(const char *axis:{"X","Y","Z"})static_cast<bNodeSocketValueFloat *>(socket(*xyz,axis,true).default_value)->value=.37f;
 auto *noise=bke::node_add_node(nullptr,*tree,UString("ShaderNodeTexNoise"));bke::node_add_link(*tree,*xyz,socket(*xyz,"Vector",false),*noise,socket(*noise,"Vector",true));
 auto *multiply=bke::node_add_node(nullptr,*tree,UString("ShaderNodeMath"));multiply->custom1=NODE_MATH_MULTIPLY;auto *first=static_cast<bNodeSocket *>(multiply->inputs.first);auto *second=first->next;bke::node_add_link(*tree,*noise,socket(*noise,"Fac",false),*multiply,*first);bke::node_add_link(*tree,*parameter,socket(*parameter,"Fac",false),*multiply,*second);
 auto *scalar=bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputAOV"));STRNCPY(static_cast<NodeShaderOutputAOV *>(scalar->storage)->name,"BTS_Scalar");bke::node_add_link(*tree,*multiply,socket(*multiply,"Value",false),*scalar,socket(*scalar,"Value",true));
 bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(*main,*tree);
 Compilation compilation{tree};QElapsedTimer timer;timer.start();auto *material=compile_native_material(compilation);std::unique_ptr<GPUMaterial,decltype(&GPU_material_free_single)> release_material(material,GPU_material_free_single);
 const QString output=qEnvironmentVariable("BTS_NATIVE_MATERIAL_GPU_TEST");QFile source(output+"/native-channel-generated.glsl");if(source.open(QIODevice::WriteOnly))source.write(QByteArray::fromStdString(compilation.generated));
 require(compilation.error.empty(),compilation.error.c_str());require(material&&GPU_material_status(material)==GPU_MAT_SUCCESS,"Native GPU material channel compilation failed");auto *shader=GPU_material_get_shader(material);require(shader,"Native GPU material channel shader missing");const double compile_ms=timer.nsecsElapsed()/1e6;
 auto *image=GPU_texture_create_2d("Native channel prototype",8,8,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr);require(image,"Native GPU material target missing");std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)> release_image(image,GPU_texture_free);
 const float parameter_value[4]{1,1,1,1};auto *parameters=GPU_storagebuf_create_ex(sizeof(parameter_value),parameter_value,GPU_USAGE_DYNAMIC,"mPainter native material parameters");std::unique_ptr<gpu::StorageBuf,decltype(&GPU_storagebuf_free)> release_parameters(parameters,GPU_storagebuf_free);GPU_storagebuf_bind(parameters,0);
 GPU_shader_bind(shader);if(auto *ubo=GPU_material_uniform_buffer_get(material))GPU_uniformbuf_bind(ubo,GPU_shader_get_ubo_binding(shader,"node_tree"));GPU_texture_image_bind(image,0);GPU_shader_uniform_1i(shader,"selected_aov",int(BLI_hash_string("BTS_Color")));GPU_shader_uniform_1i(shader,"scalar_aov",0);GPU_compute_dispatch(shader,1,1,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
 auto *pixels=static_cast<float *>(GPU_texture_read(image,GPU_DATA_FLOAT,0));require(pixels,"Native material diagnostic readback failed");float maximum=0;for(int i=0;i<64;++i)for(int c=0;c<4;++c)maximum=std::max(maximum,std::abs(pixels[i*4+c]-expected[c]));MEM_delete(pixels);
 const auto compilations=GPU_pass_global_compilation_count();GPU_shader_uniform_1i(shader,"selected_aov",int(BLI_hash_string("BTS_Scalar")));GPU_shader_uniform_1i(shader,"scalar_aov",1);
 float base=0;int reads=1;double update_ms=0;
 for(float value:{1.f,-.75f,.25f,1.5f,4.f}){float data[4]{value,value,value,1};timer.restart();GPU_storagebuf_update(parameters,data);GPU_compute_dispatch(shader,1,1,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);update_ms=std::max(update_ms,timer.nsecsElapsed()/1e6);auto *samples=static_cast<float *>(GPU_texture_read(image,GPU_DATA_FLOAT,0));require(samples,"Native procedural diagnostic read failed");++reads;if(value==1){base=samples[0];require(base>0&&base<1,"Native noise did not execute");}for(int i=0;i<64;++i)maximum=std::max(maximum,std::abs(samples[i*4]-base*value));MEM_delete(samples);}
 require(GPU_pass_global_compilation_count()==compilations,"Native parameter update recompiled a shader");GPU_storagebuf_unbind(parameters);GPU_texture_image_unbind(image);GPU_uniformbuf_unbind(GPU_material_uniform_buffer_get(material));GPU_shader_unbind();require(maximum<1e-6,"Native material constant-channel oracle mismatch");
 int group_reads=0;QJsonArray real_groups;const auto manifest=qEnvironmentVariable("BTS_NATIVE_MATERIAL_CASES");
 if(!manifest.isEmpty()){
  QFile file(manifest);require(file.open(QIODevice::ReadOnly),"Native GPU cases manifest missing");const auto cases=QJsonDocument::fromJson(file.readAll()).array();
  for(const auto &entry:cases){const auto test=entry.toObject();const auto colorspace=reference.colorspace;timer.restart();
   auto graph=std::async(std::launch::async,[test,colorspace]{std::unique_ptr<Main,decltype(&BKE_main_free)> worker(BKE_main_new(),BKE_main_free);worker->colorspace=colorspace;std::atomic_bool cancel=false;
    if(test["synthetic_displacement"].isObject()){
     const auto values=test["synthetic_displacement"].toObject();auto *mat=BKE_material_add(worker.get(),test["material"].toString().toUtf8().constData());auto &tree=*mat->nodetree;auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*out,socket(*out,"Surface",true));auto *displacement=bke::node_add_node(nullptr,tree,UString("ShaderNodeDisplacement"));displacement->custom1=values["space"]=="world"?SHD_SPACE_WORLD:SHD_SPACE_OBJECT;
     for(const auto &setting:{std::pair{"Height","height"},std::pair{"Midlevel","midlevel"},std::pair{"Scale","scale"}})static_cast<bNodeSocketValueFloat *>(socket(*displacement,setting.first,true).default_value)->value=float(values[setting.second].toDouble());
     if(values["custom_normal"].toBool()){auto *normal=bke::node_add_node(nullptr,tree,UString("ShaderNodeCombineXYZ"));static_cast<bNodeSocketValueFloat *>(socket(*normal,"Z",true).default_value)->value=1;bke::node_add_link(tree,*normal,socket(*normal,"Vector",false),*displacement,socket(*displacement,"Normal",true));}
     bke::node_add_link(tree,*displacement,socket(*displacement,"Displacement",false),*out,socket(*out,"Displacement",true));BKE_ntree_update_after_single_tree_change(*worker,tree);NativeMaterialDescription description;description.material=test["material"].toString();description.surface_type="ShaderNodeBsdfPrincipled";return std::make_shared<NativeMaterialGraph>(std::move(worker),mat,std::move(description),cancel);
    }
    return prepare_native_material_graph(*worker,test["file"].toString(),test["material"].toString(),cancel);}).get();
   QJsonObject archive;
   if(qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_ARCHIVE_TEST")&&!test["file"].toString().isEmpty()){
    const auto roundtrip=std::async(std::launch::async,[graph,test,colorspace,output]{std::atomic_bool cancel=false;auto packed=archive_native_material(*graph,test["file"].toString(),cancel,qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_EXTERNAL_IMAGES"));QString safe=graph->description.material;safe.replace('/','_').replace('\\','_');const auto target=output+"/archived-"+safe+".blend";QSaveFile file(target);if(!file.open(QIODevice::WriteOnly)||file.write(packed.bytes)!=packed.bytes.size()||!file.commit())throw std::runtime_error("Native archive fixture cannot be saved");std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);reference->colorspace=colorspace;auto asset=native_material_asset("Renamed Shelf item",packed.bytes,packed.description);auto restored_asset=asset_metadata_read(asset_metadata(asset));restored_asset.bytes=asset.bytes;if(restored_asset.bytes.constData()!=asset.bytes.constData())throw std::runtime_error("Native resource metadata duplicated archive bytes");auto resources=packed.textures;if(qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_EXTERNAL_IMAGES")){auto project=verify_native_bundle_project_resources(restored_asset,resources,packed.diagnostics);const auto found=std::find_if(project.begin(),project.end(),[&](const auto &entry){return entry.id==restored_asset.id;});if(found==project.end())throw std::runtime_error("Native node bundle missing from packed resources");restored_asset=*found;resources=std::move(project);}auto reopened=hydrate_native_material_resource(*reference,restored_asset,cancel,resources);packed.diagnostics["resource_metadata_roundtrip"]=true;packed.diagnostics["renamed_asset_source_preserved"]=reopened->description.material==graph->description.material;auto original=native_material_description_json(graph->description),restored=native_material_description_json(reopened->description);original.remove("warnings");restored.remove("warnings");if(original!=restored||packed.diagnostics["materials"]!=1)throw std::runtime_error("Native source archive changed its input interface or included runtime materials");packed.diagnostics["input_interface_exact"]=true;return std::pair{std::move(reopened),packed.diagnostics};}).get();graph=roundtrip.first;archive=roundtrip.second;
   }
   const double prepare_ms=timer.nsecsElapsed()/1e6;bool worker_guard=false;if(qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_DECODE_IMAGES")){try{std::atomic_bool cancel=false;prepare_native_material_images(*graph,cancel);}catch(const std::logic_error &){worker_guard=true;}require(worker_guard,"Native image decoding accepted the UI thread");}QJsonArray images;if(qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_DECODE_IMAGES"))images=std::async(std::launch::async,[graph]{std::atomic_bool cancel=false;return prepare_native_material_images(*graph,cancel);}).get();QJsonObject upload_proof;if(qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_DECODE_IMAGES")){NativeMaterialImageUpload upload(graph);int steps=0;while(!upload.step()){if(++steps>10000)throw std::runtime_error("Native source upload did not finish");}const auto state=upload.progress();upload_proof={{"images",qint64(state.total)},{"steps",steps},{"maximum_step_ms",state.maximum_step_ms},{"transfer_budget_upper_bound_bytes",qint64(state.transfer_budget_bytes)}};}QJsonArray channels,unsupported_channels;std::map<Image *,gpu::Texture *> shared_images;int image_bindings=0;
   for(const auto &channel:graph->channels){if(channel.channel>2&&channel.channel!=14)continue;if(!channel.tree){unsupported_channels.append(QJsonObject{{"channel",channel.channel},{"error",channel.error}});continue;}require(channel.tree,"Native group channel was not prepared");Compilation native{channel.tree,{},graph->parameters.size(),true};native.displacement_scale=test["object_scale"].toDouble(1);timer.restart();auto *program=compile_native_material(native);std::unique_ptr<GPUMaterial,decltype(&GPU_material_free_single)> release_program(program,GPU_material_free_single);QFile generated(output+"/group-"+graph->description.material+"-"+QString::number(channel.channel)+".glsl");if(generated.open(QIODevice::WriteOnly))generated.write(QByteArray::fromStdString(native.generated));std::printf("BTS_NATIVE_GROUP_COMPILE material=%s channel=%d success=%d\n",qPrintable(graph->description.material),channel.channel,int(program&&GPU_material_status(program)==GPU_MAT_SUCCESS));std::fflush(stdout);require(native.error.empty(),native.error.c_str());require(program&&GPU_material_status(program)==GPU_MAT_SUCCESS,"Native group GPU shader compilation failed");auto *native_shader=GPU_material_get_shader(program);const double native_compile_ms=timer.nsecsElapsed()/1e6;
    GPU_shader_bind(native_shader);if(auto *uniform=GPU_material_uniform_buffer_get(program))GPU_uniformbuf_bind(uniform,GPU_shader_get_ubo_binding(native_shader,"node_tree"));
    const std::array<float,4> empty{0,0,0,1};const auto count=graph->parameters.size();const auto row_count=std::max<std::uint64_t>(1,native_mapping_parameter_rows(count,native.mapping_required));const auto bytes=resource_product(row_count,sizeof(empty));if(bytes>GPU_max_storage_buffer_size())throw std::length_error("Native group diagnostic parameters exceed driver SSBO storage");std::vector<std::array<float,4>> values(row_count);std::copy(graph->parameters.begin(),graph->parameters.end(),values.begin());if(!count)values[0]=empty;if(native.mapping_required)for(std::size_t i=0;i<count;++i){const auto rows=NativeMaterialMapping{}.rows();std::copy(rows.begin(),rows.end(),values.begin()+count+3*i);}auto *buffer=GPU_storagebuf_create_ex(bytes,values.data(),GPU_USAGE_DYNAMIC,"Native group controls");std::unique_ptr<gpu::StorageBuf,decltype(&GPU_storagebuf_free)> release_buffer(buffer,GPU_storagebuf_free);GPU_storagebuf_bind(buffer,0);
    int resources=0;for(auto &texture:GPU_material_textures(program)){
     gpu::Texture *resource=nullptr;if(texture.colorband)resource=*texture.colorband;else if(texture.ima){const auto images=BKE_image_get_gpu_material_texture(texture.ima,texture.iuser_available?&texture.iuser:nullptr,texture.tiled_mapping_name[0]!=0);resource=images.texture?*images.texture:nullptr;if(texture.tiled_mapping_name[0]){require(images.tile_mapping&&*images.tile_mapping,"Native UDIM mapping missing");GPU_texture_bind_ex(*images.tile_mapping,texture.sampler_state,GPU_shader_get_sampler_binding(native_shader,texture.tiled_mapping_name));}}
     if(texture.ima){auto [found,fresh]=shared_images.try_emplace(texture.ima,resource);require(fresh||found->second==resource,"Native channel programs duplicated an image GPU allocation");++image_bindings;}
     require(resource,"Native group image/ramp source missing");if(texture.colorband)GPU_texture_bind(resource,GPU_shader_get_sampler_binding(native_shader,texture.sampler_name));else GPU_texture_bind_ex(resource,texture.sampler_state,GPU_shader_get_sampler_binding(native_shader,texture.sampler_name));++resources;
    }
    GPU_texture_image_bind(image,0);GPU_shader_uniform_1i(native_shader,"selected_aov",int(BLI_hash_string("BTS_Channel")));GPU_shader_uniform_1i(native_shader,"scalar_aov",channel.channel!=0);if(GPU_shader_get_uniform(native_shader,"fixture_displacement_scale")>=0)GPU_shader_uniform_1f(native_shader,"fixture_displacement_scale",float(native.displacement_scale));GPU_compute_dispatch(native_shader,1,1,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);auto *sample=static_cast<float *>(GPU_texture_read(image,GPU_DATA_FLOAT,0));require(sample,"Native group diagnostic readback missing");++group_reads;const auto free_sample=qScopeGuard([&]{MEM_delete(sample);});QJsonArray rgba;for(int c=0;c<4;++c){require(std::isfinite(sample[c]),"Native group generated non-finite pixels");rgba.append(sample[c]);}for(int i=0;i<64;++i)for(int c=0;c<4;++c)require(std::isfinite(sample[i*4+c])&&std::abs(sample[i*4+c]-sample[c])<1e-6,"Fixed-coordinate native group changed across texels");require(std::abs(sample[3]-1)<1e-6,"Native group channel coverage is not one");
    if(channel.channel==14&&test["synthetic_displacement"].isObject()){const auto v=test["synthetic_displacement"].toObject();const float expected=(float(v["height"].toDouble())-float(v["midlevel"].toDouble()))*float(v["scale"].toDouble())*(v["space"]=="world"?1.f:float(native.displacement_scale));std::printf("BTS_NATIVE_DISPLACEMENT_ORACLE name=%s actual=%.9g expected=%.9g\n",qPrintable(graph->description.material),sample[0],expected);require(std::abs(sample[0]-expected)<1e-6f*std::max(1.f,std::abs(expected)),"Native displacement channel differs from the node equation");}
    if(channel.channel==0)require(sample[0]+sample[1]+sample[2]>.01,"Native group color is missing");
    GPU_texture_image_unbind(image);GPU_texture_unbind_all();GPU_storagebuf_unbind(buffer);if(auto *uniform=GPU_material_uniform_buffer_get(program))GPU_uniformbuf_unbind(uniform);GPU_shader_unbind();channels.append(QJsonObject{{"channel",channel.channel},{"rgba",rgba},{"compile_ms",native_compile_ms},{"native_texture_resources",resources}});
   }
   QJsonObject mapping_source;if(test["linked_mapping"].toBool())mapping_source=verify_native_material_mapping_source(graph,image);
   real_groups.append(QJsonObject{{"linked_mapping",mapping_source},{"material",graph->description.material},{"archive",archive},{"image_upload",upload_proof},{"worker_image_guard",worker_guard},{"shared_image_gpu_allocations",qint64(shared_images.size())},{"image_bindings",image_bindings},{"images",images},{"prepare_ms",prepare_ms},{"channels",channels},{"unsupported_channels",unsupported_channels},{"source_inputs_preserved",graph->original_inputs_preserved},{"synthetic_displacement",test["synthetic_displacement"]},{"object_scale",test["object_scale"].toDouble(1)},{"context","fixed coordinate fixture; not artist geometry"}});
  }
 }

#include "NativeMaterialGpuCacheTests.inc"
 auto *unsupported=bke::node_tree_add_tree(main,"mPainter unsupported diagnostic","ShaderNodeTree");unsupported->flag|=NTREE_IS_GPU_SHADER_INTERNAL;auto *unknown=bke::node_add_node(nullptr,*unsupported,UString("ShaderNodeAttribute"));STRNCPY(static_cast<NodeShaderAttribute *>(unknown->storage)->name,"BTS_Forbidden");auto *unknown_aov=bke::node_add_node(nullptr,*unsupported,UString("ShaderNodeOutputAOV"));bke::node_add_link(*unsupported,*unknown,socket(*unknown,"Color",false),*unknown_aov,socket(*unknown_aov,"Color",true));bke::node_add_node(nullptr,*unsupported,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(*main,*unsupported);Compilation rejected{unsupported};auto *rejected_program=compile_native_material(rejected);require(rejected_program&&!rejected.error.empty(),"Unsupported native attribute was not rejected");GPU_material_free_single(rejected_program);
 const auto surface_proof=verify_native_material_gpu_program();
 const auto stack_proof=verify_native_material_stack();
 QFile proof(output+"/native-material-gpu.json");require(proof.open(QIODevice::WriteOnly),"Native material GPU proof cannot be written");proof.write(QJsonDocument(QJsonObject{{"native_stack",stack_proof},{"surface_program",surface_proof},{"stage","native-codegen-procedural-parameter-prototype"},{"native_nodes",true},{"compile_ms",compile_ms},{"parameter_update_ms",update_ms},{"shader_recompiles",0},{"native_noise",base},{"signed_parameter",true},{"maximum_error",maximum},{"diagnostic_readbacks",reads+group_reads+binding_cases.size()},{"synthetic_readbacks",reads},{"binding_cases",binding_cases},{"interactive_integration",false},{"real_groups",real_groups},{"unsupported_rejected_without_callback_exception",true}}).toJson());std::printf("BTS_NATIVE_MATERIAL_GPU constant=pass max_error=%.9g compile_ms=%.3f\n",maximum,compile_ms);std::fflush(stdout);
}
}
