#pragma once
#include "GpuAdjustmentPaintTest.hh"
#include "ImportedShaderCache.hh"
#include "NativeMaterialImageResource.hh"
#include "GPU_host_texture_memory.hh"
#include <epoxy/gl.h>
#include <QFile>
#include <QJsonDocument>
#include <cstring>
#include <bit>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
namespace bts::unload_test {
using namespace blender;
using gpu_adjustment_test::require;
inline QByteArray pixels(gpu::Texture *image){auto *data=GPU_texture_read(image,GPU_DATA_FLOAT,0);require(data,"Unload probe read failed");QByteArray result(static_cast<const char *>(data),GPU_texture_width(image)*GPU_texture_height(image)*16);MEM_delete_void(data);return result;}
inline QJsonObject memory(){GPU_finish();
#if defined(__GLIBC__)
 malloc_trim(0);
#endif
 QFile file("/proc/self/status");qint64 rss=0;if(file.open(QIODevice::ReadOnly))for(const auto &line:file.readAll().split('\n'))if(line.startsWith("VmRSS:"))rss=line.simplified().split(' ')[1].toLongLong()*1024;
 GLint total=0,available=0;if(epoxy_has_gl_extension("GL_NVX_gpu_memory_info")){glGetIntegerv(0x9048,&total);glGetIntegerv(0x9049,&available);}
 return {{"driver_device_used_bytes",qint64(total-available)*1024},{"driver_device_total_bytes",qint64(total)*1024},{"process_rss_bytes",rss},{"gpu_texture_bytes",qint64(gpu::host_texture_memory::live_bytes())}};
}
inline QByteArray topology(bNodeTree &tree){
 QByteArray result;const auto raw=[&](const auto &value){result.append(reinterpret_cast<const char *>(&value),sizeof(value));};
 for(auto &n:tree.nodes){result.append(n.name);result.append(n.idname);raw(n.identifier);raw(n.custom1);raw(n.custom2);raw(n.custom3);raw(n.custom4);if(n.id)result.append(n.id->name);for(auto &s:n.inputs){result.append(s.identifier);raw(s.type);if(s.default_value){if(s.type==SOCK_FLOAT)raw(*static_cast<bNodeSocketValueFloat *>(s.default_value));else if(s.type==SOCK_RGBA)raw(*static_cast<bNodeSocketValueRGBA *>(s.default_value));else if(s.type==SOCK_VECTOR)raw(*static_cast<bNodeSocketValueVector *>(s.default_value));}}}
 for(auto &e:tree.links){raw(e.fromnode->identifier);result.append(e.fromsock->identifier);raw(e.tonode->identifier);result.append(e.tosock->identifier);raw(e.flag);}return result;
}
inline QJsonObject run(){
 using namespace gpu_adjustment_test;QJsonObject proof;
 // Raw image storage preserves signed zero, HDR, subnormal, and NaN payload bits.
 {auto *buffer=IMB_allocImBuf(32,32,ImBufFlags::Zero);require(buffer&&IMB_alloc_float_pixels(buffer,4)&&IMB_alloc_byte_pixels(buffer),"Native image fixture failed");std::vector<std::uint32_t> expected(32*32*4);for(std::size_t i=0;i<expected.size();++i)expected[i]=std::array<std::uint32_t,8>{0,0x80000000,0x3f800000,0xbf800000,0x40800000,1,0x7fc01234,0x3eaaaaab}[i%8];std::memcpy(buffer->float_data_for_write(),expected.data(),expected.size()*4);std::memset(buffer->byte_data_for_write(),73,expected.size());NativeMaterialImageResource resource(buffer,"unload-raw");resource.spill();require(!buffer->float_data()&&!buffer->byte_data(),"Native pixels stayed resident");require(std::memcmp(resource.buffer()->float_data(),expected.data(),expected.size()*4)==0,"Native float bits changed");for(std::size_t i=0;i<expected.size();++i)require(resource.buffer()->byte_data()[i]==73,"Native byte pixels changed");proof["native_pixel_bits"]=true;}
 // An unsupported/non-editable imported emission network plus an image. Its
 // native IDs and arbitrary socket values survive whole-set and tile toggles.
 {std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);auto *m=BKE_material_add(main.get(),"Imported emission");auto &tree=*m->nodetree;auto *emission=bke::node_add_node(nullptr,tree,UString("ShaderNodeEmission"));auto *output=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));bke::node_add_link(tree,*emission,*static_cast<bNodeSocket *>(emission->outputs.first),*output,*static_cast<bNodeSocket *>(output->inputs.first));auto *strength=bke::node_find_enabled_input_socket(*emission,"Strength");static_cast<bNodeSocketValueFloat *>(strength->default_value)->value=3.125f;
 const float color[4]={-.25f,2.f,.125f,1.f};auto *image=BKE_image_add_generated(main.get(),32,32,"Imported pixels",32,1,IMA_GENTYPE_BLANK,color,false,true,false);auto *texture=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexImage"));texture->id=&image->id;bke::node_add_link(tree,*texture,*static_cast<bNodeSocket *>(texture->outputs.first),*emission,*bke::node_find_enabled_input_socket(*emission,"Color"));ImageUser user{};auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);require(buffer&&buffer->float_data(),"Imported image fixture missing");buffer->float_data_for_write()[17]=std::bit_cast<float>(std::uint32_t(0x3eaaaaab));const QByteArray image_before(reinterpret_cast<const char *>(buffer->float_data()),32*32*16);BKE_image_release_ibuf(image,buffer,nullptr);
 auto *group=bke::node_tree_add_tree(main.get(),"Private group","ShaderNodeTree");auto *math=bke::node_add_node(nullptr,*group,UString("ShaderNodeMath"));math->custom1=NODE_MATH_MULTIPLY;auto *group_node=bke::node_add_node(nullptr,tree,UString("ShaderNodeGroup"));group_node->id=&group->id;const auto group_before=topology(*group);
 BKE_ntree_update_after_single_tree_change(*main,tree);const auto original=topology(tree);ImportedShaderCache cache;TextureSet set;set.uniform_editable=false;set.loaded=false;
 const auto recovery=qgetenv("BTS_RECOVERY_DIR");QTemporaryFile blocked;require(blocked.open(),"Cache failure fixture unavailable");qputenv("BTS_RECOVERY_DIR",blocked.fileName().toUtf8());bool rejected=false;try{cache.change(*main,*m,set);}catch(const std::exception &){rejected=true;}qputenv("BTS_RECOVERY_DIR",recovery);require(rejected&&topology(tree)==original&&buffer->float_data(),"Shader cache write failure lost source data");proof["failed_write_preserves_source"]=true;
 cache.change(*main,*m,set);require(cache.cold(),"Imported network was not cached");require(group->nodes.is_empty(),"Private imported group stayed resident");require(!buffer->float_data(),"Imported pixels stayed resident");set.loaded=true;cache.change(*main,*m,set);require(topology(tree)==original,"Imported shader network changed");require(topology(*group)==group_before,"Imported group changed");require(QByteArray(reinterpret_cast<const char *>(buffer->float_data()),32*32*16)==image_before,"Imported pixels changed");set.tiles={1001,1002};set.unloaded_tiles={1002};cache.change(*main,*m,set);set.unloaded_tiles.clear();cache.change(*main,*m,set);require(topology(tree)==original,"Imported tile visibility changed original network");proof["imported_network_bits"]=true;}
 Renderer renderer;std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);auto *material=BKE_material_add(main.get(),"Frozen native prefix");auto &tree=*material->nodetree;auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *out=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));bke::node_add_link(tree,*pbr,*static_cast<bNodeSocket *>(pbr->outputs.first),*out,*bke::node_find_enabled_input_socket(*out,"Surface"));BKE_ntree_update_after_single_tree_change(*main,tree);
 NativeMaterialDescription description;description.material="Frozen native prefix";description.surface_type="ShaderNodeBsdfPrincipled";NativeMaterialParameter rough;rough.identifier="Roughness";rough.name="Roughness";rough.value[0]=.25;description.parameters.push_back(rough);std::atomic_bool cancel=false;auto graph=std::make_shared<NativeMaterialGraph>(std::move(main),material,description,cancel);
 TextureSet set;set.texture_resolution=128;set.tiles={1001,1002};set.precision[2]=ChannelPrecision::L32F;set.layers.clear();TextureLayer lower,layer;lower.kind=LayerKind::Fill;lower.fill.material_resource=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();lower.fill.native_material=std::make_shared<const NativeMaterialDescription>(description);layer.blending[2]=LayerBlend::Passthrough;layer.opacity[2]=.5;layer.pixels.assign_channel(128,2,gpu_adjustment_test::pixels(true,.75,0));set.layers={lower,layer};set.selected_layer=layer.id;renderer.preview.install_native_material(lower.fill.material_resource,"unload-native-prefix",graph);
 std::vector<TextureSet> history{set};std::vector<QByteArray> images{pixels(renderer.render(set,2))};
 for(int gesture=0;gesture<2;++gesture){auto token=renderer.preview.begin_adjustment_source(set,layer.id);auto &target=set.layers.back();{PaintRecordingScope recording(true);CloneStroke stroke(target.pixels,128,stamp(.25,.5),false,adjustment_main_source(set,target,token));target.pixels.stamp(128,stroke.dab(stamp(gesture?.65:.75,.5)),set.tiles);}history.push_back(set);images.push_back(pixels(renderer.render(set,2)));}
 const auto native_before=topology(*graph->material->nodetree);proof["before"]=memory();
 renderer.preview.evict_residency();proof["unloaded"]=memory();require(graph->material->nodetree->nodes.is_empty(),"Native library node payload stayed resident");
 require(pixels(renderer.render(set,2))==images.back(),"Reload changed final adjustment pixels");proof["reloaded"]=memory();
 // Replaying undo then redo across every frozen adjustment snapshot checks all
 // output pixels, including untouched texels, rather than selected samples.
 for(std::size_t i=history.size();i-->0;)require(pixels(renderer.render(history[i],2))==images[i],"Unload changed undo or adjustment pixels");
 for(std::size_t i=0;i<history.size();++i)require(pixels(renderer.render(history[i],2))==images[i],"Unload changed redo pixels");
 require(topology(*graph->material->nodetree)==native_before,"Native library source graph changed");
 // Active aliases protect a resource shared with another loaded set.
 renderer.preview.evict_residency({set});require(!graph->material->nodetree->nodes.is_empty(),"Active shared source was evicted");
 proof["undo_states"]=qint64(history.size());proof["adjustment_roundtrip_bit_exact"]=true;proof["shared_source_protected"]=true;proof["resolution"]=128;proof["passed"]=true;
 renderer.preview.reset(true);GpuLayerPreview::collect_retired(true);return proof;
}
}
