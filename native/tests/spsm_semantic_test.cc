#include "SpsmSemantic.hh"
#include "../host/ResourceAvailability.hh"
#include <bit>
#include <iostream>
#include <sstream>
#include <set>
using namespace bts::spsm;
using namespace bts::spsm::semantic;
namespace {
void require(bool yes,const char *message){if(!yes)throw std::runtime_error(message);}
struct Builder {
 Preset preset;
 template<class T> NodeId add(T value,std::uint8_t tag=18){const auto id=preset.nodes.size();preset.nodes.push_back({tag,0,1,std::move(value)});return id;}
 NodeId text(std::string s){return add(std::move(s),16);}
 NodeId integer(std::uint64_t n){return add(n,21);}
 NodeId boolean(bool b){return add(b,10);}
 NodeId floating(float f){FloatValues v;v.count=1;v.values[0]=f;return add(v,1);}
 NodeId object(std::string name,std::initializer_list<Field> fields){return add(Object{std::move(name),fields});}
 NodeId array(std::vector<NodeId> children){return add(Array{std::move(children)},17);}
 NodeId stack(std::string name,std::vector<NodeId> children){return object(std::move(name),{{"items",array(std::move(children))}});}
 NodeId blending(std::uint64_t bits){return object("DataBlending",{{"channelTypes",integer(bits)},{"blendingMode",integer(9001)},{"opacity",floating(.37f)}});}
 NodeId fill(std::string label){
  const auto uniform=object("DataSourceUniform",{{"channelTypes",integer(1|128)},{"uid",integer(UINT64_MAX)},{"color",floating(.25f)},{"opacity",floating(.43f)}});
  return object("DataActionFill",{{"label",text(std::move(label))},{"uid",integer(UINT64_MAX-1)},{"enabled",boolean(true)},{"channelTypes",integer(1|128)},{"sources",array({uniform})},{"projection",integer(777)},{"uvSamplingWrap",integer(888)},{"dddWrap",integer(999)},{"perChannelBlending",array({blending(1),blending(128)})}});
 }
 Archive archive(NodeId root,std::string dataset="/preset.bin"){preset.root=root;preset.dataset=std::move(dataset);Archive result;result.source="synthetic.spsm";result.presets.push_back(std::move(preset));return result;}
};
bool has(const PresetInspection &p,std::string_view code){return std::any_of(p.findings.begin(),p.findings.end(),[&](const auto &f){return f.code==code;});}
Archive fixture(){
 Builder b;
 const auto bitmap=b.object("DataBitmap",{{"urlToBitmapRes",b.text("resourcealias:Curvature")},{"colorSpace",b.integer(12345)},{"alphaType",b.integer(54321)}});
 const auto source=b.object("DataSourceBitmap",{{"channelTypes",b.integer(0)},{"bitmap",bitmap}});
 const auto mask_fill=b.object("DataActionFill",{{"sources",b.array({source})},{"label",b.text("Mask fill")}});
 const auto tweak=b.object("DataTweakFloat",{{"uid",b.integer(UINT64_MAX)},{"identifier",b.text("UnknownTweak")},{"value",b.floating(std::bit_cast<float>(0x7fc01234u))}});
 const auto proc=b.object("DataProcedural",{{"urlToSbsRes",b.text("/mg_mask_builder/mg_mask_builder?version=opaque.opaque")},{"tweaks",b.array({tweak})},{"sourcesInputs",b.array({})},{"outputs",b.array({})}});
 const auto filter=b.object("DataActionFilterProcedural",{{"procedural",proc},{"label",b.text("Unimplemented")}});
 const auto group=b.object("DataActionGroup",{{"subStack",b.stack("DataStackActions",{mask_fill,filter})},{"label",b.text("Nested mask")},{"perChannelBlending",b.array({b.blending(1)})}});
 const auto unknown=b.object("DataActionFuture",{{"label",b.text("Do not drop")},{"opaque",b.add(Blob{0,2},15)}});
 const auto layer=b.object("DataLayerColor",{{"label",b.text("Dirt")},{"uid",b.integer(UINT64_MAX)},{"enabled",b.boolean(false)},{"maskEnabled",b.boolean(true)},{"maskInitial",b.integer(0)},{"actions",b.stack("DataStackActions",{b.fill("first"),b.fill("second"),unknown})},{"maskActions",b.stack("DataStackActions",{group})},{"perChannelBlending",b.array({b.blending(1|128)})},{"futureGeometry",b.text(std::string("keep\0all",8))}});
 const auto folder=b.object("DataLayerGroup",{{"label",b.text("Folder")},{"subStack",b.stack("DataStackLayers",{layer})}});
 const auto root=b.object("DataLayersPreset",{{"layerGroup",folder},{"preset_dataversion",b.integer(80)}});
 b.preset.encoded={7,9};auto result=b.archive(root);result.references={{0,bitmap,"urlToBitmapRes","resourcealias:Curvature",{}},{0,proc,"urlToSbsRes","/mg_mask_builder/mg_mask_builder?version=opaque.opaque","/resources/opaque.opaque"}};result.datasets={{"/resources/opaque.opaque",16ULL*1024*1024*1024}};return result;
}
void synthetic(){
 const auto bits=1ULL|128|4194304|(1ULL<<63);const auto mapped=inspect_channels(bits);require(mapped.original_bits==bits&&mapped.unknown_bits==(1ULL<<63)&&mapped.mappings.size()==3,"64-bit source mask changed");require(inspect_channels(0).auxiliary&&inspect_channels(0).mappings.empty(),"zero auxiliary mask misclassified");
 const auto inspected=inspect_archive(fixture());const auto &p=inspected.presets.at(0);require(p.layers.size()==2&&p.actions.size()==6&&p.sources.size()==3,"semantic item counts differ");
 const auto &folder=p.layers.at(p.roots.at(0));require(folder.group&&folder.children.size()==1,"folder tree lost");const auto &layer=p.layers.at(folder.children[0]);require(layer.parent==p.roots[0]&&layer.actions.size()==3&&layer.mask_actions.size()==1,"source layer order/group ownership lost");
 require(std::get<std::string>(source_value(inspected,0,*p.actions[layer.actions[0]].label).value)=="first"&&std::get<std::string>(source_value(inspected,0,*p.actions[layer.actions[1]].label).value)=="second","multiple fill order collapsed");
 require(std::get<std::uint64_t>(source_value(inspected,0,*layer.uid).value)==UINT64_MAX,"UID was narrowed");require(!std::get<bool>(source_value(inspected,0,*layer.enabled).value),"disabled flag was changed");
 const auto &group=p.actions[layer.mask_actions[0]];require(group.kind==ActionKind::Group&&group.children.size()==2&&p.actions[group.children[0]].kind==ActionKind::Fill&&p.actions[group.children[1]].kind==ActionKind::ProceduralFilter,"nested mask action order lost");
 require(!p.native_import_available&&!p.rendered&&has(p,"unknown-class")&&has(p,"multiple-content-fills")&&has(p,"nested-action-group")&&has(p,"procedural-unimplemented")&&has(p,"source-blend-enum"),"missing capability gaps");
 const auto &fill=p.actions[layer.actions[0]];require(fill.blending.size()==2&&fill.projection.size()==3,"blend/projection fields lost");require(std::get<std::uint64_t>(source_value(inspected,0,*fill.blending[0].source_mode).value)==9001,"source blend enum was mapped/cast");require(std::get<std::uint64_t>(source_value(inspected,0,source_field(inspected,0,fill.projection[0]).value).value)==777,"source projection enum changed");
 require(!has(p,"image-hydration"),"destination-map alias misreported as an embedded image");require(p.resources.size()==2&&p.resources[0].destination_map==DestinationMeshMap::Curvature&&p.resources[1].state==ResourceBinding::State::EmbeddedDescriptor,"resource identities/alias binding lost");require(inspected.source().datasets[0].size==16ULL*1024*1024*1024,"lazy resource descriptor changed");
 const auto tweak=p.procedurals[0].tweaks[0];const auto *value=inspected.source().presets[0].field(tweak,"value");require(value&&std::bit_cast<std::uint32_t>(std::get<FloatValues>(value->value).values[0])==0x7fc01234u,"unknown tweak/NaN payload lost");
 const auto path=source_path(inspected,0,p.actions[group.children[1]].source);require(path=="$[\"layerGroup\"][\"subStack\"][\"items\"][0][\"maskActions\"][\"items\"][0][\"subStack\"][\"items\"][1]","source path is not exact");
 const auto copied=inspected;require(&copied.source()==&inspected.source(),"immutable source ownership was copied/detached");require(source_path(copied,0,p.actions[group.children[1]].source)==path,"inspection copy contains dangling source views");std::ostringstream report;write_report(report,copied);require(report.str().find("\"native_import_available\":false")!=std::string::npos&&report.str().find("opaque.opaque")!=std::string::npos,"honest report/resource provenance missing");
 auto variants=fixture();auto variant=variants.presets[0];variant.dataset="/preset_22.bin";variant.wire_version=22;variants.presets.push_back(std::move(variant));const auto separate=inspect_archive(std::move(variants));require(separate.presets.size()==2&&separate.presets[0].layers.size()==2&&separate.presets[1].layers.size()==2&&separate.primary_preset==0,"variant trees were concatenated/selected implicitly");
 bool denied=false;try{inspect_archive(fixture(),{});}catch(const Error &){denied=true;}require(!denied,"low worker budget accepted");auto low=fixture();{bts::TextureRamQueryScope ram([]()->std::uint64_t{return 32;});denied=false;try{inspect_archive(std::move(low),{});}catch(const Error &){denied=true;}require(!denied,"explicit budget bypassed actual RAM");}
 auto invalid=fixture();invalid.presets[0].nodes[invalid.presets[0].root].value=Array{{invalid.presets[0].root}};denied=false;try{inspect_archive(std::move(invalid));}catch(const Error &){denied=true;}require(denied,"cyclic source arena accepted");
 Builder deep;auto leaf=deep.object("DataLayerColor",{});const auto original=leaf;for(unsigned i=0;i<20000;++i)leaf=deep.object("DataLayerGroup",{{"subStack",deep.stack("DataStackLayers",{leaf})}});const auto root=deep.object("DataLayersPreset",{{"layerGroup",leaf}});const auto nested=inspect_archive(deep.archive(root));require(nested.presets[0].layers.size()==20001&&source_path(nested,0,original).size()>20000,"deep semantic inspection/path was capped or recursive");
 std::cout<<"PASS semantic: ordered nested layers/actions/masks, unknown ops/fields, raw enums, UINT64 channels/UID, exact paths, variants, lazy resources, RAM and 20000 nested groups\n";
}
void user_files(const std::filesystem::path &directory){
 const char *names[]={"dirty-plastic-robot.spsm","shd-hard-rust.spsm","sratched-metal-pealing-paint-dirt.spsm","used_varnish_wood.spsm","worn leather chest.spsm"};const std::size_t layers[]={5,4,7,26,10};
 for(std::size_t fi=0;fi<std::size(names);++fi){auto archive=read_archive(directory/names[fi]);const auto bytes=archive.payload_bytes_read;std::uint64_t preset_bytes=0;for(const auto &p:archive.presets)preset_bytes+=p.encoded.size();require(bytes==preset_bytes,"structural reader hydrated resource data");const auto inspection=inspect_archive(std::move(archive));require(inspection.primary_preset.has_value(),"primary preset stream not designated");const auto &p=inspection.presets.at(*inspection.primary_preset);require(p.layers.size()==layers[fi],"actual source hierarchy count mismatch");require(!p.native_import_available&&!p.rendered&&has(p,"procedural-unimplemented"),"user source falsely reported renderable");require(has(p,"image-hydration")==bool(fi>=3),"embedded images confused with mesh-map aliases");if(fi==0){require(inspection.presets.size()==3,"Dirty Plastic variants lost");require(has(p,"multiple-content-fills"),"actual multiple content fills missing");}if(fi==2)require(has(p,"source-paint-semantics"),"actual 94 paint actions not identified");std::ostringstream report;write_report(report,inspection);require(report.str().find("\"resource_payloads_read\":false")!=std::string::npos,"resource IO report incorrect");std::cout<<"PASS "<<names[fi]<<" streams="<<inspection.presets.size()<<" primary_layers="<<p.layers.size()<<" actions="<<p.actions.size()<<" findings="<<p.findings.size()<<'\n';}
}
}
int main(int argc,char **argv){try{synthetic();if(argc>1)user_files(argv[1]);return 0;}catch(const std::exception &e){std::cerr<<"SPSM semantic test failed: "<<e.what()<<'\n';return 1;}}
