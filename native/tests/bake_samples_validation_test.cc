// SPDX-License-Identifier: GPL-3.0-or-later
#include "BakeQuality.hh"
#include "ProjectTemplate.hh"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
using namespace bts;
template<class F> void rejects(F f){bool failed=false;try{f();}catch(const std::invalid_argument &){failed=true;}assert(failed);}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);QTemporaryDir output;assert(output.isValid());
 SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="BL validation";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,material.name}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument doc(options,imported,scene);
 const auto id=doc.set(1).id;const std::array bindings{TextureSetBinding{id,1,material.name}};
 const QByteArray hdr="BL pure-parser environment fixture, not a decoded HDR";
 const QJsonObject resource{{"name","owned.hdr"},{"data",QString::fromLatin1(hdr.toBase64())},{"sha256",QString::fromLatin1(QCryptographicHash::hash(hdr,QCryptographicHash::Sha256).toHex())}};
 int cases=0;
 for(int rays:{1,16,17,128,256,511,512})for(int aa:{1,2,4,8}){
  QJsonObject recipe{{"resolution",32},{"height",32},{"locked",true},{"rays",rays},{"sampling",aa},{"dilation",7},{"diffusion",true},{"project_mesh",true},{"frontal",.01},{"rear",.02},{"relative",true},{"average",false},{"ignore_backface",true},{"enabled",QJsonArray{false,false,false,true,false,false,true}}};
  recipe=with_bake_quality(recipe,id,3,{{"rays",rays},{"sampling",aa},{"dilation",3},{"diffusion",false}});
  recipe=with_bake_quality(recipe,id,6,{{"rays",rays},{"sampling",aa},{"dilation",9},{"diffusion",true}});
  validate_bake_quality({{"rays",rays},{"sampling",aa}});doc.set_bake_configuration(recipe);
  const auto encoded=QJsonDocument(doc.to_json()).toJson(QJsonDocument::Compact);
  QSaveFile file(output.path()+"/document.json");assert(file.open(QIODevice::WriteOnly));assert(file.write(encoded)==encoded.size()&&file.commit());QFile saved(file.fileName());assert(saved.open(QIODevice::ReadOnly));
  const auto restored=TextureDocument::from_json(QJsonDocument::fromJson(saved.readAll()).object(),bindings);assert(restored.texture_sets().front().id==id&&restored.bake_configuration()==recipe);
  for(int role:{0,3,6}){const auto q=bake_quality(restored.bake_configuration(),id,role);assert(q["rays"].toInt()==rays&&q["sampling"].toInt()==aa);}
  const auto templ=make_project_template(doc,1,{{"view_transform","Standard"}},{{"rotation",0},{"exposure",0},{"opacity",100},{"blur",0},{"alignment","World"}},resource,recipe);
  write_project_template(output.path()+"/bake.btst",templ);const auto loaded=read_project_template(output.path()+"/bake.btst");assert(loaded==templ&&loaded["baking"].toObject()==recipe);
  for(int bad_rays:{0,513}){auto bad=recipe;bad["rays"]=bad_rays;rejects([&]{doc.set_bake_configuration(bad);});assert(doc.bake_configuration()==recipe);auto bad_json=doc.to_json();bad_json["bake_configuration"]=bad;rejects([&]{TextureDocument::from_json(bad_json,bindings);});auto bad_template=templ;bad_template["baking"]=bad;rejects([&]{validate_project_template(bad_template);});rejects([&]{validate_bake_quality({{"rays",bad_rays}});});}
  auto bad=recipe;bad["sampling"]=3;rejects([&]{doc.set_bake_configuration(bad);});assert(doc.bake_configuration()==recipe);auto bad_template=templ;bad_template["baking"]=bad;rejects([&]{validate_project_template(bad_template);});rejects([&]{validate_bake_quality({{"sampling",3}});});
  ++cases;
 }
 std::printf("BL CPU metadata: %d exact-ray/AA cases;17/511/512 retained through actual TextureDocument JSON+disk and ProjectTemplate file codec;0/513/AA3 atomic rejection PASS. Native packed Open and dialogs pending.\n",cases);
}
