// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureSetResidency.hh"
#include "HistoryPageStorage.hh"
#include "TextureResidencyMemory.hh"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <cstdio>
using namespace bts;
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);QTemporaryDir cache;assert(cache.isValid());qputenv("BTS_RECOVERY_DIR",cache.path().toUtf8());
 SceneSnapshot scene;ImportSummary imported;
 for(unsigned uid:{1,2}){MaterialSummary m;m.session_id=uid;m.name="Set "+std::to_string(uid);m.uniform_editable=true;scene.materials.push_back(m);imported.materials.push_back({uid,m.name,{1001,1002}});}
 NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;TextureDocument doc(options,imported,scene);
 auto &set=doc.set(1);auto &other=doc.set(2);
 PaintPixels::Stamp stamp{{.5,.5},{.4,0},{0,.4}};stamp.channels={true,false,false,false,false};stamp.values.base_color={.25f,.75f,.5f,1};stamp.hardness=1;
 auto &paint=set.layers.front().pixels;paint.stamp_working(128,stamp,{1001});stamp.center={1.5,.5};paint.stamp_working(128,stamp,{1002});
 const auto hash=paint.hash(0);const auto sample=paint.sample(0,64,64,1002);auto undo=doc;
 auto texture=std::make_shared<FillTexture>();texture->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();texture->name="Exact float payload";texture->hash=std::string(64,'a');texture->width=texture->height=32;
 auto vector=std::make_shared<const std::vector<FillTexture::Pixel>>(1024,FillTexture::Pixel{-.25f,1.75f,.5f,.3f});std::weak_ptr<const std::vector<FillTexture::Pixel>> weak=vector;texture->pixels=vector;vector.reset();
 other.layers.front().fill.texture=texture;
 auto alias=*texture;assert(alias.pixels.identity()==texture->pixels.identity());
 other.loaded=false;set.unloaded_tiles.insert(1002);
 const auto resident=[](){return history_mapping_rss();};
 const auto before=resident();assert(before>0);
 spill_unloaded_textures(doc);
 assert(texture->pixels.resident_bytes()==0&&alias.pixels.resident_bytes()==0&&weak.expired());
 const auto after=resident();assert(after<before);
 assert(paint.hash(0)==hash&&paint.sample(0,64,64,1002)==sample&&undo.set(1).layers.front().pixels.sample(0,64,64,1002)==sample);
 assert((*alias.pixels)[0]==(FillTexture::Pixel{-.25f,1.75f,.5f,.3f}));
 assert(texture->pixels.get()==alias.pixels.get());
 const auto view=resident_texture_view(set);assert(set.width(1002)==128&&view.width(1002)==1&&view.width(1001)==128);
 assert(set.loaded_tiles()==std::set<int>{1001});
 // Serialization preserves choices and the original export dimensions.
 const std::array bindings{TextureSetBinding{set.id,1,set.name},TextureSetBinding{other.id,2,other.name}};
 const auto saved=doc.to_json();const auto restored=TextureDocument::from_json(saved,bindings);assert(restored.texture_sets()[0].unloaded_tiles==set.unloaded_tiles&&!restored.texture_sets()[1].loaded);assert(restored.texture_sets()[0].width(1002)==128);
 auto legacy=saved;auto sets=legacy["texture_sets"].toArray();for(int i=0;i<sets.size();++i){auto o=sets[i].toObject();o.remove("loaded");o.remove("unloaded_tiles");sets[i]=o;}legacy["texture_sets"]=sets;const auto old=TextureDocument::from_json(legacy,bindings);assert(old.texture_sets()[0].fully_loaded()&&old.texture_sets()[1].fully_loaded());
 // No cache write failure may discard the only decoded copy.
 QTemporaryFile blocker(cache.path()+"/file-XXXXXX");assert(blocker.open());qputenv("BTS_RECOVERY_DIR",blocker.fileName().toUtf8());
 DiskPixelPayload<FillTexture::Pixel> unsafe(std::make_shared<const std::vector<FillTexture::Pixel>>(16,FillTexture::Pixel{1,2,3,4}));bool rejected=false;try{unsafe.spill();}catch(...){rejected=true;}assert(rejected&&unsafe.resident_bytes()==16*sizeof(FillTexture::Pixel)&&(*unsafe)[0][2]==3);
 std::printf("RESIDENCY PASS: paint RAM %zu -> %zu; decoded pixels expired; exact reload/undo; disk failure retained; project choices/export dimensions preserved; maximum texture 128\n",before,after);
}
