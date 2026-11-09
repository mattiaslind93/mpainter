// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialBundle.hh"
#include <QCoreApplication>
#include <iostream>
using namespace bts;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception &){bad=true;}require(bad,"Expected strict metadata rejection");}
}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);
 try{
  NativeImageBinding b{{{101,"Outer"},{202,"Inner"}},303,404,"Color","Color1","ShaderNodeMixRGB","Original",
   {{"image_source",1},{"image_type",0},{"alpha_mode",0},{"high_precision",true},{"multiview",false},{"source_space","sRGB"},{"interpolation",1},{"projection",0},{"projection_blend",0.},{"extension",0}}};
  require(read_native_image_binding(native_image_binding_json(b))==b,"Binding path roundtrip changed");
  NativeImageVariantProvenance p{QUuid::createUuid(),QString(64,'a'),{{b,QString(64,'b'),"sRGB","original-declared"}}};
  const auto blend=QByteArray("BLENDER-test-original");const auto encoded=native_material_bundle_bytes(blend,{},p);const auto restored=read_native_material_bundle(encoded);require(restored.blend==blend&&restored.external,"Bundle did not preserve exact original blend bytes");
  const auto previewed=native_material_bundle_bytes(blend,{},p,"preview-png");require(read_native_material_bundle(previewed).preview_png=="preview-png"&&native_material_bundle_preview(previewed)=="preview-png","Variant bundle lost preview");require(native_material_bundle_provenance(previewed)->original_id==p.original_id,"Preview changed variant Reset lineage");
  const auto lineage=native_material_bundle_provenance(encoded);require(lineage&&lineage->original_id==p.original_id&&lineage->operations.front().binding==b,"Provenance changed source identity");
  require(!native_material_bundle_provenance(native_material_bundle_bytes(blend,{}))&&!native_material_bundle_provenance(blend),"Legacy raw/v1 source invented Reset lineage");
  for(const auto &value:QJsonArray{0,-1,2147483648.,.5}){auto bad=native_image_binding_json(b);bad["node"]=value;rejects([&]{read_native_image_binding(bad);});}
  auto bad=native_image_binding_json(b);bad["unknown"]=true;rejects([&]{read_native_image_binding(bad);});bad=native_image_binding_json(b);bad["image"]=QString::fromUtf8("bad\0image",9);rejects([&]{read_native_image_binding(bad);});
  auto settings=b.source_settings;settings["projection_blend"]=1e100;bad=native_image_binding_json(b);bad["settings"]=settings;rejects([&]{read_native_image_binding(bad);});
  auto meta=native_image_variant_provenance_json(p);meta["original_id"]=QUuid{}.toString();rejects([&]{read_native_image_variant_provenance(meta);});meta=native_image_variant_provenance_json(p);meta["version"]=2;rejects([&]{read_native_image_variant_provenance(meta);});auto wrong=p;wrong.operations[0].source_space="Non-Color";rejects([&]{native_image_variant_provenance_json(wrong);});wrong=p;wrong.operations[0].precision_policy="inherited-half";rejects([&]{native_image_variant_provenance_json(wrong);});wrong=p;wrong.operations[0].source_space_policy="guess";rejects([&]{native_image_variant_provenance_json(wrong);});
  // Path/count storage is dynamically admitted, rather than capped at a parser
  // depth or a small fixed number of bindings/operations.
  auto many=b;for(int i=0;i<4097;++i)many.groups.push_back({i+1,"Nested"});require(read_native_image_binding(native_image_binding_json(many))==many,"Large native path hit an arbitrary count cap");
  auto ops=p;for(int i=0;i<65;++i)ops.operations.push_back(p.operations.front());require(read_native_image_variant_provenance(native_image_variant_provenance_json(ops)).operations.size()==66,"Operations hit arbitrary count cap");
  const auto binding_json=native_image_binding_json(b);
  {TextureRamQueryScope low([]()->std::uint64_t{return 0;});([&]{read_native_image_binding(binding_json);})();([&]{read_native_material_bundle(encoded);})();}
  auto truncated=encoded;truncated.chop(1);rejects([&]{read_native_material_bundle(truncated);});auto overflow=encoded;char length[8];qToBigEndian<quint64>(~quint64(0),length);std::copy_n(length,8,overflow.data()+8);rejects([&]{read_native_material_bundle(overflow);});
  std::cout<<"NATIVE_IMAGE_VARIANT_METADATA_PASS\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
