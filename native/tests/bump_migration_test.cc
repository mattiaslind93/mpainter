// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include <QJsonArray>
#include <cassert>
#include <iostream>
using namespace bts;
namespace {
QJsonValue legacy(QJsonValue value){
  if(value.isArray()){auto array=value.toArray();for(int i=0;i<array.size();++i)array[i]=legacy(array[i]);return array;}
  if(!value.isObject())return value;
  auto object=value.toObject();for(const auto &key:object.keys())object[key]=legacy(object[key]);
  if(object.contains("bump")){object["height"]=object.take("bump");}
  if(object.contains("bump_method")){object["height_method"]=object.take("bump_method");}
  return object;
}
}
int main(){
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="LegacyHeight";material.uniform_editable=true;material.values.bump=-.17;scene.materials.push_back(material);
  ImportSummary imported;imported.materials={{1,"LegacyHeight"}};NewProjectOptions settings;settings.resolution=DocumentResolution::Pixels128;TextureDocument document(settings,imported,scene);
  auto &set=document.set(1);set.bump_method=BumpMethod::Sharp;TextureDocument::add_fill(set);TextureDocument::edit_values(set).bump=.7;
  TextureDocument::add_paint(set);auto &paint=TextureDocument::layer(set,set.selected_layer);paint.author_resolution=128;PaintPixels::Stamp dab{{.5,.5},{.15,0},{0,.15}};dab.hardness=1;dab.values.bump=-.2;paint.pixels.stamp(128,dab);paint.opacity[4]=.65;TextureDocument::add_mask(set,.45);
  for(auto &layer:set.layers)layer.displacement.enabled=false;
  const auto canonical=document.to_json();assert(canonical["schema"].toInt()==bts::texture_document_schema);
  auto old=legacy(canonical).toObject();old["schema"]=45;const auto legacy_set=old["texture_sets"].toArray()[0].toObject();assert(legacy_set.contains("height_method"));assert(legacy_set["base_values"].toObject().contains("height"));assert(!legacy_set["base_values"].toObject().contains("bump"));
  const std::array<TextureSetBinding,1> bindings{{{set.id,1,set.name}}};auto restored=TextureDocument::from_json(old,bindings);
  // Packed native images still use the original five channel indices, including scalar slot 4.
  for(int i=0;i<int(set.layers.size());++i){restored.set(1).layers[i].pixels=set.layers[i].pixels;if(set.layers[i].mask)restored.set(1).layers[i].mask->pixels=set.layers[i].mask->pixels;}
  assert(restored.to_json()==canonical);assert(restored.set(1).base_values.bump==set.base_values.bump);
  assert(restored.set(1).layers.back().pixels.hash(4)==paint.pixels.hash(4));
  const auto before=TextureDocument::composite(set,64,64),after=TextureDocument::composite(restored.set(1),64,64);assert(before.material==after.material&&before.coverage==after.coverage);
  std::cout<<"Schema45 Height and method migrate to schema46 Bump; values, masks, flags, channel4 hashes and composition retained\n";
}
