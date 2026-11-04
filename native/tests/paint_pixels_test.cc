#include "PaintPixels.hh"
#include "TilePaintPixels.hh"
#include "StrokeSpacing.hh"
#include "BrushSettings.hh"
#include "TextureDocument.hh"
#include "PaintRecipe.hh"
#include "EvaluationScope.hh"
#ifdef NDEBUG
#  undef NDEBUG
#endif
#include <cassert>
#include <QJsonArray>
#include <cmath>
#include <cstdio>
#include <stdexcept>
using bts::PaintPixels;
int main() {
  {
    PaintPixels cpu,recorded;PaintPixels::Stamp dab{{.5,.5},{.18,0},{0,.18}};dab.values.roughness=.8f;dab.values.bump=-.3f;dab.flow=.6f;
    cpu.stamp(128,dab);{bts::PaintRecordingScope gpu;recorded.stamp(128,dab);}
    const auto undo=recorded;const auto versions=recorded.page_versions();const auto materializations=PaintPixels::recipe_materializations();
    {bts::PaintRecordingScope gpu;recorded.clear_channel(2);assert(recorded.recipe()&&recorded.recipe()->kind==bts::PaintRecipe::Kind::ClearChannel);assert(recorded.page_count(2)==0);const auto filtered=recorded.page_versions();for(int c:{0,1,3,4}){assert(filtered[c]==versions[c]);for(const auto &[key,version]:filtered[c])assert(version!=recorded.recipe()->revision);}assert(PaintPixels::recipe_materializations()==materializations);}
    cpu.clear_channel(2);assert((recorded.sample(2,64,64)==PaintPixels::Pixel{}));
    for(int c:{0,1,3,4})assert(recorded.sample(c,64,64)==cpu.sample(c,64,64));
    assert(undo.sample(2,64,64)[3]>0&&undo.page_versions()==versions);
    dab.channels={false,false,true,false,false};dab.center={.2,.2};{bts::PaintRecordingScope gpu;recorded.stamp(128,dab);}assert(recorded.sample(2,25,25)[3]>0&&recorded.sample(2,64,64)[3]==0);
    bts::TilePaintPixels tiles;tiles.assign_tile(undo,1001);tiles.assign_tile(undo,1002);const auto before=tiles;
    {bts::PaintRecordingScope gpu;tiles.clear_channel(2);assert(tiles.page_count(2)==0);assert(tiles.tile(1002).page_versions()[4]==before.tile(1002).page_versions()[4]);}
    for(int tile:{1001,1002})assert(tiles.sample(2,64,64,tile)[3]==0&&before.sample(2,64,64,tile)[3]>0);
    for(int c:{0,1,3,4})tiles.clear_channel(c);assert(tiles.tiles().empty());
    std::puts("channel clear: recipe metadata only, retained GPU page identities, exact other channels, immutable Undo, new empty ink and two UDIMs PASS");
  }
  {
    PaintPixels cpu,recorded,base;
    PaintPixels::Stamp stamp{{.45,.5},{.13,.03},{-.04,.17}};
    stamp.values.bump=-.7;stamp.flow=.37;stamp.values.normal={.2,-.3,.9};
    base.stamp(128,stamp);cpu=base;recorded=base;
    const auto count=PaintPixels::recipe_materializations();
    std::vector<PaintPixels> undo,cpu_undo;
    for(int i=0;i<8;++i){
      stamp.center[0]=.2+i*.07;stamp.alpha.kind=bts::AlphaKind(i%5);stamp.alpha.inverted=i%2;stamp.alpha.dynamic.phase=double(i)/8;
      if(i%5==3){auto alpha=std::make_shared<bts::AlphaBitmap>();alpha->width=alpha->height=2;alpha->pixels={.2f,.8f,.7f,.1f};stamp.alpha.bitmap=alpha;}
      cpu_undo.push_back(cpu);cpu.stamp(128,stamp);
      {bts::PaintRecordingScope scope;undo.push_back(recorded);recorded.stamp(128,stamp);assert(recorded.recipe()&&!recorded.empty());assert(recorded.page_count(0)>0);assert(!recorded.page_versions()[4].empty());bts::EditStateScope metadata;assert(recorded.hash(0));}
    }
    assert(PaintPixels::recipe_materializations()==count);
    for(int c=0;c<5;++c)assert(recorded.hash(c)==cpu.hash(c));
    assert(PaintPixels::recipe_materializations()==count+8);
    const auto saved=recorded;PaintPixels::Bounds bounds{0,0,128,128};
    for(bool erase:{false,true})for(bool replace:{false,true}){
      auto expected=base;expected.over_stroke(base,cpu,.61,bounds,erase,replace);
      auto result=base;const auto prior=PaintPixels::recipe_materializations();
      {bts::PaintRecordingScope scope;result.over_stroke(base,recorded,.61,bounds,erase,replace);assert(result.recipe());}
      assert(PaintPixels::recipe_materializations()==prior);
      for(int c=0;c<5;++c)assert(result.hash(c)==expected.hash(c));
    }
    for(int c=0;c<5;++c)assert(saved.hash(c)==cpu.hash(c));
    assert(undo.front().hash(0)==base.hash(0));for(std::size_t i=0;i<undo.size();++i)for(int c=0;c<5;++c)assert(undo[i].hash(c)==cpu_undo[i].hash(c));
    std::puts("paint-recipes: zero pixel work during recording, shared undo, lazy exact CPU materialization and stroke opacity/erase PASS");
  }
  {
    PaintPixels recorded,reference;PaintPixels::Stamp dab{{.5,.5},{.05,0},{0,.05}};
    dab.channels={true,false,false,false,false};dab.flow=.01;
    for(int i=0;i<1024;++i){reference.stamp(16,dab);bts::PaintRecordingScope recording;recorded.stamp(16,dab);}
    const auto before_materialization=PaintPixels::resident_bytes();
    assert(recorded.hash(0)==reference.hash(0));
    assert(PaintPixels::resident_bytes()-before_materialization==64*64*sizeof(PaintPixels::Pixel));
    std::puts("paint-recipes: 1024-dab iterative materialization and release PASS");
  }
  {
    bts::TilePaintPixels stroke,before,result;
    PaintPixels::Stamp stamp{{1,.5},{.2,0},{0,.2},{}};stamp.flow=.2;stamp.hardness=1;
    stamp.channels={true,false,true,false,false};stamp.values.roughness=.3;
    const auto bounds=stroke.stamp(128,stamp,{1001,1002});
    result.over_stroke(before,stroke,.5,bounds);
    assert(std::abs(result.sample(2,127,64,1001)[3]-.1)<1e-6);
    assert(result.sample(2,127,64,1001)==result.sample(2,0,64,1002));
    assert(result.sample(0,127,64,1001)==result.sample(0,0,64,1002));
    assert(result.page_count(1)==0&&result.page_count(3)==0&&result.page_count(4)==0);
    auto undo=result;const auto old_hash=result.hash(2);stamp.channels={true,false,false,false,false};
    result.stamp(128,stamp,{1001,1002});assert(result.hash(2)==old_hash);result=undo;
    auto invalid=result;bool rejected=false;
    try{result.stamp(128,stamp,{1001,2001});}catch(const std::invalid_argument &){rejected=true;}
    assert(rejected&&result.hash(0)==invalid.hash(0));
    std::puts("UDIM-pages: two-tile seam equality, independent channels, opacity, COW and transactional invalid-tile rollback PASS");
  }
  {
    bts::TextureSet set;for(int i=0;i<3;++i)bts::TextureDocument::add_fill(set);
    const auto original=set.layers;const auto id=set.layers[1].id;
    bts::TextureDocument::move_layer_to(set,id,3);
    assert(set.layers[0].id==original[0].id&&set.layers[1].id==original[2].id&&set.layers[2].id==original[3].id&&set.layers[3].id==id);
    bts::TextureDocument::move_layer_to(set,id,0);assert(set.layers[0].id==id&&set.layers[1].id==original[0].id);
    const auto before=set.layers;bool rejected=false;
    try{bts::TextureDocument::move_layer_to(set,id,4);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
    for(int i=0;i<4;++i)assert(set.layers[i].id==before[i].id);
    std::puts("layer-drop: stable block order in both directions and invalid destination preservation PASS");
  }
  assert(PaintPixels::resident_bytes()==0);
  {
    PaintPixels initial;
    PaintPixels::Stamp stamp{{.5,.5},{.1,0},{0,.1},{}};
    stamp.values.base_color={.7f,.2f,.1f,1};stamp.values.roughness=.8f;
    auto painted=initial;auto bounds=painted.stamp(512,stamp);
    assert(!bounds.empty() && bounds.x0>=204 && bounds.x1<=308);
    assert(initial.empty());assert(painted.sample(2,256,256)[0]>.79f);
    assert(painted.sample(2,100,100)==PaintPixels::Pixel{});
    assert(painted.sample(0,256,256)[0]>.69f);
    std::vector<float> rgba(512*512*4);
    for(int c=0;c<5;++c) {
      for(int y=0;y<512;++y)for(int x=0;x<512;++x) {const auto pixel=painted.sample(c,x,y);std::copy(pixel.begin(),pixel.end(),rgba.begin()+(y*512+x)*4);}
      PaintPixels decoded;decoded.assign_channel(512,c,rgba);assert(decoded.hash(c)==painted.hash(c));
      auto before=decoded.hash(c);rgba.back()=NAN;bool rejected=false;
      try{decoded.assign_channel(512,c,rgba);}catch(const std::invalid_argument &){rejected=true;}
      assert(rejected&&decoded.hash(c)==before);
    }
    std::puts("paint-import: all five channels bit-exact roundtrip and invalid late-pixel rollback PASS");
    auto snapshot=painted;
    const auto old_rough=snapshot.hash(2),old_color=snapshot.hash(0);
    const auto shared_bytes=PaintPixels::resident_bytes();
    stamp.channels.fill(false);stamp.channels[1]=true;stamp.values.metallic=1;stamp.center={.7,.7};
    painted.stamp(512,stamp);
    assert(PaintPixels::resident_bytes()>shared_bytes);
    assert(painted.hash(2)==old_rough && painted.hash(0)==old_color);
    assert(painted.hash(1)!=snapshot.hash(1));
    painted=snapshot;assert(painted.hash(1)==snapshot.hash(1)); // Whole-stroke undo snapshot.
    PaintPixels small,large;stamp.channels.fill(true);stamp.hardness=1;stamp.center={.5,.5};
    small.stamp(512,stamp);stamp.axis_x={.3,0};stamp.axis_y={0,.3};large.stamp(512,stamp);
    int a=0,b=0;for(int y=0;y<512;++y)for(int x=0;x<512;++x){a+=small.sample(2,x,y)[3]>0;b+=large.sample(2,x,y)[3]>0;}
    assert(double(b)/a>8.9 && double(b)/a<9.1);
    auto preserved=small.hash(2);stamp.center={1.3,.5};stamp.axis_x={.1,0};stamp.axis_y={0,.1};
    assert(small.stamp(512,stamp).empty() && small.hash(2)==preserved); // No wrapping.
    stamp.center={NAN,0};bool rejected=false;
    try {small.stamp(512,stamp);} catch(const std::invalid_argument &) {rejected=true;}
    assert(rejected && small.hash(2)==preserved);
    std::printf("paint-pages: COW, multichannel writes/undo, toggle retention, diameter area %.4f, clipping and invalid-input preservation PASS\n",double(b)/a);
  }
  {
    PaintPixels pixels;PaintPixels::Stamp stamp{{.5,.5},{.1,0},{0,.1},{}};
    stamp.hardness=1;stamp.flow=.2f;stamp.values.roughness=1;stamp.channels.fill(false);stamp.channels[2]=true;
    pixels.stamp(128,stamp);assert(std::abs(pixels.sample(2,64,64)[0]-.2f)<1e-6);
    for(int i=1;i<10;++i)pixels.stamp(128,stamp);
    assert(std::abs(pixels.sample(2,64,64)[3]-(1-std::pow(.8,10)))<1e-6);
    assert(pixels.page_count(0)==0&&pixels.page_count(1)==0&&pixels.page_count(3)==0&&pixels.page_count(4)==0);
    const auto hash=pixels.hash(2);stamp.flow=0;assert(pixels.stamp(128,stamp).empty());assert(pixels.hash(2)==hash);
    stamp.flow=1;pixels.stamp(128,stamp);assert(pixels.sample(2,64,64)[3]==1);
    std::puts("brush-flow: 20% stamp, ten-stamp accumulation, zero-flow no-op, full flow and disabled channels PASS");
  }
  {
    PaintPixels before,stroke,result;PaintPixels::Stamp stamp{{.5,.5},{.1,0},{0,.1},{}};
    stamp.hardness=1;stamp.values.roughness=1;stamp.values.base_color={.7f,.2f,.1f,1};
    for(int i=0;i<100;++i)stroke.stamp(128,stamp);
    const PaintPixels::Bounds bounds{0,0,128,128};result.over_stroke(before,stroke,.2f,bounds);
    for(int c=0;c<5;++c)assert(std::abs(result.sample(c,64,64)[3]-.2f)<1e-6);
    const auto snapshot=result;const auto hash=result.hash(2);
    assert(result.over_stroke(before,stroke,.2f,bounds).empty()&&result.hash(2)==hash);
    before=result;result.over_stroke(before,stroke,.2f,bounds);
    assert(std::abs(result.sample(2,64,64)[3]-.36f)<1e-6);
    assert(snapshot.hash(2)==hash&&std::abs(snapshot.sample(2,64,64)[3]-.2f)<1e-6);
    assert(result.over_stroke(before,stroke,0,bounds).empty());
    bool rejected=false;try{result.over_stroke(before,stroke,NAN,bounds);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
    // A predicted spline tail can move away from a pixel. Replacement restores
    // the pre-stroke raster there, including erase and an entirely empty tail.
    PaintPixels base,stable,old_path,new_path,live,reference;auto tail=stamp;tail.axis_x={.06,0};tail.axis_y={0,.06};tail.flow=.4;tail.center={.25,.25};stable.stamp(128,tail);old_path=stable;tail.center={.6,.3};old_path.stamp(128,tail);new_path=stable;tail.center={.6,.7};new_path.stamp(128,tail);live.over_stroke(base,old_path,.6,bounds);const auto predicted=live;live.over_stroke(base,new_path,.6,bounds,false,true);reference.over_stroke(base,new_path,.6,bounds);for(int c=0;c<5;++c)assert(live.hash(c)==reference.hash(c));assert(predicted.hash(0)!=live.hash(0));live.over_stroke(base,PaintPixels{},.6,bounds,false,true);assert(live.empty());
    bts::TilePaintPixels atomic_dabs,working_dabs;for(int i=0;i<40;++i){auto brush=tail;brush.center={.2+i*.012,.5};atomic_dabs.stamp(128,brush);working_dabs.stamp_working(128,brush);}for(int c=0;c<5;++c)assert(atomic_dabs.hash(c)==working_dabs.hash(c));const auto checkpoint=working_dabs;auto shifted=tail;shifted.center={.7,.7};working_dabs.stamp_working(128,shifted);assert(checkpoint.hash(0)==atomic_dabs.hash(0));
    bts::TilePaintPixels tiled_base,tiled_live,empty_tail,erase_tail;auto tile_stamp=tail;tile_stamp.center={1.5,.5};tile_stamp.axis_x={2,0};tile_stamp.axis_y={0,2};tile_stamp.flow=1;tile_stamp.hardness=1;tiled_base.stamp(128,tile_stamp,{1002});tiled_live=tiled_base;erase_tail.stamp(128,tile_stamp,{1002});tiled_live.over_stroke(tiled_base,erase_tail,1,bounds,true);assert(tiled_live.empty());tiled_live.over_stroke(tiled_base,empty_tail,1,bounds,true,true);for(int c=0;c<5;++c)assert(tiled_live.hash(c)==tiled_base.hash(c));
    base=reference;live=base;live.over_stroke(base,old_path,.6,bounds,true);live.over_stroke(base,new_path,.6,bounds,true,true);reference=base;reference.over_stroke(base,new_path,.6,bounds,true);for(int c=0;c<5;++c)assert(live.hash(c)==reference.hash(c));
    std::puts("stroke-opacity: five-channel 20% cap under 100 overlaps, idempotent live updates, separate-stroke 36%, COW/zero/invalid PASS");
  }
  {
    bts::StrokeSpacing coarse,fine;coarse.reset({0,0},6);fine.reset({0,0},6);
    std::vector<double> a,b;
    coarse.advance({20,0},[&](auto p){a.push_back(p[0]);return 6.;});
    for(int x=1;x<=20;++x)fine.advance({double(x),0},[&](auto p){b.push_back(p[0]);return 6.;});
    assert(a.size()==3&&a==b&&a[0]==6&&a[1]==12&&a[2]==18);
    bts::StrokeSpacing adaptive;adaptive.reset({0,0},2);std::vector<double> points;
    adaptive.advance({20,0},[&](auto p){points.push_back(p[0]);return p[0]<10?2.:5.;});
    assert((points==std::vector<double>{2,4,6,8,10,15,20}));
    bool rejected=false;try{adaptive.advance({NAN,0},[](auto){return 1.;});}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
    std::puts("stroke-spacing: identical coarse/fine event samples, carried remainder, adaptive diameter and invalid input PASS");
  }
  {
    bts::BrushSettings brush;
    assert(brush.pressure_size&&!brush.pressure_flow&&brush.minimum_size==5&&brush.minimum_flow==5);
    assert(std::abs(brush.diameter_at(.25f)-2.875f)<1e-6&&brush.flow_at(.25f)==100);
    brush.pressure_size=false;brush.pressure_flow=true;
    assert(brush.diameter_at(.25f)==10&&std::abs(brush.flow_at(.25f)-28.75f)<1e-6);
    brush.pressure_size=true;assert(brush.diameter_at(1)==10&&brush.flow_at(1)==100);
    brush.minimum_size=20;brush.minimum_flow=10;
    assert(std::abs(brush.diameter_at(0)-2)<1e-6&&std::abs(brush.flow_at(0)-10)<1e-6);
    std::puts("pressure-mapping: independent size/flow toggles, retained minimums, bounded linear response and full-pressure mouse equivalence PASS (software only)");
  }
  {
    auto footprint=std::make_shared<PaintPixels::SurfaceFootprint>();footprint->center={0,0,0};footprint->radius=.5;
    PaintPixels::SurfaceTriangle a,b;
    a.uv={{{0,0},{1,0},{1,1}}};a.position={{{-1,-1,0},{1,-1,0},{1,1,0}}};
    b.uv={{{0,0},{1,1},{0,1}}};b.position={{{-1,-1,0},{1,1,0},{-1,1,0}}};
    footprint->triangles={a,b,a}; // Repeated/shared triangles must not apply flow twice.
    PaintPixels::Stamp stamp{{.5,.5},{1,0},{0,1},{}};stamp.surface=footprint;stamp.flow=.2f;stamp.hardness=1;stamp.values.roughness=1;
    PaintPixels pixels;pixels.stamp(128,stamp);
    for(int c=0;c<5;++c)assert(std::abs(pixels.sample(c,64,64)[3]-.2f)<1e-6);
    assert(pixels.sample(2,2,2)[3]==0&&pixels.sample(2,120,64)[3]==0);
    auto far=std::make_shared<PaintPixels::SurfaceFootprint>(*footprint);
    for(auto &triangle:far->triangles)for(auto &vertex:triangle.position)vertex[2]=2;
    stamp.surface=far;PaintPixels witness;assert(witness.stamp(128,stamp).empty()&&witness.empty());
    auto seam=std::make_shared<PaintPixels::SurfaceFootprint>(*footprint);seam->center={-1,0,0};seam->radius=.2;
    for(auto &triangle:seam->triangles){for(auto &uv:triangle.uv)for(double &v:uv)v=.2+.6*v;triangle.seam_edges={true,true,true};}
    stamp.surface=seam;PaintPixels padded;padded.stamp(128,stamp);
    assert(padded.sample(2,24,64)[3]>.19f&&padded.sample(2,21,64)[3]==0);
    std::puts("surface-raster: world-space radius, all channels, shared-edge single flow, far witness rejection and bounded seam padding PASS");
  }
  {
    bts::SceneSnapshot scene;
    bts::MaterialSummary material;material.session_id=1;material.name="A";material.uniform_editable=true;
    scene.materials.push_back(material);material.session_id=2;material.name="B";scene.materials.push_back(material);
    bts::ImportSummary imported;imported.materials={{1,"A"},{2,"B"}};
    bts::NewProjectOptions settings;settings.resolution=bts::DocumentResolution::Pixels128;
    bts::TextureDocument document(settings,imported,scene);
    auto &set=document.set(1);bts::TextureDocument::add_fill(set);set.layers.back().values.base_color={.5,.5,.5,1};bts::TextureDocument::add_paint(set);
    auto &paint=bts::TextureDocument::layer(set,set.selected_layer);
    PaintPixels::Stamp stamp{{.5,.5},{.1,0},{0,.1},{}};stamp.hardness=1;
    stamp.values.roughness=.8f;stamp.values.base_color={1,0,0,1};
    paint.pixels.stamp(128,stamp);paint.opacity.fill(.5);
    auto undo=document;
    const auto composite=bts::TextureDocument::composite(set,64,64);
    assert(std::abs(composite.material.roughness-.55f)<1e-6);
    assert(composite.coverage[2]==1);
    assert(std::abs(composite.material.base_color[0]-.75f)<1e-6);
    assert(std::abs(composite.material.base_color[1]-.25f)<1e-6);
    assert(std::abs(bts::TextureDocument::composite(set,0,0).material.roughness-.3f)<1e-6);
    const auto original_hash=paint.pixels.hash(2);
    paint.visible=false;
    assert(std::abs(bts::TextureDocument::composite(set,64,64).material.roughness-.3f)<1e-6);
    paint.visible=true;set.channels[2]=false;
    assert(std::abs(bts::TextureDocument::composite(set,64,64).material.roughness-.3f)<1e-6);
    assert(paint.pixels.hash(2)==original_hash);
    assert(!bts::TextureDocument::has_pixels(document.set(2)));
    document=undo;
    const auto json=document.to_json();std::vector<bts::TextureSetBinding> bindings;
    for(const auto &set:document.texture_sets())bindings.push_back({set.id,set.material_session_uid,set.name});
    const auto loaded=bts::TextureDocument::from_json(json,bindings);
    assert(loaded.texture_sets()[0].id==document.texture_sets()[0].id);
    assert(loaded.texture_sets()[0].layers[2].id==document.texture_sets()[0].layers[2].id);
    assert(loaded.texture_sets()[0].layers[2].opacity==document.texture_sets()[0].layers[2].opacity);
    // Opacity belongs to each channel; changing Height cannot change authored Color or Roughness.
    auto independent=document;
    auto &independent_set=independent.set(1);
    auto &independent_paint=bts::TextureDocument::layer(independent_set,independent_set.selected_layer);
    independent_paint.opacity[4]=.2;
    independent_paint.opacity[2]=0;
    auto evaluated=bts::TextureDocument::composite(independent_set,64,64);
    assert(evaluated.material.base_color==composite.material.base_color);
    assert(std::abs(evaluated.material.roughness-.3f)<1e-6);
    assert(independent_paint.pixels.hash(2)==original_hash);
    // Migrate old scalar metadata to every channel without changing its evaluation.
    auto legacy=json;legacy["schema"]=2;auto old_sets=legacy["texture_sets"].toArray();
    for(int i=0;i<old_sets.size();++i) {
      auto old_set=old_sets[i].toObject();auto layers=old_set["layers"].toArray();
      for(int j=0;j<layers.size();++j){auto l=layers[j].toObject();l["opacity"]=l["channel_opacity"].toArray()[0];l.remove("channel_opacity");layers[j]=l;}
      old_set["layers"]=layers;old_sets[i]=old_set;
    }
    legacy["texture_sets"]=old_sets;
    const auto migrated=bts::TextureDocument::from_json(legacy,bindings);
    assert(migrated.to_json()==loaded.to_json());
    auto bad_opacity=json;auto bad_sets=bad_opacity["texture_sets"].toArray();auto bad_set=bad_sets[0].toObject();
    auto bad_layers=bad_set["layers"].toArray();auto bad_layer=bad_layers[0].toObject();bad_layer["channel_opacity"]=QJsonArray{1,1,1,1,1.1};
    bad_layers[0]=bad_layer;bad_set["layers"]=bad_layers;bad_sets[0]=bad_set;bad_opacity["texture_sets"]=bad_sets;
    bool invalid_opacity=false;try{bts::TextureDocument::from_json(bad_opacity,bindings);}catch(const std::invalid_argument &){invalid_opacity=true;}assert(invalid_opacity);
    std::puts("channel-opacity: independent composition, retained source pixels, legacy scalar migration and range rejection PASS");
    auto broken=json;broken["schema"]=999;bool rejected=false;
    try{bts::TextureDocument::from_json(broken,bindings);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
    bindings[1].material_uid=bindings[0].material_uid;rejected=false;
    try{bts::TextureDocument::from_json(json,bindings);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
    std::puts("project-metadata: persistent set/layer IDs retained; unsupported schema and duplicate material binding rejected PASS");
    assert(std::abs(bts::TextureDocument::composite(document.set(1),64,64).material.roughness-.55f)<1e-6);
    std::puts("paint-composite: fill/paint order, independent coverage, opacity, channel definition retention, set isolation and document undo PASS");
  }
  {
    bts::SceneSnapshot scene;bts::MaterialSummary material;material.session_id=1;material.name="Folder test";material.uniform_editable=true;scene.materials.push_back(material);
    bts::ImportSummary imported;imported.materials={{1,"Folder test"}};bts::TextureDocument document({},imported,scene);
    auto &set=document.set(1);bts::TextureDocument::add_fill(set);set.layers.back().values.roughness=.2;
    bts::TextureDocument::add_fill(set);set.layers.back().values.roughness=.8;const auto child=set.selected_layer;
    bts::TextureDocument::group_layers(set,{child});const auto group=set.selected_layer;
    assert(set.layers.back().kind==bts::LayerKind::Folder&&set.layers[2].parent==group&&set.layers[2].depth==1);
    assert(std::abs(bts::TextureDocument::output(set).roughness-.8)<1e-6);
    set.layers.back().opacity[2]=.5;assert(std::abs(bts::TextureDocument::output(set).roughness-.5)<1e-6);
    set.layers.back().visible=false;assert(std::abs(bts::TextureDocument::output(set).roughness-.2)<1e-6);set.layers.back().visible=true;
    bts::TextureDocument::group_layers(set,{group});assert(set.layers[2].depth==2&&std::abs(bts::TextureDocument::output(set).roughness-.5)<1e-6);
    bool rejected=false;try{bts::TextureDocument::move_layer_relative(set,set.selected_layer,child,true);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
    bts::TextureDocument::add_folder(set);const auto empty=set.selected_layer;const auto count=set.layers.size();bts::TextureDocument::add_paint(set);
    assert(set.layers.size()==count+1&&set.layers.back().parent.isNull()&&set.layers[count-1].id==empty);
    auto broken=set;broken.layers[2].parent=broken.layers[2].id;rejected=false;
    try{bts::TextureDocument::validate_hierarchy(broken);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
    const std::vector<bts::TextureSetBinding> bindings{{set.id,1,"Folder test"}};
    const auto loaded=bts::TextureDocument::from_json(document.to_json(),bindings);assert(loaded.to_json()==document.to_json());
    auto corrupt=document.to_json();auto sets=corrupt["texture_sets"].toArray();auto object=sets[0].toObject();auto layers=object["layers"].toArray();
    auto cyclic=layers[2].toObject();cyclic["parent"]=cyclic["id"];layers[2]=cyclic;object["layers"]=layers;sets[0]=object;corrupt["texture_sets"]=sets;
    rejected=false;try{bts::TextureDocument::from_json(corrupt,bindings);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
    std::puts("layer-folders: nested composition, group opacity/visibility, stable child IDs, empty-folder insertion and cycle rejection PASS");
    auto stacked=set;const auto kept=stacked.layers.front().id;const auto removed_fill=stacked.layers[1].id;
    bts::TextureDocument::add_mask(stacked,0);const auto masked=stacked.selected_layer;auto &masked_layer=bts::TextureDocument::layer(stacked,masked);bts::LevelsEffect levels;levels.channel=0;masked_layer.effects.push_back(levels);const auto effect=masked_layer.effects.back().id;masked_layer.mask->effects.push_back({});const auto mask_effect=masked_layer.mask->effects.back().id;
    bts::TextureDocument::remove_effects(stacked,{{masked,effect},{masked,mask_effect}});assert(bts::TextureDocument::layer(stacked,masked).effects.empty()&&bts::TextureDocument::layer(stacked,masked).mask->effects.empty());
    bts::TextureDocument::remove_masks(stacked,{masked});assert(!bts::TextureDocument::layer(stacked,masked).mask&&!stacked.selected_mask);
    bts::TextureDocument::delete_layers(stacked,{group,removed_fill});assert(std::none_of(stacked.layers.begin(),stacked.layers.end(),[&](const auto &layer){return layer.id==group||layer.id==child||layer.id==removed_fill;}));assert(stacked.layers.front().id==kept);
    std::vector<QUuid> rest;for(const auto &layer:stacked.layers)if(layer.id!=kept)rest.push_back(layer.id);bts::TextureDocument::delete_layers(stacked,rest);assert(stacked.layers.size()==1&&stacked.layers.front().id==kept);
    rejected=false;try{bts::TextureDocument::delete_layers(stacked,{kept});}catch(const std::invalid_argument &){rejected=true;}assert(rejected&&stacked.layers.size()==1);
    std::puts("layer-delete: effect, mask, folder subtree and last-layer rejection PASS");
  }
  {
    bts::TextureSet set;bts::TextureDocument::add_fill(set);const auto fill=set.selected_layer;
    bts::TextureDocument::add_paint(set);const auto paint=set.selected_layer;set.layers.back().visible=false;
    bts::TextureDocument::group_layers(set,{paint,fill});const auto folder=set.selected_layer;
    assert(set.layers[1].id==fill&&set.layers[2].id==paint&&!set.layers[2].visible);
    assert(set.layers[1].parent==folder&&set.layers[2].parent==folder);
    bts::TextureDocument::group_layers(set,{fill,folder,paint});
    assert(set.layers.size()==5&&set.layers[1].depth==2&&set.layers[2].depth==2&&set.layers[3].depth==1);
    std::puts("group-selection: selected sibling order/visibility, descendant de-duplication and nested grouping PASS");
  }
  {
    bts::TextureSet set;set.texture_resolution=128;bts::TextureDocument::add_fill(set);set.layers.back().values.roughness=.3;set.layers.back().values.base_color={.5,.5,.5,1};
    bts::TextureDocument::add_fill(set);auto &top=set.layers.back();top.values.roughness=.5;top.values.metallic=1;
    for(auto [mode,value]:{std::pair{bts::LayerBlend::Normal,.5f},{bts::LayerBlend::Multiply,.15f},{bts::LayerBlend::Screen,.65f},{bts::LayerBlend::Disabled,.3f}}) {
      top.blending[2]=mode;const auto result=bts::TextureDocument::composite(set);assert(std::abs(result.material.roughness-value)<1e-6);assert(result.material.metallic==1&&top.values.roughness==.5f);
    }
    for(int i=0;i<=100;++i) {
      top.values.roughness=i/100.f;
      for(auto mode:{bts::LayerBlend::Multiply,bts::LayerBlend::Screen})for(float opacity:{0.f,.25f,.75f,1.f}) {
        top.blending[2]=mode;top.opacity[2]=opacity;
        const double source=i/100.;const double expected=mode==bts::LayerBlend::Multiply?.3*source:1-(1-.3)*(1-source);
        assert(std::abs(bts::TextureDocument::output(set).roughness-(.3*(1-opacity)+expected*opacity))<1e-6);
      }
    }
    top.values.base_color={.5,.5,.5,1};top.blending[0]=bts::LayerBlend::Screen;
    const auto linear=bts::TextureDocument::output(set).base_color[0];assert(std::abs(linear-.75)<1e-6);
    const auto encode=[](double value){return value<=.0031308?12.92*value:1.055*std::pow(value,1/2.4)-.055;};
    assert(std::lround(encode(linear)*255)==225);assert(std::lround((1-std::pow(1-188/255.,2))*255)==237);
    bts::TextureDocument::add_paint(set);auto &paint=set.layers.back();
    PaintPixels::Stamp stamp{{.5,.5},{.1,0},{0,.1},{}};stamp.hardness=1;stamp.flow=.5;stamp.values.roughness=1;stamp.channels={false,false,true,false,false};paint.pixels.stamp(128,stamp);
    const auto hash=paint.pixels.hash(2);paint.blending[2]=bts::LayerBlend::Replace;
    assert(bts::TextureDocument::composite(set,0,0).coverage[2]==0);
    const auto replacement=bts::TextureDocument::composite(set,64,64);assert(replacement.coverage[2]==.5f&&replacement.authored.roughness==1);
    assert(replacement.coverage[0]==1);paint.blending[2]=bts::LayerBlend::Normal;assert(bts::TextureDocument::composite(set,0,0).coverage[2]==1);
    paint.blending[2]=bts::LayerBlend::Disabled;assert(paint.pixels.hash(2)==hash);
    // With no backdrop, Multiply must retain source color and its coverage.
    bts::TextureSet transparent;transparent.texture_resolution=128;transparent.layers[0]=paint;transparent.layers[0].blending[2]=bts::LayerBlend::Multiply;
    const auto alone=bts::TextureDocument::composite(transparent,64,64);assert(alone.coverage[2]==.5f&&alone.authored.roughness==1);
    std::puts("layer-blending: scalar ramps/opacity, independent Disable, linear Screen 225 vs encoded 237, Replace transparency and alpha-aware Multiply PASS");
  }
  {
    bts::SceneSnapshot scene;bts::MaterialSummary material;material.session_id=1;material.name="Mask test";material.uniform_editable=true;scene.materials.push_back(material);
    bts::ImportSummary imported;imported.materials={{1,"Mask test"}};bts::NewProjectOptions settings;settings.resolution=bts::DocumentResolution::Pixels128;bts::TextureDocument document(settings,imported,scene);
    auto &set=document.set(1);bts::TextureDocument::add_fill(set);set.layers.back().values.roughness=.2;set.layers.back().values.metallic=0;
    bts::TextureDocument::add_fill(set);auto &top=set.layers.back();top.values.roughness=.8;top.values.metallic=1;
    bts::TextureDocument::add_mask(set,0);assert(set.selected_mask&&bts::TextureDocument::output(set).roughness==.2f);
    PaintPixels::Stamp stamp{{.5,.5},{.1,0},{0,.1},{}};stamp.hardness=1;stamp.flow=.5;stamp.channels={false,false,true,false,false};stamp.values.roughness=1;
    top.mask->pixels.stamp(128,stamp);assert(bts::TextureDocument::has_pixels(set));
    auto value=bts::TextureDocument::composite(set,64,64);assert(std::abs(value.material.roughness-.5)<1e-6&&value.material.metallic==.5f);
    assert(bts::TextureDocument::composite(set,0,0).material.metallic==0);
    top.mask->enabled=false;assert(bts::TextureDocument::composite(set,0,0).material.metallic==1);top.mask->enabled=true;
    top.blending[2]=bts::LayerBlend::Replace;
    assert(std::abs(bts::TextureDocument::composite(set,0,0).material.roughness-.2)<1e-6); // Black mask also bypasses Replace's clearing.
    assert(std::abs(bts::TextureDocument::composite(set,64,64).material.roughness-.5)<1e-6);
    top.mask->pixels={};const auto metadata=document.to_json();std::vector<bts::TextureSetBinding> bindings{{set.id,1,"Mask test"}};
    assert(bts::TextureDocument::from_json(metadata,bindings).to_json()==metadata);
    bts::TextureDocument::add_paint(set);assert(!set.selected_mask);
    std::puts("mask-composite: shared multichannel coverage, retained bypass, masked Replace, target selection and schema roundtrip PASS");
  }
  assert(PaintPixels::resident_bytes()==0);
}
