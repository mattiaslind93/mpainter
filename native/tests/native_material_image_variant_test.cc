#include <QApplication>
#include <QCryptographicHash>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QScopeGuard>
#include "BlenderHost.hh"
#include "NativeMaterialImageVariant.hh"
#include "NativeMaterialImageDependencies.hh"
#include "NativeMaterialSource.hh"
#include "NativeMaterialSourceFiles.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialArchive.hh"
#include "NativeMaterialImages.hh"
#include "NativeMaterialImageResource.hh"
#include "ResourceAvailability.hh"
#include "ResourceIO.hh"
#include "BKE_blendfile.hh"
#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_report.hh"
#include "BKE_scene.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "BLO_writefile.hh"
#include "BLO_readfile.hh"
#include "DNA_image_types.h"
#include "DNA_colorband_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_scene_types.h"
#include "IMB_colormanagement.hh"
#include <algorithm>
#include <clocale>
#include <future>
#include <iostream>

namespace {
using namespace bts;using namespace blender;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}require(rejected,"Expected checked rejection");}
QByteArray load(const QString &p){QFile f(p);require(f.open(QIODevice::ReadOnly),"Cannot read own fixture");return f.readAll();}
QString sha(const QByteArray &bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
bNodeSocket &socket(bNode &n,const char *id,bool input){for(auto &s:input?n.inputs:n.outputs)if(STREQ(s.identifier,id))return s;throw std::runtime_error("Missing synthetic socket");}
bNode &add(bNodeTree &tree,const char *type){auto *n=bke::node_add_node(nullptr,tree,UString(type));require(n,"Cannot create native node");return *n;}
bNode &group(Main &main,bNodeTree &parent,bNodeTree &child){auto &n=add(parent,"ShaderNodeGroup");n.id=&child.id;id_us_plus(n.id);BKE_ntree_update(main);return n;}
void output(bNodeTree &tree){require(tree.tree_interface.add_socket("Color","","NodeSocketColor",NODE_INTERFACE_SOCKET_OUTPUT,nullptr),"Cannot add group interface");}
void fixture(const QString &path,const QString &imagepath,bool high=true,bool top_group=false){
 std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(main.get());
 auto *image=BKE_image_load(main.get(),imagepath.toUtf8().constData());require(image,"Cannot add source image");STRNCPY(image->colorspace_settings.name,"sRGB");image->alpha_mode=IMA_ALPHA_STRAIGHT;if(high)image->flag|=IMA_HIGH_BITDEPTH;else image->flag&=~IMA_HIGH_BITDEPTH;
 auto *inner=bke::node_tree_add_tree(main.get(),"Shared-inner","ShaderNodeTree");output(*inner);auto &tex=add(*inner,"ShaderNodeTexImage");tex.id=&image->id;id_us_plus(tex.id);auto &coordinates=add(*inner,"ShaderNodeTexCoord");bke::node_add_link(*inner,coordinates,socket(coordinates,"UV",false),tex,socket(tex,"Vector",true));auto &mix=add(*inner,"ShaderNodeMixRGB");mix.custom1=MA_RAMP_BLEND;auto *factor=static_cast<bNodeSocket *>(mix.inputs.first);static_cast<bNodeSocketValueFloat *>(factor->default_value)->value=.25f;
 auto &innerout=add(*inner,"NodeGroupOutput");innerout.flag|=NODE_DO_OUTPUT;BKE_ntree_update(*main);
 // The same node has two operative users. Replacement must split one edge,
 // preserving the other fanout and both occurrences of this shared group.
 bke::node_add_link(*inner,tex,socket(tex,"Color",false),mix,*factor->next);
 bke::node_add_link(*inner,tex,socket(tex,"Color",false),mix,*factor->next->next);
 bke::node_add_link(*inner,mix,socket(mix,"Color",false),innerout,*static_cast<bNodeSocket *>(innerout.inputs.first));
 auto *outer=bke::node_tree_add_tree(main.get(),"Shared-outer","ShaderNodeTree");output(*outer);auto &inside=group(*main,*outer,*inner);auto &outerout=add(*outer,"NodeGroupOutput");outerout.flag|=NODE_DO_OUTPUT;BKE_ntree_update(*main);
 bke::node_add_link(*outer,inside,*static_cast<bNodeSocket *>(inside.outputs.first),outerout,*static_cast<bNodeSocket *>(outerout.inputs.first));
 auto *material=BKE_material_add(main.get(),"Replacement-source");auto &tree=top_group?*bke::node_tree_add_tree(main.get(),"Surface-wrapper","ShaderNodeTree"):*material->nodetree;
 if(top_group)require(tree.tree_interface.add_socket("Shader","","NodeSocketShader",NODE_INTERFACE_SOCKET_OUTPUT,nullptr),"Cannot add shader interface");auto &left=group(*main,tree,*outer),&right=group(*main,tree,*outer);auto &combine=add(tree,"ShaderNodeMixRGB");combine.custom1=MA_RAMP_BLEND;factor=static_cast<bNodeSocket *>(combine.inputs.first);static_cast<bNodeSocketValueFloat *>(factor->default_value)->value=.5f;
 auto &pbr=add(tree,"ShaderNodeBsdfPrincipled"),&out=add(tree,top_group?"NodeGroupOutput":"ShaderNodeOutputMaterial");if(top_group){out.flag|=NODE_DO_OUTPUT;BKE_ntree_update(*main);}
 auto &normaltex=add(tree,"ShaderNodeTexImage");normaltex.id=&image->id;id_us_plus(normaltex.id);auto &normal=add(tree,"ShaderNodeNormalMap");
 bke::node_add_link(tree,normaltex,socket(normaltex,"Color",false),normal,socket(normal,"Color",true));bke::node_add_link(tree,normal,socket(normal,"Normal",false),pbr,socket(pbr,"Normal",true));
 auto &ramp=add(tree,"ShaderNodeValToRGB");bke::node_add_link(tree,ramp,socket(ramp,"Color",false),pbr,socket(pbr,"Roughness",true));
 bke::node_add_link(tree,left,*static_cast<bNodeSocket *>(left.outputs.first),combine,*factor->next);
 bke::node_add_link(tree,right,*static_cast<bNodeSocket *>(right.outputs.first),combine,*factor->next->next);
 bke::node_add_link(tree,combine,socket(combine,"Color",false),pbr,socket(pbr,"Base Color",true));bke::node_add_link(tree,pbr,socket(pbr,"BSDF",false),out,top_group?*static_cast<bNodeSocket *>(out.inputs.first):socket(out,"Surface",true));
 if(top_group){auto &top=group(*main,*material->nodetree,tree),&output=add(*material->nodetree,"ShaderNodeOutputMaterial");bke::node_add_link(*material->nodetree,top,*static_cast<bNodeSocket *>(top.outputs.first),output,socket(output,"Surface",true));}BKE_ntree_update(*main);
 bke::blendfile::PartialWriteContext writer(*main);writer.id_add(&material->id,{bke::blendfile::PartialWriteContext::ADD_DEPENDENCIES|bke::blendfile::PartialWriteContext::MAKE_LOCAL|bke::blendfile::PartialWriteContext::SET_FAKE_USER});ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto clear=qScopeGuard([&]{BKE_reports_free(&reports);});require(writer.write(path.toUtf8().constData(),0,BLO_WRITE_PATH_REMAP_NONE,reports),"Cannot write synthetic native library");
}
StudioAsset packed(StudioAsset source){require(source.texture_files.size()==1,"Single source expected");source.bytes=load(source.texture_files.front().path);source.texture_files.clear();source.path.clear();return source;}
NativeImageBindingInventory inventory(const StudioAsset &source,const std::vector<StudioAsset> &images){std::atomic_bool cancel=false;auto bundle=read_native_material_bundle(source.bytes);auto scene=source;scene.bytes=bundle.blend;auto detached=read_native_material_source(materialize_native_material_archive(scene,cancel),source.fill_preset->native_material->material,cancel);bind_native_material_source_images(*detached.main,bundle,images,cancel);return native_material_image_bindings(*detached.material,cancel);}
QJsonObject run(const QString &out){
 QDir().mkpath(out);const auto image=out+"/source.png",replacement_file=out+"/replacement.png",library=out+"/source.blend";
 QImage p(3,2,QImage::Format_RGBA8888);p.fill(QColor(224,32,96));require(p.save(image),"Cannot write source PNG");p.fill(QColor(16,192,48));require(p.save(replacement_file),"Cannot write replacement PNG");fixture(library,image);const auto original_library=sha(load(library)),original_png=sha(load(image));
 std::atomic_bool cancel=false;auto detached=read_native_material_source(library,"Replacement-source",cancel);auto graph=finish_native_material_source(std::move(detached),cancel);auto archived=archive_native_material(*graph,library,cancel,true);auto original=native_material_asset("Replacement-source",archived.bytes,archived.description);auto sources=archived.textures;require(sources.size()==1,"Original sources not deduplicated");
 auto bindings=inventory(original,sources);require(bindings.bindings.size()==5,"Expected four nested Color edges plus one unsupported Normal edge");for(const auto &b:bindings.bindings){require(read_native_image_binding(native_image_binding_json(b))==b,"Binding metadata roundtrip changed exact path");}
 const auto available=native_material_image_availability(original,cancel);require(available.bindings.size()==5,"Eligibility inventory lost source links");int enabled=0,disabled=0;for(const auto &row:available.bindings){if(row.available()){++enabled;require(row.binding.groups.size()==2,"Unsupported shared Normal edge inherited Color eligibility");}else{++disabled;require(!row.reason.isEmpty(),"Unavailable exact link has no explanation");}}require(enabled==4&&disabled==1,"Exact image eligibility did not isolate each original edge");
 const auto chosen=*std::find_if(bindings.bindings.begin(),bindings.bindings.end(),[](const auto &b){return b.groups.size()==2;});
 const auto inactive=*std::find_if(bindings.bindings.begin(),bindings.bindings.end(),[](const auto &b){return b.groups.empty();});auto modified=native_image_binding_json(chosen);modified["node"]=2147483648.;rejects([&]{read_native_image_binding(modified);});modified=native_image_binding_json(chosen);modified["unexpected"]=1;rejects([&]{read_native_image_binding(modified);});
 {TextureRamQueryScope budget([]()->uint64_t{return 0;});([&]{native_image_binding_json(chosen);})();}
 StudioAsset replacement;replacement.name="Replacement";replacement.filename="replacement.png";replacement.usages={"texture"};replacement.bytes=load(replacement_file);const auto encoded=replacement.bytes;NativeImageReplacementPolicy unknown;rejects([&]{prepare_native_image_variant(original,sources,*original.fill_preset->native_material,chosen,replacement,unknown,cancel);});
 NativeImageReplacementPolicy named{NativeImageSourceSpace::Named,"sRGB"};
 auto altered=original;auto mismatched=*original.fill_preset->native_material;mismatched.group+="-changed";altered.fill_preset->native_material=std::make_shared<const NativeMaterialDescription>(mismatched);rejects([&]{prepare_native_image_variant(altered,sources,*original.fill_preset->native_material,chosen,replacement,named,cancel);});rejects([&]{prepare_native_image_variant(original,sources,*original.fill_preset->native_material,inactive,replacement,named,cancel);});auto stale=chosen;stale.input+="-changed";rejects([&]{prepare_native_image_variant(original,sources,*original.fill_preset->native_material,stale,replacement,named,cancel);});
 cancel=true;rejects([&]{prepare_native_image_variant(original,sources,*original.fill_preset->native_material,chosen,replacement,named,cancel);});cancel=false;
 auto instance=*original.fill_preset->native_material;bool scalar=false,ramp=false;
 for(auto &parameter:instance.parameters){if(parameter.identifier=="Metallic"){parameter.value[0]=.625;scalar=true;}if(!parameter.ramp.isEmpty()){parameter.ramp["interpolation"]=int(COLBAND_INTERP_CONSTANT);ramp=true;}}
 require(scalar&&ramp,"Synthetic instance lacks editable scalar/ramp controls");
 const auto original_hash=original.hash();auto first=prepare_native_image_variant(original,sources,instance,chosen,replacement,named,cancel);
 for(size_t i=0;i<instance.parameters.size();++i)require(first.instance.parameters[i].value==instance.parameters[i].value&&first.instance.parameters[i].ramp==instance.parameters[i].ramp,"Replacement discarded existing instance/ramp overrides");
 require(first.prepared.diagnostics["supported_aov_usage_verified"].toBool()&&!first.prepared.diagnostics["active_channel_uses"].toArray().isEmpty(),"Candidate lacks exact supported AOV eligibility");
 require(first.original.id==original.id&&first.original.hash()==original_hash,"Original Reset source changed");require(first.prepared.textures.size()==2,"Variant duplicated unchanged encoded source");require(replacement.bytes==encoded&&first.prepared.asset.hash()!=original_hash,"Replacement did not create independent immutable source");
 require(std::any_of(first.prepared.textures.begin(),first.prepared.textures.end(),[&](const auto &a){return a.id==sources.front().id&&a.hash()==sources.front().hash();}),"Unchanged external image UUID was duplicated");
 const auto variant_bindings=inventory(first.prepared.asset,first.prepared.textures);require(variant_bindings.bindings.size()==5,"Replacement changed source fanout count");int changed=0,unchanged=0;for(const auto &b:variant_bindings.bindings){if(b.image==chosen.image)++unchanged;else ++changed;}require(changed==1&&unchanged==4,"Replacement affected another group occurrence or fanout edge");
 {const auto &changed_binding=*std::find_if(variant_bindings.bindings.begin(),variant_bindings.bindings.end(),[&](const auto &b){return b.image!=chosen.image;});const auto resolved=resolve_native_image_binding(*first.prepared.graph->material,changed_binding);int vector_links=0;for(const auto &link:resolved.tree->links)if(link.tonode==resolved.image_node&&STREQ(link.tosock->identifier,"Vector")){require(STREQ(link.fromnode->idname,"ShaderNodeTexCoord")&&STREQ(link.fromsock->identifier,"UV"),"Replacement changed its original Vector source");++vector_links;}require(vector_links==1,"Replacement lost its original linked Vector input");}
 const auto provenance=native_image_variant_provenance(first.prepared.asset);require(provenance&&provenance->original_id==original.id&&provenance->original_sha256==original_hash&&provenance->operations.size()==1,"Variant provenance lost original Reset identity");
 auto all=first.prepared.textures;all.push_back(original);all.push_back(first.prepared.asset);validate_native_material_dependencies(first.prepared.asset,all);auto missing=all;std::erase_if(missing,[&](const auto &a){return a.id==original.id;});rejects([&]{validate_native_material_dependencies(first.prepared.asset,missing);});
 auto second_binding=*std::find_if(variant_bindings.bindings.begin(),variant_bindings.bindings.end(),[](const auto &b){return b.groups.size()==2;});auto second=prepare_native_image_variant(first.prepared.asset,all,first.instance,second_binding,replacement,named,cancel);require(second.provenance.original_id==original.id&&second.provenance.operations.size()==2,"Repeated replacement chained Reset instead of retaining original");
 AssetCatalog catalog(out+"/catalog");for(const auto &a:all)catalog.add(a);const auto retained=catalog.retain_project(first.prepared.asset);const auto projects=catalog.project_assets();require(std::any_of(projects.begin(),projects.end(),[&](const auto &a){return a.id==original.id&&a.hash()==original_hash&&a.scope==AssetScope::Project;}),"Packed collection lost exact original Reset resource");require(!retained.isNull(),"Variant catalog retention failed");
 // Real native libraries often expose a Shader Group rather than Principled
 // at the material surface. Copying that occurrence changes its source group
 // ID name; only the new source layout receives that identity, not the original.
 const auto wrapped_file=out+"/shader-group-source.blend";fixture(wrapped_file,image,true,true);auto wrapped_source=read_native_material_source(wrapped_file,"Replacement-source",cancel);auto wrapped_graph=finish_native_material_source(std::move(wrapped_source),cancel);auto wrapped_archive=archive_native_material(*wrapped_graph,wrapped_file,cancel,true);auto wrapped=native_material_asset("Wrapped source",wrapped_archive.bytes,wrapped_archive.description);const auto wrapped_inventory=inventory(wrapped,wrapped_archive.textures);const auto wrapped_binding=*std::find_if(wrapped_inventory.bindings.begin(),wrapped_inventory.bindings.end(),[](const auto &b){return b.groups.size()==3;});auto wrapped_variant=prepare_native_image_variant(wrapped,wrapped_archive.textures,wrapped_graph->description,wrapped_binding,replacement,named,cancel);require(wrapped_variant.instance.group==wrapped_variant.prepared.graph->description.group&&wrapped_variant.instance.group!=wrapped_graph->description.group,"Top shader group source layout was not explicitly rebound");
 // Real resource packing must include the exact original Reset asset and
 // deduplicate its shared source image payloads. No workspace/UI is involved.
 const auto project_file=out+"/packed-resources.blend";
 {std::unique_ptr<Main,decltype(&BKE_main_free)> packed_main(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(packed_main.get());auto *scene=BKE_scene_add(packed_main.get(),"Packed native variants");pack_project_assets(*packed_main,*scene,projects);
  ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto clear=qScopeGuard([&]{BKE_reports_free(&reports);});BlendFileWriteParams write{};write.use_save_as_copy=true;require(BLO_write_file(packed_main.get(),project_file.toUtf8().constData(),0,&write,&reports),"Cannot save private packed resources");}

 // Cold source-free open uses exact encoded original/replacement sources and
 // the new manifest. Hiding paths is confined to our generated fixtures.
 auto cold=first.prepared.textures;for(auto &a:cold)if(!a.texture_files.empty())a=packed(a);cold.push_back(original);
 const auto before=first.prepared.asset.bytes;require(QFile::rename(image,image+".offline"),"Cannot hide own source");const auto restore=qScopeGuard([&]{QFile::rename(image+".offline",image);});qputenv("BTS_RESOURCE_CACHE",(out+"/cold-cache").toUtf8());
 std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(reference.get());auto restored=hydrate_native_material_resource(*reference,first.prepared.asset,cancel,cold);require(restored&&first.prepared.asset.bytes==before,"Source-free native variant open changed source bytes");
 {ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto clear=qScopeGuard([&]{BKE_reports_free(&reports);});BlendFileReadReport read_report{};read_report.reports=&reports;
  std::unique_ptr<BlendFileData,decltype(&BLO_blendfiledata_free)> read(BLO_read_from_file(project_file.toUtf8().constData(),BLO_READ_SKIP_USERDEF,&read_report),BLO_blendfiledata_free);require(read&&read->main&&!read->main->scenes.is_empty(),"Cannot open private packed project resources");auto opened=read_project_assets(*static_cast<Scene *>(read->main->scenes.first));const auto variant=std::find_if(opened.begin(),opened.end(),[&](const auto &a){return a.id==retained;});require(variant!=opened.end(),"Packed project lost variant identity");auto reopened=hydrate_native_material_resource(*reference,*variant,cancel,opened);require(reopened&&native_image_variant_provenance(*variant)->original_id==original.id,"Source-free packed Open lost original Reset provenance");}
 require(sha(load(image+".offline"))==original_png&&sha(load(library))==original_library&&original.hash()==original_hash,"Original user-style sources mutated");
 return {{"linked_vector_input_preserved",true},{"top_shader_group_layout_rebound",true},{"nested_color_bindings",4},{"unsupported_normal_binding_rejected",true},{"selected_edges_changed",1},{"unrelated_edges_unchanged",4},{"unchanged_image_uuid_shared",true},{"original_reset_uuid_preserved",true},{"variant_chain_original_preserved",true},{"source_free_cold_open",true},{"packed_resource_save_open",true},{"scalar_ramp_overrides_preserved",true},{"source_unchanged",true},{"original_blend_sha256",original_library},{"original_image_sha256",original_png}};
}
QJsonArray audit_libraries(const QString &manifest){
 QFile file(manifest);require(file.open(QIODevice::ReadOnly),"Cannot open explicit library manifest");const auto entries=QJsonDocument::fromJson(file.readAll()).array();require(!entries.isEmpty(),"Empty explicit library manifest");
 const auto digest=[](const QString &path){QFile file(path);require(file.open(QIODevice::ReadOnly),"Cannot open specified source library");QCryptographicHash hash(QCryptographicHash::Sha256);require(hash.addData(&file),"Cannot checksum specified source library");return QString::fromLatin1(hash.result().toHex());};
 std::map<QString,QString> hashes;for(const auto &entry:entries){const auto path=entry.toObject()["file"].toString();if(!hashes.contains(path))hashes[path]=digest(path);}
 std::atomic_bool cancel=false;QJsonArray result;
 for(const auto &entry:entries){const auto spec=entry.toObject();auto source=read_native_material_source(spec["file"].toString(),spec["material"].toString(),cancel);const auto bindings=native_material_image_bindings(*source.material,cancel);QJsonArray paths;for(const auto &b:bindings.bindings)paths.append(native_image_binding_json(b));
  result.append(QJsonObject{{"material",spec["material"]},{"source_sha256",hashes.at(spec["file"].toString())},{"reachable_edges",qint64(bindings.bindings.size())},{"prepared_usage","unverified"},{"paths",paths},{"diagnostics",QJsonArray::fromStringList(bindings.diagnostics)}});
 }
 for(const auto &[path,before]:hashes)require(digest(path)==before,"Original specified source library changed");return result;
}

}
#ifndef BTS_IMAGE_VARIANT_NO_MAIN
int main(int argc,char **argv){
 QApplication app(argc,argv);std::setlocale(LC_NUMERIC,"C");std::cout<<std::unitbuf;
 try{require(argc==2||argc==3,"Expected private output directory [explicit-library-manifest]");BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");const auto close=qScopeGuard([&]{host.shutdown();});const auto out=QString::fromLocal8Bit(argv[1]);QDir().mkpath(out);auto report=std::async(std::launch::async,[&]{auto result=argc==3&&QString::fromLocal8Bit(argv[2])=="--runtime-baseline"?QJsonObject{{"runtime_baseline",true}}:run(out);if(argc==3&&QString::fromLocal8Bit(argv[2])!="--runtime-baseline")result["actual_library_inventory"]=audit_libraries(QString::fromLocal8Bit(argv[2]));return result;}).get();QFile f(out+"/cpu.json");require(f.open(QIODevice::WriteOnly),"Cannot write CPU proof");f.write(QJsonDocument(report).toJson());if(report.contains("actual_library_inventory")){QFile inventory_file(out+"/actual-inventory.json");require(inventory_file.open(QIODevice::WriteOnly),"Cannot preserve actual-source inventory");inventory_file.write(QJsonDocument(report["actual_library_inventory"].toArray()).toJson());}std::cout<<"NATIVE_IMAGE_VARIANT_CPU_PASS\n";return 0;}catch(const std::exception &e){std::cerr<<"NATIVE_IMAGE_VARIANT_CPU_FAIL "<<e.what()<<'\n';return 1;}
}

#endif
