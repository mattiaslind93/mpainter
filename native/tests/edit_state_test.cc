#include <future>
#include "TextureDocument.hh"
#include "EvaluationScope.hh"
#include "LayerEditDiff.hh"
#include <cassert>
#include <bit>
#include <iostream>
using namespace bts;
static std::uint64_t reference(const PaintPixels &pixels,int channel){std::uint64_t hash=14695981039346656037ULL;const auto add=[&](std::uint32_t value){for(int i=0;i<4;++i){hash^=(value>>(i*8))&255;hash*=1099511628211ULL;}};if(pixels.page_count(channel))add(pixels.resolution());pixels.visit_pages([&](int c,std::uint32_t key,std::span<const PaintPixels::Pixel> page){if(c!=channel)return;add(key);for(const auto &pixel:page)for(float value:pixel)add(std::bit_cast<std::uint32_t>(value));});return hash;}
int main(){
 SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Edit";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"Edit"}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);auto &set=document.set(1);auto &layer=set.layers.front();PaintPixels::Stamp stamp{{.5,.5},{.2,0},{0,.2}};stamp.values.base_color={.2,.3,.7,1};stamp.hardness=1;layer.pixels.stamp(128,stamp);
 const auto bytes=PaintPixels::hash_bytes();const auto identity=document.edit_state();assert(PaintPixels::hash_bytes()==bytes);auto copy=document;assert(copy.edit_state()==identity);copy.set(1).layers.front().visible=false;assert(copy.edit_state()!=identity);copy=document;stamp.values.base_color={.8,.1,.1,1};copy.set(1).layers.front().pixels.stamp(128,stamp);assert(copy.edit_state()!=identity);assert(PaintPixels::hash_bytes()==bytes);
 for(int c=0;c<5;++c){const auto &pixels=layer.pixels.tile(1001);assert(pixels.hash(c)==reference(pixels,c));}const auto warm=PaintPixels::hash_bytes();document.to_json();document.to_json();assert(PaintPixels::hash_bytes()==warm);const auto old=layer.pixels.hash(0);layer.pixels.stamp(128,stamp);assert(layer.pixels.hash(0)!=old);assert(layer.pixels.hash(0)==reference(layer.pixels.tile(1001),0));assert(document.edit_state()==document.edit_state());
 const auto &pixels=layer.pixels.tile(1001);const auto precise=pixels.sample_resampled(0,0,0,1);{PreviewSamplingScope scope;const auto preview=pixels.sample_resampled(0,0,0,1);assert(preview[3]>precise[3]);}assert(pixels.sample_resampled(0,0,0,1)==precise);
 auto empty=document;TextureDocument::add_paint(empty.set(1));auto channels=layer_edit_channels(document.set(1),empty.set(1),edit_set_state(document.edit_state(),set.id),edit_set_state(empty.edit_state(),set.id));for(bool changed:channels)assert(!changed);
 auto invisible_fill=document;TextureDocument::add_fill(invisible_fill.set(1));invisible_fill.set(1).layers.back().channels.fill(false);channels=layer_edit_channels(document.set(1),invisible_fill.set(1),edit_set_state(document.edit_state(),set.id),edit_set_state(invisible_fill.edit_state(),set.id));for(bool changed:channels)assert(!changed);
 auto hidden=document;hidden.set(1).layers.front().visible=false;channels=layer_edit_channels(document.set(1),hidden.set(1),edit_set_state(document.edit_state(),set.id),edit_set_state(hidden.edit_state(),set.id));assert(channels[0]&&channels[4]&&channels[3]);assert(!channels[16]);assert(layer_edit_compatible(edit_set_state(document.edit_state(),set.id),edit_set_state(empty.edit_state(),set.id)));auto resized=empty;resized.set(1).texture_resolution=256;assert(!layer_edit_compatible(edit_set_state(document.edit_state(),set.id),edit_set_state(resized.edit_state(),set.id)));
 std::cout<<"COW edit identities avoid raster hashes; legacy FNV cache invalidation and scoped preview sampling passed\n";
 auto displacement=document;displacement.set(1).displacement.allocated=true;displacement.set(1).displacement.channel=true;
 const auto old_disp=edit_set_state(document.edit_state(),set.id),new_disp=edit_set_state(displacement.edit_state(),set.id);
 assert(layer_edit_compatible(old_disp,new_disp));const auto displacement_channels=layer_edit_channels(document.set(1),displacement.set(1),old_disp,new_disp);
 assert(displacement_channels[16]);for(int c=0;c<16;++c)assert(!displacement_channels[c]);
 {
  PaintPixels versioned;std::vector<PaintPixels::Pixel> page(PaintPixels::page_side*PaintPixels::page_side,{.25f,.25f,.25f,1});versioned.assign_page(128,0,0,page);
  const auto versions=[](const PaintPixels &p){std::map<std::uint32_t,std::uint64_t> result;p.visit_versioned_pages(0,[&](auto key,auto revision,auto){assert(revision);result[key]=revision;});return result;};
  auto shared=versioned;auto a=std::async(std::launch::async,[&]{return versions(versioned);}),b=std::async(std::launch::async,[&]{return versions(shared);});const auto original=a.get();assert(original==b.get()&&original==versions(versioned));
  const auto pixel_hash=versioned.hash(0),hash_work=PaintPixels::hash_bytes();versions(versioned);assert(versioned.hash(0)==pixel_hash&&PaintPixels::hash_bytes()==hash_work);
  PaintPixels::Stamp change{{.1,.1},{.03,0},{0,.03}};change.channels={true,false,false,false,false};change.values.base_color={.8f,.1f,.2f,1};versioned.stamp(128,change);
  assert(versions(versioned)!=original&&versions(shared)==original);versioned=PaintPixels{};versioned.assign_page(128,0,0,page);assert(versions(versioned)!=original);
 }
}
