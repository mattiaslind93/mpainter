// SPDX-License-Identifier: GPL-3.0-or-later
#include "LayerChannelCommand.hh"
#include "PaintRecording.hh"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>

using namespace bts;
int main()
{
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Channels";material.uniform_editable=true;scene.materials.push_back(material);
  ImportSummary imported;imported.materials={{1,"Channels",{1001,1002}}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;
  TextureDocument document(options,imported,scene);auto &set=document.set(1);set.tiles={1001,1002};
  const auto id=set.selected_layer;
  for(const auto &layer:set.layers){for(int c:{0,1,2,4})assert(layer_channel_enabled(layer,c));assert(!layer_channel_enabled(layer,14));}assert(!set.displacement.channel);
  PaintPixels::Stamp dab{{.5,.5},{.2,0},{0,.2}};dab.values.roughness=.9;dab.values.bump=-.3;
  auto &layer=TextureDocument::layer(set,id);
  {PaintRecordingScope gpu;for(double u:{.5,1.5}){dab.center[0]=u;layer.pixels.stamp(128,dab,{1001,1002});}}
  auto history=std::make_shared<StrokeHistory>();history->baseline=std::make_shared<StrokeRaster>();
  auto operation=std::make_shared<StrokeOperation>();operation->ink=std::make_shared<StrokeRaster>(StrokeRaster{layer.pixels,{}});operation->bounds={0,0,128,128};history->operations.push_back(operation);history->final_pixels=operation->ink;layer.strokes=history;
  const auto undo=document;
  const auto versions=layer.pixels.tile(1002).page_versions();const auto materializations=PaintPixels::recipe_materializations();
  {PaintRecordingScope gpu;assert(set_layer_channel(set,id,2,false));assert(!layer_channel_enabled(layer,2));assert(layer.pixels.page_count(2)==0);assert(layer.pixels.tile(1002).page_versions()[4]==versions[4]);assert(layer.strokes->operations.front()->ink->paint.page_count(2)==0);assert(PaintPixels::recipe_materializations()==materializations);}
  for(int tile:{1001,1002})assert(layer.pixels.sample(2,64,64,tile)[3]==0&&undo.texture_sets().front().layers.front().pixels.sample(2,64,64,tile)[3]>0);
  assert(set_layer_channel(set,id,2,true)&&layer.pixels.page_count(2)==0);
  auto texture=std::make_shared<FillTexture>();texture->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();texture->hash=std::string(64,'a');texture->name="shared texture";texture->raw=true;texture->width=texture->height=1;texture->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(1,FillTexture::Pixel{.2f,.2f,.2f,1});
  fill_channel_texture(layer.fill,1)=texture;fill_channel_texture(layer.fill,4)=texture;const auto shared_before=layer.fill;
  assert(set_layer_channel(set,id,1,false));assert(!fill_channel_texture(layer.fill,1)&&fill_channel_texture(layer.fill,4)==texture&&fill_channel_texture(shared_before,1)==texture);
  TextureDocument::add_paint(set);const auto other=set.selected_layer;
  assert(set_layer_channel(set,id,14,true));assert(set.displacement.channel&&TextureDocument::layer(set,id).displacement.enabled&&!TextureDocument::layer(set,other).displacement.enabled);
  auto &displacement=TextureDocument::layer(set,id).displacement;displacement.value=-.7;PaintPixels::Stamp scalar=dab;scalar.channels={false,false,true,false,false};for(double u:{.5,1.5}){scalar.center[0]=u;displacement.pixels.stamp(128,scalar,{1001,1002});}
  assert(set_layer_channel(set,id,14,false));assert(displacement.pixels.empty()&&displacement.value==set.displacement.midlevel);
  assert(set_layer_channel(set,id,14,true)&&displacement.pixels.empty()&&displacement.value==set.displacement.midlevel);
  TextureDocument::add_paint(set);assert(!TextureDocument::layer(set,set.selected_layer).displacement.enabled);TextureDocument::add_fill(set);assert(!TextureDocument::layer(set,set.selected_layer).displacement.enabled);
  const auto saved=document.to_json();const std::array bindings{TextureSetBinding{set.id,1,"Channels"}};auto opened=TextureDocument::from_json(saved,bindings);const auto &restored=opened.texture_sets().front();assert(restored.layers.size()==set.layers.size());for(std::size_t i=0;i<set.layers.size();++i){const auto &before=set.layers[i],&after=restored.layers[i];assert(before.channels==after.channels&&before.displacement.enabled==after.displacement.enabled&&before.displacement.value==after.displacement.value);assert(bool(fill_channel_texture(before.fill,4))==bool(fill_channel_texture(after.fill,4)));}
  // A deferred history rejects deletion atomically, including channel existence.
  auto unloaded=std::make_shared<StrokeHistory>();unloaded->loaded=false;auto &target=TextureDocument::layer(set,id);target.strokes=unloaded;const auto before=target.pixels.tile(1001).page_versions();
  const auto original_channels=target.channels;bool rejected=false;try{set_layer_channel(set,id,0,false);}catch(const std::runtime_error &){rejected=true;}assert(rejected&&target.channels==original_channels&&target.pixels.tile(1001).page_versions()==before);
  std::puts("layer channels: defaults, zero CPU evaluation, channel/UDIM isolation, immutable Undo, shared textures, empty re-enable, displacement ownership and metadata roundtrip PASS");
}
