#include <QtCore>
#include "MaterialLayeringSourcesTest.hh"
#include "MaterialLayeringSources.hh"
#include "MaterialLayeringSourceSnapshots.hh"
#include "MaterialLayerParameterIO.hh"
#include "MaterialLayerParameterImportJob.hh"
#include "MaterialLayerParameterCatalog.hh"
#include <QElapsedTimer>
#include <QThread>
#include "MaterialLayerParameterDocumentTest.hh"
#include "MaterialLayerParameterSurface.hh"
#include "NewProjectProfile.hh"
#include "MaterialLayeringIO.hh"
#include "MaterialLayeringSourceColorProcessor.hh"
#include <cstring>
#include "GpuTextureResource.hh"
#include "MaterialLayerShader.hh"
#include "MaterialLayeringStacks.hh"
#include "ResourceIO.hh"
#include "FillTextureJSON.hh"
#include "MaterialInspection.hh"
#include "FillTextureIO.hh"
#include "MaterialLayerUdim.hh"
#include "ImageSequence.hh"
#include "BKE_idprop.hh"
#include "BKE_material.hh"
#include "BKE_scene.hh"
#include "BKE_layer.hh"
#include "BKE_node.hh"
#include "BKE_node_runtime.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_report.hh"
#include "BLO_readfile.hh"
#include "BLO_writefile.hh"
#include "BLI_string.h"
#include "BLI_listbase.h"
#include "BLI_ustring.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "MEM_guardedalloc.h"
#include "NativeMaterialSampler.hh"
#include "GPU_context.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include "BKE_image_hosted.hh"
#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_blendfile.hh"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <future>
#ifdef BTS_MATERIAL_LAYER_SOURCE_LOADER_TEST
#include "MaterialLayeringCyclesSourceProbe.hh"
#endif
// Blender runtime containers must use the library's NDEBUG layout. Keep this
// fixture's checks active locally, after every native header has been parsed.
#undef assert
#define assert(expression) do { if(!(expression)){std::fprintf(stderr,"Source pixels assertion failed: %s (%s:%d)\n",#expression,__FILE__,__LINE__);std::abort();} } while(false)
#include "MaterialLayerEeveeObservationPredicateTest.hh"
#include "MaterialLayerSurfaceGraphPredicateTest.hh"
namespace bts {
namespace {
int verify_native_material_sampler_states(){
 using namespace blender;int cases=0;const auto policy16=GPU_anisotropic_filtering_flags(16);
 for(auto wrap_x:{GPU_SAMPLER_EXTEND_MODE_EXTEND,GPU_SAMPLER_EXTEND_MODE_REPEAT,GPU_SAMPLER_EXTEND_MODE_MIRRORED_REPEAT,GPU_SAMPLER_EXTEND_MODE_CLAMP_TO_BORDER})for(auto wrap_y:{GPU_SAMPLER_EXTEND_MODE_EXTEND,GPU_SAMPLER_EXTEND_MODE_REPEAT,GPU_SAMPLER_EXTEND_MODE_MIRRORED_REPEAT,GPU_SAMPLER_EXTEND_MODE_CLAMP_TO_BORDER}){
  auto request=GPUSamplerState::default_sampler();request.extend_x=wrap_x;request.extend_yz=wrap_y;request.filtering=GPU_SAMPLER_FILTERING_ANISOTROPIC_ENABLE|GPU_SAMPLER_FILTERING_LINEAR|GPU_SAMPLER_FILTERING_MIPMAP;
  assert(int(request.filtering)==31&&int(request.filtering)>=GPU_SAMPLER_FILTERING_TYPES_COUNT);
  for(bool suspended:{false,true}){const auto state=native_material_image_sampler(request,policy16,suspended);assert(state.type==GPU_SAMPLER_STATE_TYPE_PARAMETERS&&state.extend_x==wrap_x&&state.extend_yz==wrap_y);assert(int(state.filtering)==(suspended?21:23)&&int(state.filtering)<GPU_SAMPLER_FILTERING_TYPES_COUNT);assert(bool(state.filtering&GPU_SAMPLER_FILTERING_LINEAR)&&bool(state.filtering&GPU_SAMPLER_FILTERING_MIPMAP)==!suspended);++cases;}
 }
 // Follow the caller's concrete policy, including anisotropy disabled.
 for(int samples:{1,2,4,8,16}){auto request=GPUSamplerState::default_sampler();request.extend_x=GPU_SAMPLER_EXTEND_MODE_REPEAT;request.filtering=GPU_SAMPLER_FILTERING_ANISOTROPIC_ENABLE|GPU_SAMPLER_FILTERING_LINEAR|GPU_SAMPLER_FILTERING_MIPMAP;const auto policy=GPU_anisotropic_filtering_flags(samples);const auto state=native_material_image_sampler(request,policy,false);assert(state.filtering==(policy|GPU_SAMPLER_FILTERING_LINEAR|GPU_SAMPLER_FILTERING_MIPMAP)&&int(state.filtering)<GPU_SAMPLER_FILTERING_TYPES_COUNT&&state.extend_x==request.extend_x);++cases;}
 auto nearest=GPUSamplerState::default_sampler();nearest.extend_x=GPU_SAMPLER_EXTEND_MODE_REPEAT;assert(native_material_image_sampler(nearest,policy16,false)==nearest&&native_material_image_sampler(nearest,policy16,true)==nearest);++cases;
 auto linear=nearest;linear.filtering=GPU_SAMPLER_FILTERING_LINEAR;assert(native_material_image_sampler(linear,policy16,false)==linear&&native_material_image_sampler(linear,policy16,true)==linear);++cases;
 auto concrete=nearest;concrete.filtering=GPU_SAMPLER_FILTERING_ANISOTROPIC_8|GPU_SAMPLER_FILTERING_MIPMAP;const auto resolved=native_material_image_sampler(concrete,policy16,false);assert(resolved.filtering==(GPU_SAMPLER_FILTERING_ANISOTROPIC_16|GPU_SAMPLER_FILTERING_MIPMAP)&&resolved.extend_x==concrete.extend_x);++cases;
 auto custom=nearest;custom.type=GPU_SAMPLER_STATE_TYPE_CUSTOM;custom.custom_type=GPU_SAMPLER_CUSTOM_ICON;custom.filtering=GPU_SAMPLER_FILTERING_ANISOTROPIC_ENABLE;assert(native_material_image_sampler(custom,policy16,false)==custom&&native_material_image_sampler(custom,policy16,true)==custom);++cases;
 const auto internal=GPUSamplerState::internal_sampler();assert(native_material_image_sampler(internal,policy16,false)==internal&&native_material_image_sampler(internal,policy16,true)==internal);++cases;
 return cases;
}
void describe_source(blender::Image &image,int w,int h){
 image.source=blender::IMA_SRC_GENERATED;image.type=blender::IMA_TYPE_UV_TEST;image.alpha_mode=blender::IMA_ALPHA_CHANNEL_PACKED;
 auto *tile=static_cast<blender::ImageTile *>(image.tiles.first);tile->gen_x=w;tile->gen_y=h;
 image.runtime->gpuflag|=blender::IMA_HOSTED_IMMUTABLE_SOURCE;
}
int verify_partial_write_embedded_cache_ownership(){
 using namespace blender;
 std::unique_ptr<Main,decltype(&BKE_main_free)> live(BKE_main_new(),BKE_main_free);
 auto *first=BKE_material_add(live.get(),"Embedded self-cache A"),*second=BKE_material_add(live.get(),"Embedded self-cache B");
 auto *node=bke::node_add_node(nullptr,*first->nodetree,UString("ShaderNodeValue"));
 const std::string stable_name=node->name;const int stable_identifier=node->identifier;
 const auto dump=[](const char *phase,const bNodeTree &tree){std::printf("BTS_EMBEDDED_DIAG phase=%s tree=%p runtime=%p listed=%d indexed=%lld\n",phase,static_cast<const void *>(&tree),static_cast<const void *>(tree.runtime),BLI_listbase_count(&tree.nodes),static_cast<long long>(tree.runtime->nodes_by_id.size()));for(const auto &item:tree.nodes)std::printf("BTS_EMBEDDED_NODE phase=%s node=%p identifier=%d name=%s runtime=%p owner=%p\n",phase,static_cast<const void *>(&item),item.identifier,item.name,static_cast<const void *>(item.runtime),static_cast<const void *>(item.runtime?item.runtime->owner_tree:nullptr));std::fflush(stdout);};
 std::printf("BTS_EMBEDDED_ORIGINAL identifier=%d name=%s node=%p\n",stable_identifier,stable_name.c_str(),static_cast<void *>(node));dump("original-before",*first->nodetree);dump("other-before",*second->nodetree);

 auto *foreign=reinterpret_cast<Image *>(BKE_id_new(live.get(),ID_IM,"Foreign logical dependency"));
 bke::blendfile::PartialWriteContext snapshot(*live);using Context=bke::blendfile::PartialWriteContext;
 auto *copy=reinterpret_cast<Material *>(snapshot.id_add(&first->id,{Context::ADD_DEPENDENCIES}));dump("copy-after-first-add",*copy->nodetree);dump("original-after-first-add",*first->nodetree);
 auto *other=reinterpret_cast<Material *>(snapshot.id_add(&second->id,{Context::ADD_DEPENDENCIES}));dump("copy-after-second-add",*copy->nodetree);dump("other-after-add",*other->nodetree);dump("original-after-second-add",*first->nodetree);
 BKE_ntree_update_tag_all(copy->nodetree);BKE_ntree_update_tag_all(other->nodetree);
 copy->nodetree->ensure_topology_cache();other->nodetree->ensure_topology_cache();
 dump("copy-after-refresh",*copy->nodetree);dump("other-after-refresh",*other->nodetree);
 auto *cached=bke::node_find_node_by_name(*copy->nodetree,node->name);std::printf("BTS_EMBEDDED_LOOKUP original_name=%s stable_name=%s original_identifier=%d stable_identifier=%d cached=%p cached_owner=%p expected_tree=%p\n",node->name,stable_name.c_str(),node->identifier,stable_identifier,static_cast<void *>(cached),static_cast<void *>(cached?cached->runtime->owner_tree:nullptr),static_cast<void *>(copy->nodetree));std::fflush(stdout);assert(cached&&cached->runtime->owner_tree==copy->nodetree);
 assert((copy->nodetree->id.flag&ID_FLAG_EMBEDDED_DATA)&&BKE_id_owner_get(&copy->nodetree->id,false)==&copy->id);
 assert(snapshot.is_valid()); // Real refreshed, genuinely-owned embedded self-cache.
 cached->runtime->owner_tree=first->nodetree;assert(!snapshot.is_valid()); // Live foreign tree outside the copied Main.
 cached->runtime->owner_tree=other->nodetree;assert(!snapshot.is_valid()); // Another embedded tree is not self.
 cached->runtime->owner_tree=copy->nodetree;cached->id=&foreign->id;assert(!snapshot.is_valid()); // Genuine ID edge remains rejected.
 cached->id=nullptr;assert(snapshot.is_valid()&&cached->runtime->owner_tree==copy->nodetree);
 return 4;
}
void verify_source_snapshot_worker_pack(){
 using namespace blender;using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
 std::unique_ptr<Main,decltype(&BKE_main_free)> live(BKE_main_new(),BKE_main_free);
 auto *image=reinterpret_cast<Image *>(BKE_id_new(live.get(),ID_IM,"Reusable save snapshot"));describe_source(*image,3,2);
 FillTexture source;source.asset=std::string(36,'e');source.hash=std::string(64,'f');source.name="Snapshot source";source.raw=true;source.width=3;source.height=2;
 source.pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(6,FillTexture::Pixel{-2.25f,70000.f,.75f,0});const auto expected=*source.pixels;std::weak_ptr<const std::vector<FillTexture::Pixel>> lifetime=source.pixels;
 Buffer buffer(material_layer_source_pixel_view(source),IMB_freeImBuf);const auto *original=buffer->float_data();
 assert(BKE_image_assign_hosted_immutable_source(image,1001,buffer.get()));
 bke::blendfile::PartialWriteContext snapshot(*live);using Context=bke::blendfile::PartialWriteContext;
 auto *copy=reinterpret_cast<Image *>(snapshot.id_add(&image->id,{Context::ADD_DEPENDENCIES}));assert(copy&&copy->id.session_uid==image->id.session_uid&&snapshot.is_valid());
 assert(!BKE_image_has_loaded_ibuf(copy));auto owners=retain_material_layer_source_snapshot_headers(*live,snapshot.bmain);assert(owners.size()==1);
 {Buffer transferred(BKE_image_hosted_source_snapshot(copy,1001),IMB_freeImBuf);assert(transferred&&transferred->float_data()==original);}
 Buffer retired(BKE_image_detach_hosted_source(image,1001),IMB_freeImBuf);retired.reset();buffer.reset();source.pixels.reset();assert(!lifetime.expired());
 auto worker=std::async(std::launch::async,[&]{return pack_material_layer_source_snapshots(snapshot.bmain);});assert(worker.get()==1);
 auto *part=static_cast<ImagePackedFile *>(copy->packedfiles.first);assert(part&&part->packedfile&&copy->packedfiles.first==copy->packedfiles.last);
 const auto *packed=part->packedfile;
 Buffer decoded(IMB_load_image_from_memory(static_cast<const unsigned char *>(packed->data),packed->size,ImBufFlags::AlphaChannelPacked|ImBufFlags::NoColorspaceConvert,"snapshot.exr","snapshot.exr"),IMB_freeImBuf);
 assert(decoded&&decoded->x==3&&decoded->y==2&&decoded->channels==4&&decoded->float_data());
 for(std::size_t i=0;i<expected.size();++i)for(int c=0;c<4;++c)assert(decoded->float_data()[i*4+c]==expected[i][c]);
 Buffer retire_copy(BKE_image_detach_hosted_source(copy,1001),IMB_freeImBuf);retire_copy.reset();owners.clear();assert(lifetime.expired());
}
int verify_original_backed_source_snapshot(){
 int cold_rejections=0;
 using namespace blender;using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
 std::unique_ptr<Main,decltype(&BKE_main_free)> live(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(live.get());
 auto *scene=BKE_scene_add(live.get(),"Original source persistence");
 for(auto &view_layer:scene->view_layers)BKE_view_layer_synced_ensure(*live,scene,&view_layer);
 // Encode one owned small original. It is then removed from the Main; the
 // resource packer, not this temporary encoder Image, owns the saved payload.
 auto *encoder=reinterpret_cast<Image *>(BKE_id_new(live.get(),ID_IM,"Original EXR encoder"));describe_source(*encoder,3,2);
 FillTexture encoded;encoded.asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();encoded.hash=std::string(64,'e');encoded.name="Original signed linear source";encoded.raw=true;encoded.width=3;encoded.height=2;
 encoded.pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(std::initializer_list<FillTexture::Pixel>{{-.25f,.5f,2,0},{.25f,.75f,1,.25f},{1,.125f,.5f,1},{.75f,.25f,1,0},{.125f,1,.25f,.25f},{2,.5f,.75f,1}});
 {Buffer header(material_layer_source_pixel_view(encoded),IMB_freeImBuf);assert(BKE_image_assign_hosted_immutable_source(encoder,1001,header.get()));}
 assert(BKE_image_memorypack(encoder));const auto *packed=static_cast<ImagePackedFile *>(encoder->packedfiles.first)->packedfile;
 StudioAsset original;original.id=QUuid(QString::fromStdString(encoded.asset));original.name="Original source";original.filename="linear-source.exr";original.usages={"texture"};original.scope=AssetScope::Project;original.bytes=QByteArray(static_cast<const char *>(packed->data),packed->size);BKE_id_free(live.get(),encoder);
 auto ap1_asset=original,rec709_asset=original;ap1_asset.id=QUuid::createUuid();rec709_asset.id=QUuid::createUuid();const std::vector<StudioAsset> assets{original,ap1_asset,rec709_asset};
 const auto raw=decode_fill_texture(original,true),ap1=decode_fill_texture(ap1_asset,false,"ACEScg"),rec709=decode_fill_texture(rec709_asset,false,"Linear Rec.709");assert((*raw->pixels)[0][0]==-.25f&&(*raw->pixels)[0][3]==0);
 const auto ap1_matrix=material_layer_linear_color_transform(*ap1),rec709_matrix=material_layer_linear_color_transform(*rec709);assert(!ap1_matrix.processor.empty()&&!rec709_matrix.processor.empty());
 // Independent existing decoder versus the extracted stock-node matrix: no
 // production shader helper supplies the expected converted pixel values.
 for(std::size_t i=0;i<raw->pixels->size();++i){for(int row=0;row<3;++row){double expected=0;for(int column=0;column<3;++column)expected+=double(rec709_matrix.to_aces[column*3+row])*(*raw->pixels)[i][column];assert(std::isfinite(expected)&&std::abs(expected-(*rec709->pixels)[i][row])<=3e-5);assert((*ap1->pixels)[i][row]==(*raw->pixels)[i][row]);}assert((*raw->pixels)[i][3]==(*ap1->pixels)[i][3]&&(*raw->pixels)[i][3]==(*rec709->pixels)[i][3]);}
 auto nonlinear=*ap1;nonlinear.input_space="sRGB";bool nonlinear_rejected=false;try{material_layer_linear_color_transform(nonlinear);}catch(const std::invalid_argument &){nonlinear_rejected=true;}assert(nonlinear_rejected);
 std::array<Material *,2> materials{};SceneSnapshot description;ImportSummary imported;
 const auto socket=[](bNode &node,const char *name,bool inputs)->bNodeSocket &{for(auto &s:inputs?node.inputs:node.outputs)if(STREQ(s.identifier,name))return s;throw std::runtime_error("Source snapshot fixture socket missing");};
 for(int i=0;i<2;++i){auto *material=BKE_material_add(live.get(),i?"Source owner B":"Source owner A");materials[i]=material;auto &tree=*material->nodetree;auto *surface=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled")),*output=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(*live,tree);bke::node_add_link(tree,*surface,socket(*surface,"BSDF",false),*output,socket(*output,"Surface",true));BKE_ntree_update_after_single_tree_change(*live,tree);MaterialSummary summary;summary.session_id=material->id.session_uid;summary.name=material->id.name+2;summary.uniform_editable=true;description.materials.push_back(summary);imported.materials.push_back({summary.session_id,summary.name});}
 TextureDocument document({},imported,description);
 std::map<std::string,Image *> aliases;
 for(auto *material:materials){auto &set=document.set(material->id.session_uid);configure_material_layering(set,true);auto &model=set.material_layering;model.materials[0].textures[0]=ap1;model.materials[0].textures[1]=raw;model.materials[1].textures[0]=rec709;model.materials[1].textures[2]=raw;model.materials[2].textures[4]=raw;model.materials[3].textures[0]=ap1;
  for(int slot:model.mask_slots){auto *image=reinterpret_cast<Image *>(BKE_id_new(live.get(),ID_IM,"Source fixture opacity"));describe_source(*image,1,1);image->runtime->gpuflag&=~IMA_HOSTED_IMMUTABLE_SOURCE; // Ordinary mask placeholder, not an immutable source alias.
   auto *node=bke::node_add_node(nullptr,*material->nodetree,UString("ShaderNodeTexImage"));STRNCPY(node->name,("__BTS_UserPixels"+std::to_string(slot)).c_str());node->id=&image->id;id_us_plus(node->id);}
  MaterialLayerImages images{};for(int input=0;input<4;++input)for(int channel=0;channel<6;++channel)if(const auto &source=model.materials[input].textures[channel]){const auto key=gpu_texture_resource_key(*source);auto [at,fresh]=aliases.try_emplace(key);if(fresh){auto *image=reinterpret_cast<Image *>(BKE_id_new(live.get(),ID_IM,"Typed runtime alias"));id_us_min(&image->id);describe_source(*image,source->width,source->height);Buffer header(material_layer_source_pixel_view(*source),IMB_freeImBuf);assert(BKE_image_assign_hosted_immutable_source(image,1001,header.get()));at->second=image;}images[input][channel]=at->second;}
  MaterialLayerShader shader;shader.apply(*live,*material,model,1,1,false,images);
 }
 assert(aliases.size()==3);const auto original_document=document.to_json();const auto bindings=capture_material_layer_source_bindings(*live,document);assert(bindings.size()==12);
 // Record actual live alias counts/flags, independently of the detached
 // PartialWrite counters. Packing must never recount or modify these IDs.
 const auto alias_counts=[](const std::map<std::string,Image *> &images){std::map<std::string,std::array<int,3>> result;for(const auto &[key,image]:images)result.emplace(key,std::array<int,3>{image->id.us,int(image->id.flag),image->id.tag&(ID_TAG_EXTRAUSER|ID_TAG_EXTRAUSER_SET)});return result;};
 const auto live_alias_counts=alias_counts(aliases);
 const auto verify_carrier_counts=[](bke::blendfile::PartialWriteContext &context,const ProjectResourceImages &resources){
  auto *carrier=resources.by_key.begin()->second;for(const auto &[key,image]:resources.by_key)assert(image==carrier);
  // Three manifest slots plus twelve owned sample-node references.
  assert(carrier->flag&IMA_HIGH_BITDEPTH);
  assert(carrier->id.us==15&&!(carrier->id.tag&ID_TAG_EXTRAUSER_SET));const int flags=carrier->id.flag,tags=carrier->id.tag;
  BKE_main_id_refcount_recompute(&context.bmain,false);
  assert(carrier->id.us==15&&carrier->id.flag==flags&&carrier->id.tag==tags);
 };
 QTemporaryDir output;assert(output.isValid());
 for(int corruption=0;corruption<6;++corruption){
  bke::blendfile::PartialWriteContext snapshot(*live);snapshot.bmain.colorspace=live->colorspace;using Context=bke::blendfile::PartialWriteContext;
  auto *copy_scene=reinterpret_cast<Scene *>(snapshot.id_add(&scene->id,{Context::ADD_DEPENDENCIES}));for(auto *material:materials)snapshot.id_add(&material->id,{Context::ADD_DEPENDENCIES});
  auto owners=retain_material_layer_source_snapshot_headers(*live,snapshot.bmain);assert(owners.size()==3);
  Image *alias=nullptr;for(Image &image:snapshot.bmain.images)if(image.id.session_uid==bindings.front().image_uid)alias=&image;assert(alias&&alias->id.us>0); // synthetic EXTRAUSER must not reject the valid case
  if(corruption==1){auto *material=static_cast<Material *>(snapshot.bmain.materials.first);auto *foreign=bke::node_add_node(nullptr,*material->nodetree,UString("ShaderNodeTexImage"));foreign->id=&alias->id;id_us_plus(foreign->id);}
  if(corruption==2){IDPropertyTemplate value{};value.id=&alias->id;IDP_ReplaceInGroup(IDP_EnsureProperties(&copy_scene->id),IDP_New(IDP_ID,&value,"foreign_alias_user"));}
  if(corruption==3)id_fake_user_set(&alias->id);
  ProjectResourceImages resources;pack_project_assets(snapshot.bmain,*copy_scene,assets,&resources,&snapshot);assert(resources.by_key.size()==3&&resources.by_key.begin()->second==resources.by_key.rbegin()->second);
  if(corruption==4){auto *carrier=resources.by_key.begin()->second;IDPropertyTemplate value{};value.id=&carrier->id;IDP_ReplaceInGroup(IDP_EnsureProperties(&copy_scene->id),IDP_New(IDP_ID,&value,"foreign_carrier_user"));}
  auto checked_bindings=bindings;
  if(corruption==5){auto changed=std::make_shared<FillTexture>(*checked_bindings.back().source);changed->width=2;changed->height=3;checked_bindings.back().source=std::move(changed);} // same pixel count, later binding shares an already-checked carrier
  bool rejected=false;try{pack_material_layer_source_bindings(snapshot,*copy_scene,checked_bindings,resources);}catch(const std::invalid_argument &){rejected=true;}
  assert(rejected==(corruption!=0));assert(document.to_json()==original_document&&BLI_listbase_count(&live->images)==9);assert(alias_counts(aliases)==live_alias_counts);
  if(corruption){bool retained=false;for(const Image &image:snapshot.bmain.images)retained|=image.id.session_uid==bindings.front().image_uid;assert(retained&&snapshot.is_valid());continue;}
  verify_carrier_counts(snapshot,resources);assert(alias_counts(aliases)==live_alias_counts);
  assert(snapshot.is_valid());for(const Image &image:snapshot.bmain.images)for(const auto &binding:bindings)assert(image.id.session_uid!=binding.image_uid);
  const auto file=output.filePath("one-original.blend");ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto clear=qScopeGuard([&]{BKE_reports_free(&reports);});BlendFileWriteParams write{};write.use_save_as_copy=true;assert(BLO_write_file(&snapshot.bmain,file.toUtf8().constData(),0,&write,&reports));
  QFile bytes(file);assert(bytes.open(QIODevice::ReadOnly));const auto data=bytes.readAll();assert(data.startsWith("BLENDER")&&data.count(original.bytes)==1);
  BlendFileReadReport report{};report.reports=&reports;std::unique_ptr<BlendFileData,decltype(&BLO_blendfiledata_free)> read(BLO_read_from_file(file.toUtf8().constData(),BLO_READ_SKIP_USERDEF,&report),BLO_blendfiledata_free);assert(read&&read->main&&!read->main->scenes.is_empty());
  auto &cold=*read->main;auto &cold_scene=*static_cast<Scene *>(cold.scenes.first);ProjectResourceImages cold_images;const auto cold_assets=read_project_assets(cold_scene,&cold_images);assert(cold_assets.size()==3&&cold_images.by_key.size()==3&&cold_images.by_key.begin()->second==cold_images.by_key.rbegin()->second);
  std::vector<TextureSetBinding> set_bindings;for(const auto &set:document.texture_sets()){Material *found=nullptr;for(Material &material:cold.materials)if(set.name==material.id.name+2)found=&material;assert(found);set_bindings.push_back({set.id,found->id.session_uid,set.name});}
  auto restored=TextureDocument::from_json(original_document,set_bindings);resolve_fill_textures(restored,cold_assets);assert(validate_packed_material_layer_sources(cold,cold_scene,restored,cold_images));
  // Artist Bump uses the same signed raw resource as Metallic/Roughness:
  // disk decoding and the exact saved sampler must retain R even when alpha=0.
  for(const auto &set:restored.texture_sets()){
   const auto &height=set.material_layering.materials[2].textures[4],&metal=set.material_layering.materials[0].textures[1],&rough=set.material_layering.materials[1].textures[2];
   assert(height&&metal&&rough&&height->raw&&gpu_texture_resource_key(*height)==gpu_texture_resource_key(*metal)&&gpu_texture_resource_key(*height)==gpu_texture_resource_key(*rough)&&height->pixels&&(*height->pixels)[0][0]==-.25f&&(*height->pixels)[0][3]==0);
   Material *material=nullptr;for(Material &candidate:cold.materials)if(candidate.id.session_uid==set.material_session_uid)material=&candidate;assert(material);
   auto *sample=bke::node_find_node_by_name(*material->nodetree,"__BTS_LayerMaterial2ScalarMap4");assert(sample&&sample->id==&cold_images.by_key.begin()->second->id);
   auto *bump=bke::node_find_node_by_name(*material->nodetree,"__BTS_LayeredBump");assert(bump&&STREQ(bump->idname,"ShaderNodeBump"));
  }
  // Each cold corruption starts from the SAME verified resources/read pass.
  // Rejection must not alter the live project or silently sanitize the copied Main.
  auto *carrier=cold_images.by_key.begin()->second;
  const auto reject=[&](const TextureDocument &candidate,const ProjectResourceImages &images){bool rejected=false;try{validate_packed_material_layer_sources(cold,cold_scene,candidate,images);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);++cold_rejections;assert(document.to_json()==original_document&&BLI_listbase_count(&live->images)==9);};
  const auto check=[&]{assert(validate_packed_material_layer_sources(cold,cold_scene,restored,cold_images));};
  auto &cold_material=*static_cast<Material *>(cold.materials.first);
  {auto *node=bke::node_add_node(nullptr,*cold_material.nodetree,UString("ShaderNodeTexImage"));node->id=&carrier->id;id_us_plus(node->id);reject(restored,cold_images);bke::node_remove_node(&cold,*cold_material.nodetree,*node,true);check();}
  {IDPropertyTemplate value{};value.id=&carrier->id;auto *root=IDP_EnsureProperties(&cold_scene.id);IDP_ReplaceInGroup(root,IDP_New(IDP_ID,&value,"foreign_carrier"));reject(restored,cold_images);IDP_FreeFromGroup(root,IDP_GetPropertyFromGroup(root,"foreign_carrier"));check();}
  {id_fake_user_set(&carrier->id);reject(restored,cold_images);id_fake_user_clear(&carrier->id);check();}
  {auto *group=IDP_GetPropertyFromGroup(cold_scene.id.properties,"bts_resource_images");IDPropertyTemplate value{};value.id=&carrier->id;IDP_ReplaceInGroup(group,IDP_New(IDP_ID,&value,"unmanifested_carrier"));ProjectResourceImages with_extra;read_project_assets(cold_scene,&with_extra);reject(restored,with_extra);IDP_FreeFromGroup(group,IDP_GetPropertyFromGroup(group,"unmanifested_carrier"));check();}
  {auto *screen=reinterpret_cast<bScreen *>(BKE_id_new(&cold,ID_SCR,"Foreign source UI user"));auto *area=MEM_new<ScrArea>(__func__);auto *space=MEM_new<SpaceImage>(__func__);space->spacetype=SPACE_IMAGE;space->image=carrier;area->spacetype=SPACE_IMAGE;BLI_addtail(&area->spacedata,space);BLI_addtail(&screen->areabase,area);reject(restored,cold_images);space->image=nullptr;BKE_id_free(&cold,screen);check();}
  // Different asset UUIDs and identical bytes must not excuse a second packed
  // carrier ID. Rebind its own sample edges too, so provenance rows still match.
  {auto *duplicate=reinterpret_cast<Image *>(BKE_id_copy(&cold,&carrier->id));const auto key=rec709_asset.id.toString(QUuid::WithoutBraces).toStdString();auto *group=IDP_GetPropertyFromGroup(cold_scene.id.properties,"bts_resource_images");IDPropertyTemplate value{};value.id=&duplicate->id;IDP_ReplaceInGroup(group,IDP_New(IDP_ID,&value,key.c_str()));std::vector<ID **> edges;
   for(const auto &set:restored.texture_sets()){Material *material=nullptr;for(Material &m:cold.materials)if(m.id.session_uid==set.material_session_uid)material=&m;assert(material);auto *node=bke::node_find_node_by_name(*material->nodetree,"__BTS_LayerMaterial1Color");assert(node&&node->id==&carrier->id);edges.push_back(&node->id);id_us_min(node->id);node->id=&duplicate->id;id_us_plus(node->id);}
   ProjectResourceImages separate;read_project_assets(cold_scene,&separate);reject(restored,separate);for(auto **edge:edges){id_us_min(*edge);*edge=&carrier->id;id_us_plus(*edge);}value.id=&carrier->id;IDP_ReplaceInGroup(group,IDP_New(IDP_ID,&value,key.c_str()));BKE_id_free(&cold,duplicate);check();
  }
  // Read-only and disabled source owners must fail before restore can skip
  // them, including a valid first set followed by an unsupported second set.
  {auto changed=restored;assert(changed.texture_sets().size()==2);auto &set=changed.set(changed.texture_sets().back().material_session_uid);set.uniform_editable=false;reject(changed,cold_images);set.uniform_editable=true;set.disabled=true;reject(changed,cold_images);check();}
  {MaterialInspection empty;bool rejected=false;try{empty.validate_material_layer_sources(restored);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);++cold_rejections;}
  // Update the persisted row as well as the last typed descriptor: rejection
  // must come from this binding's header dimensions, not stale row metadata.
  {auto changed=restored;auto &set=changed.set(changed.texture_sets().back().material_session_uid);auto source=std::make_shared<FillTexture>(*set.material_layering.materials[3].textures[0]);source->width=2;source->height=3;set.material_layering.materials[3].textures[0]=source;
   auto *root=IDP_EnsureProperties(&cold_scene.id);const auto *marker=IDP_GetPropertyFromGroup(root,"bts_material_layer_sources");assert(marker&&marker->type==IDP_STRING&&marker->len>0&&marker->data.pointer&&static_cast<const char *>(marker->data.pointer)[marker->len-1]==0);const QByteArray original_rows(static_cast<const char *>(marker->data.pointer),marker->len-1);auto object=QJsonDocument::fromJson(original_rows).object();auto rows=object["bindings"].toArray();bool replaced=false;for(int i=0;i<rows.size();++i){auto row=rows[i].toObject();if(row["set"].toString()==set.id.toString(QUuid::WithoutBraces)&&row["input"]==3&&row["channel"]==0){row["texture"]=fill_texture_json(source);rows[i]=row;replaced=true;}}assert(replaced);object["bindings"]=rows;const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);IDP_ReplaceInGroup(root,IDP_NewString(bytes.constData(),"bts_material_layer_sources"));reject(changed,cold_images);IDP_ReplaceInGroup(root,IDP_NewString(original_rows.constData(),"bts_material_layer_sources"));check();
  }
  {const auto old=carrier->source;carrier->source=IMA_SRC_GENERATED;reject(restored,cold_images);carrier->source=old;check();}
  {const auto old=carrier->type;carrier->type=IMA_TYPE_MULTILAYER;reject(restored,cold_images);carrier->type=old;check();}
  auto *part=static_cast<ImagePackedFile *>(carrier->packedfiles.first);auto *tile=static_cast<ImageTile *>(carrier->tiles.first);
  {part->view=1;reject(restored,cold_images);part->view=0;check();}
  {part->tile_number=1002;reject(restored,cold_images);part->tile_number=1001;check();}
  {tile->tile_number=1002;reject(restored,cold_images);tile->tile_number=1001;check();}
  {auto *extra=MEM_new<ImageTile>(__func__);extra->tile_number=1002;BLI_addtail(&carrier->tiles,extra);reject(restored,cold_images);BLI_remlink(&carrier->tiles,extra);MEM_delete(extra);check();}
  {carrier->flag|=IMA_USE_VIEWS;reject(restored,cold_images);carrier->flag&=~IMA_USE_VIEWS;check();}
  {assert(carrier->flag&IMA_HIGH_BITDEPTH);const auto old=carrier->flag;carrier->flag&=~IMA_HIGH_BITDEPTH;reject(restored,cold_images);carrier->flag=old;check();}
  {const auto old=carrier->alpha_mode;carrier->alpha_mode=IMA_ALPHA_PREMUL;reject(restored,cold_images);carrier->alpha_mode=old;check();}
  {const std::string old=carrier->colorspace_settings.name;STRNCPY(carrier->colorspace_settings.name,"ACEScg");reject(restored,cold_images);STRNCPY(carrier->colorspace_settings.name,old.c_str());check();}
  auto *coordinates=bke::node_find_node_by_name(*cold_material.nodetree,"__BTS_LayerUvCoordinates");assert(coordinates);
  {coordinates->custom1=1;reject(restored,cold_images);coordinates->custom1=0;check();}
  {auto *object=static_cast<ID *>(BKE_id_new(&cold,ID_OB,"Unexpected source coordinate object"));coordinates->id=object;reject(restored,cold_images);coordinates->id=nullptr;BKE_id_free(&cold,object);check();}
  auto *sample=bke::node_find_node_by_name(*cold_material.nodetree,"__BTS_LayerMaterial0Color");assert(sample&&sample->storage);auto &settings=*static_cast<NodeTexImage *>(sample->storage);
  {settings.iuser.scene=&cold_scene;reject(restored,cold_images);settings.iuser.scene=nullptr;check();}
  {const auto old=settings.iuser.view;settings.iuser.view=1;reject(restored,cold_images);settings.iuser.view=old;check();}
  {const auto old=settings.iuser.tile;settings.iuser.tile=1002;reject(restored,cold_images);settings.iuser.tile=old;check();}
  {settings.base.tex_mapping.loc[0]=.5f;reject(restored,cold_images);settings.base.tex_mapping.loc[0]=0;check();}
  // No new representation marker means legacy read-only Open is unaffected.
  {auto *root=IDP_EnsureProperties(&cold_scene.id);auto *marker=IDP_GetPropertyFromGroup(root,"bts_material_layer_sources");IDP_RemoveFromGroup(root,marker);auto changed=restored;changed.set(changed.texture_sets().back().material_session_uid).uniform_editable=false;assert(!validate_packed_material_layer_sources(cold,cold_scene,changed,cold_images));IDP_AddToGroup(root,marker);check();}
  for(const Image &image:cold.images)assert(!(image.runtime->gpuflag&IMA_HOSTED_IMMUTABLE_SOURCE)); // no runtime/cache identity serialized
  assert(BLI_listbase_count(&cold.images)==7);
  // Reconstruct canonical typed aliases once, exactly as the new owner source
  // stage does (without creating a GPU context in this CPU fixture).
  std::map<std::string,Image *> cold_aliases;
  for(const auto &stored:restored.texture_sets()){auto &set=restored.set(stored.material_session_uid);Material *material=nullptr;for(Material &candidate:cold.materials)if(candidate.id.session_uid==set.material_session_uid)material=&candidate;assert(material);MaterialLayerImages images{};
   for(int input=0;input<4;++input)for(int channel=0;channel<6;++channel)if(const auto &source=set.material_layering.materials[input].textures[channel]){auto [at,fresh]=cold_aliases.try_emplace(gpu_texture_resource_key(*source));if(fresh){auto *image=reinterpret_cast<Image *>(BKE_id_new(&cold,ID_IM,"Reopened typed alias"));id_us_min(&image->id);describe_source(*image,source->width,source->height);Buffer header(material_layer_source_pixel_view(*source),IMB_freeImBuf);assert(BKE_image_assign_hosted_immutable_source(image,1001,header.get()));at->second=image;}images[input][channel]=at->second;}
   MaterialLayerShader shader;shader.apply(cold,*material,set.material_layering,1,1,false,images);
  }
  assert(cold_aliases.size()==3&&BLI_listbase_count(&cold.images)==10);
  for(const auto &set:restored.texture_sets()){
   Material *material=nullptr;for(Material &candidate:cold.materials)if(candidate.id.session_uid==set.material_session_uid)material=&candidate;assert(material);
   auto *height=bke::node_find_node_by_name(*material->nodetree,"__BTS_LayerMaterial2ScalarMap4"),*metal=bke::node_find_node_by_name(*material->nodetree,"__BTS_LayerMaterial0ScalarMap1"),*rough=bke::node_find_node_by_name(*material->nodetree,"__BTS_LayerMaterial1ScalarMap2");
   assert(height&&metal&&rough&&height->id==metal->id&&height->id==rough->id);
  }

  const auto next_bindings=capture_material_layer_source_bindings(cold,restored);assert(next_bindings.size()==bindings.size());
  for(auto &view_layer:cold_scene.view_layers)BKE_view_layer_synced_ensure(cold,&cold_scene,&view_layer);
  const auto cold_alias_counts=alias_counts(cold_aliases);
  bke::blendfile::PartialWriteContext again(cold);again.bmain.colorspace=cold.colorspace;auto *again_scene=reinterpret_cast<Scene *>(again.id_add(&cold_scene.id,{Context::ADD_DEPENDENCIES}));for(Material &material:cold.materials)again.id_add(&material.id,{Context::ADD_DEPENDENCIES});auto next_owners=retain_material_layer_source_snapshot_headers(cold,again.bmain);
  ProjectResourceImages again_images;pack_project_assets(again.bmain,*again_scene,cold_assets,&again_images,&again);pack_material_layer_source_bindings(again,*again_scene,next_bindings,again_images);assert(again.is_valid()&&BLI_listbase_count(&again.bmain.images)==7);verify_carrier_counts(again,again_images);assert(alias_counts(cold_aliases)==cold_alias_counts&&alias_counts(aliases)==live_alias_counts);
  const auto second_file=output.filePath("one-original-again.blend");assert(BLO_write_file(&again.bmain,second_file.toUtf8().constData(),0,&write,&reports));QFile second(second_file);assert(second.open(QIODevice::ReadOnly)&&second.readAll().count(original.bytes)==1);
  // Disabling the new representation cannot leave a stale marker that rejects
  // an otherwise ordinary subsequent Open.
  pack_material_layer_source_bindings(again,*again_scene,{},again_images);assert(!IDP_GetPropertyFromGroup(again_scene->id.properties,"bts_material_layer_sources"));
 }
 return cold_rejections;
}
#include "MaterialLayerSrgbSourceTest.inc"
#include "MaterialLayerRawUdimTest.inc"
#include "MaterialLayerColorUdimPackageTest.inc"
#include "MaterialLayerColorUdimTest.inc"
#include "MaterialLayerParameterPackageTest.inc"
#ifdef BTS_MATERIAL_LAYER_SOURCE_LOADER_TEST
void verify_actual_cycles_source_loader(){
 using namespace blender;using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
 auto *image=reinterpret_cast<Image *>(BKE_id_new_nomain(ID_IM,"Actual Cycles immutable source"));describe_source(*image,2,2);
 bool missing=false;try{MaterialLayeringCyclesSourceProbe probe(*image);}catch(const std::runtime_error &error){missing=std::string(error.what()).find("Immutable hosted source cache is missing")!=std::string::npos;}assert(missing&&!BKE_image_has_loaded_ibuf(image));
 FillTexture source;source.asset=std::string(36,'1');source.hash=std::string(64,'2');source.name="Actual loader source";source.width=source.height=2;source.raw=true;
 source.pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(4,FillTexture::Pixel{-7.f,70000.f,.125f,0});const auto expected=*source.pixels;std::weak_ptr<const std::vector<FillTexture::Pixel>> lifetime=source.pixels;
 Buffer buffer(material_layer_source_pixel_view(source),IMB_freeImBuf);assert(BKE_image_assign_hosted_immutable_source(image,1001,buffer.get()));
 MaterialLayeringCyclesSourceProbe loader(*image);assert(loader.immutable_snapshot());auto before=loader.read();assert(before.width==2&&before.height==2&&before.data&&before.pixels==expected);
 Buffer retired(BKE_image_detach_hosted_source(image,1001),IMB_freeImBuf);retired.reset();buffer.reset();source.pixels.reset();assert(!lifetime.expired());
 assert(loader.read().pixels==expected);loader.release_on_worker();assert(lifetime.expired());BKE_id_free(nullptr,&image->id);
 // Unmarked mutable image must continue acquiring its CURRENT live cache.
 image=reinterpret_cast<Image *>(BKE_id_new_nomain(ID_IM,"Actual Cycles mutable source"));describe_source(*image,2,2);
 Buffer mutable_buffer(IMB_allocImBuf(2,2,ImBufFlags::FloatData),IMB_freeImBuf);mutable_buffer->channels=4;IMB_colormanagement_assign_float_colorspace(mutable_buffer.get(),IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA));
 std::fill_n(mutable_buffer->float_data_for_write(),16,.25f);assert(BKE_image_assign_hosted_immutable_source(image,1001,mutable_buffer.get()));image->runtime->gpuflag&=~IMA_HOSTED_IMMUTABLE_SOURCE;
 MaterialLayeringCyclesSourceProbe mutable_loader(*image);assert(!mutable_loader.immutable_snapshot());assert(mutable_loader.read().pixels[0][0]==.25f);
 mutable_buffer->float_data_for_write()[0]=.75f;assert(mutable_loader.read().pixels[0][0]==.75f);mutable_loader.release_on_worker();BKE_id_free(nullptr,&image->id);
}
#endif
}
QJsonObject test_material_layering_source_pixels(){
 const auto eevee_completion_value_predicate=pd149_eevee_observation::verify_value_predicate();
 const auto surface_graph_value_predicate=pd149_surface_test::verify_surface_graph_predicate();
 const auto linear_color_udim=verify_linear_color_udim_carriers();
 const auto parameter_package=verify_material_parameter_package();
 const int raw_udim_rejections=verify_raw_udim_original_carriers();
 const auto sampler_cases=verify_native_material_sampler_states();
 const int srgb_processor_rejections=verify_srgb_processor_structure(),srgb_graph_rejections=verify_srgb_original_carrier();
 const int embedded_cache_cases=verify_partial_write_embedded_cache_ownership();
 const int cold_source_rejections=verify_original_backed_source_snapshot();
 auto source=std::make_shared<FillTexture>();source->asset=std::string(36,'a');source->hash=std::string(64,'b');source->name="Signed shared source";source->width=3;source->height=2;source->raw=true;
 auto pixels=std::make_shared<std::vector<FillTexture::Pixel>>(6,FillTexture::Pixel{-7.25f,2.f,70000.f,0.f});source->pixels=pixels;
 std::weak_ptr<const std::vector<FillTexture::Pixel>> lifetime=source->pixels;
 const auto *original=pixels->front().data();
 using Buffer=std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>;
 Buffer view(material_layer_source_pixel_view(*source),blender::IMB_freeImBuf);
 assert(view->float_data()==original&&view->x==3&&view->y==2&&view->channels==4&&!view->gpu.texture&&view->colorspace_is_data());
 Buffer reader(blender::IMB_dupImBuf(view.get()),blender::IMB_freeImBuf);
 assert(reader&&reader->float_data()==original&&reader->float_buffer.sharing_info.get()==view->float_buffer.sharing_info.get());
 // A save/edit copy must detach instead of modifying the immutable decoder.
 Buffer writable(blender::IMB_dupImBuf(view.get()),blender::IMB_freeImBuf);auto *changed=writable->float_data_for_write();assert(changed!=original);changed[0]=99;assert(view->float_data()[0]==-7.25f&&reader->float_data()[2]==70000.f);
 const auto key=gpu_texture_resource_key(*source);auto alias=*source;alias.asset=std::string(36,'z');alias.name="Other material input";assert(gpu_texture_resource_key(alias)==key);
 alias.raw=false;alias.input_space="sRGB";assert(gpu_texture_resource_key(alias)!=key);alias=*source;alias.width=2;assert(gpu_texture_resource_key(alias)!=key);
 // The source descriptor/cache view can retire before a live reader. The
 // header's sharing owner, not a Main/Image pointer, keeps exact pixels alive.
 alias.pixels.reset();source.reset();pixels.reset();view.reset();assert(!lifetime.expired()&&reader->float_data()==original&&reader->float_data()[3]==0);
 reader.reset();assert(lifetime.expired());
 // Real cache-only APIs: no missing-buffer decode or generated blank fallback.
 auto *image=reinterpret_cast<blender::Image *>(blender::BKE_id_new_nomain(blender::ID_IM,"Immutable source lifetime"));assert(image);
 image->source=blender::IMA_SRC_GENERATED;image->type=blender::IMA_TYPE_UV_TEST;image->alpha_mode=blender::IMA_ALPHA_CHANNEL_PACKED;
 auto *tile=static_cast<blender::ImageTile *>(image->tiles.first);tile->gen_x=3;tile->gen_y=2;
 image->runtime->gpuflag|=blender::IMA_HOSTED_IMMUTABLE_SOURCE;
 assert(!blender::BKE_image_hosted_source_snapshot(image,1001));
 FillTexture second;second.asset=std::string(36,'c');second.hash=std::string(64,'d');second.name="Cache lifetime";second.width=3;second.height=2;second.raw=true;
 second.pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(6,FillTexture::Pixel{-.5f,.25f,1,0});lifetime=second.pixels;
 Buffer cached(material_layer_source_pixel_view(second),blender::IMB_freeImBuf);const auto *cached_pixels=cached->float_data();
 assert(blender::BKE_image_assign_hosted_immutable_source(image,1001,cached.get()));
 const auto cache_revision=image->runtime->update_count;
 Buffer loader(blender::BKE_image_hosted_source_snapshot(image,1001),blender::IMB_freeImBuf);assert(loader&&loader->float_data()==cached_pixels);
 assert(!blender::BKE_image_hosted_source_snapshot(image,1002));
 assert(!blender::BKE_image_detach_hosted_source(image,1002));
 assert(blender::BKE_image_has_loaded_ibuf(image));
 Buffer retired(blender::BKE_image_detach_hosted_source(image,1001),blender::IMB_freeImBuf);assert(retired&&retired->float_data()==cached_pixels);
 assert(!blender::BKE_image_hosted_source_snapshot(image,1001));cached.reset();retired.reset();second.pixels.reset();
 assert(!lifetime.expired()&&loader->float_data()[0]==-.5f);loader.reset();assert(lifetime.expired());
 assert(!blender::BKE_image_detach_hosted_source(image,1001)&&!blender::BKE_image_has_loaded_ibuf(image));
 assert(image->runtime->update_count==cache_revision); // No live-image revision/generation.
 // An extra owned sharing guard protects even a sole writable snapshot header
 // after the live cache/descriptor has retired (the save-worker lifetime).
 second.pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(6,FillTexture::Pixel{-.75f,1,2,0});
 Buffer guarded(material_layer_source_pixel_view(second),blender::IMB_freeImBuf);const auto *guarded_original=guarded->float_data();auto guard=guarded->float_buffer.sharing_info;second.pixels.reset();
 auto *guarded_write=guarded->float_data_for_write();assert(guarded_write!=guarded_original&&guarded_original[0]==-.75f);guarded_write[0]=100;assert(guarded_original[0]==-.75f);guard={};
 // Mutable authored images are never opted in merely because a cache is loaded;
 // rejection must leave that ordinary cache intact.
 assert(blender::BKE_image_assign_hosted_immutable_source(image,1001,guarded.get()));
 image->runtime->gpuflag&=~blender::IMA_HOSTED_IMMUTABLE_SOURCE;
 assert(!blender::BKE_image_hosted_source_snapshot(image,1001)&&!blender::BKE_image_detach_hosted_source(image,1001));
 assert(blender::BKE_image_has_loaded_ibuf(image));
 blender::BKE_id_free(nullptr,&image->id);
 // Two explicit UDIM cache entries, different original dimensions. Test the
 // actual index/entry encoding, absent tile, and independent retirement.
 auto *tiled=reinterpret_cast<blender::Image *>(blender::BKE_id_new_nomain(blender::ID_IM,"Immutable tiled lifetime"));assert(tiled);
 tiled->source=blender::IMA_SRC_TILED;tiled->type=blender::IMA_TYPE_IMAGE;tiled->alpha_mode=blender::IMA_ALPHA_CHANNEL_PACKED;tiled->runtime->gpuflag|=blender::IMA_HOSTED_IMMUTABLE_SOURCE;
 auto *first=static_cast<blender::ImageTile *>(tiled->tiles.first);first->gen_x=3;first->gen_y=2;
 auto *third=blender::BKE_image_add_tile(tiled,1003,nullptr);assert(third);third->gen_x=1;third->gen_y=4;
 second.pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(6,FillTexture::Pixel{1,2,3,0});Buffer a(material_layer_source_pixel_view(second),blender::IMB_freeImBuf);
 auto other=second;other.width=1;other.height=4;other.pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(4,FillTexture::Pixel{-4,5,6,0});Buffer b(material_layer_source_pixel_view(other),blender::IMB_freeImBuf);
 const auto tiled_revision=tiled->runtime->update_count;
 assert(blender::BKE_image_assign_hosted_immutable_source(tiled,1001,a.get())&&blender::BKE_image_assign_hosted_immutable_source(tiled,1003,b.get()));
 Buffer read_a(blender::BKE_image_hosted_source_snapshot(tiled,1001),blender::IMB_freeImBuf),read_b(blender::BKE_image_hosted_source_snapshot(tiled,1003),blender::IMB_freeImBuf);
 assert(read_a&&read_b&&read_a->float_data()==a->float_data()&&read_b->float_data()==b->float_data()&&read_b->x==1&&read_b->y==4);
 assert(!blender::BKE_image_hosted_source_snapshot(tiled,1002)&&!blender::BKE_image_detach_hosted_source(tiled,1002));
 Buffer drop_b(blender::BKE_image_detach_hosted_source(tiled,1003),blender::IMB_freeImBuf);assert(drop_b&&drop_b->float_data()==b->float_data());
 assert(!blender::BKE_image_hosted_source_snapshot(tiled,1003)&&!blender::BKE_image_detach_hosted_source(tiled,1003));
 Buffer still_a(blender::BKE_image_hosted_source_snapshot(tiled,1001),blender::IMB_freeImBuf);assert(still_a&&still_a->float_data()==a->float_data());
 assert(tiled->runtime->update_count==tiled_revision);blender::BKE_id_free(nullptr,&tiled->id);
 assert(read_a->float_data()[0]==1&&read_b->float_data()[0]==-4);
 verify_source_snapshot_worker_pack();
 QJsonObject result{{"zero_copy_float_header",true},{"shared_reader_lifetime",true},{"cache_snapshot_retirement",true},{"single_and_tiled_cache_indices",true},{"cache_miss_absent_mutable_no_generation",true},{"copy_on_write_with_extra_owned_guard",true},{"signed_hdr_and_alpha_unchanged",true},{"interpretation_key_separates_data_color",true},{"real_partial_write_shared_headers_and_worker_pack",true}};
 result["raw_bump_signed_R_alpha_zero_packed_cold_shared_alias"]=true;result["original_encoded_single_carrier_round_trip"]=true;result["typed_runtime_aliases_reconstructed_once"]=true;result["foreign_source_user_cases"]=4;result["later_save_binding_dimensions_rejected"]=true;result["cold_source_rejection_cases"]=cold_source_rejections;result["float_nonlinear_source_persistence_rejected"]=true;result["srgb_processor_rejections"]=srgb_processor_rejections;result["srgb_graph_rejections"]=srgb_graph_rejections;result["srgb_same_encoding_color_data_one_carrier"]=true;
 result["eevee_completion_value_predicate"]=eevee_completion_value_predicate;
 result["surface_graph_value_predicate"]=surface_graph_value_predicate;
 result["material_layer_parameter_package"]=parameter_package;
 result["linear_color_udim"]=linear_color_udim;
 result["raw_udim_graph_cold_rejections"]=raw_udim_rejections;result["raw_udim_original_backed_roundtrip"]=true;
 result["partial_write_embedded_cache_cases"]=embedded_cache_cases;result["native_material_sampler_state_cases"]=sampler_cases;result["native_sampler_filter31_resolved16x"]=true;
#ifdef BTS_MATERIAL_LAYER_SOURCE_LOADER_TEST
 verify_actual_cycles_source_loader();result["actual_cycles_loader"]=true;
#else
 result["actual_cycles_loader"]="not compiled in this fixture; separate Cycles-ABI probe required";
#endif
 return result;
}
}
