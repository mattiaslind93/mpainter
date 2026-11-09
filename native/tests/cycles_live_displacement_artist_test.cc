// SPDX-License-Identifier: GPL-3.0-or-later
#include "CyclesLiveDisplacementArtist.hh"
#include "CyclesLiveDisplacementProbe.hh"
// Complete Qt headers precede Blender/AT/GPU headers (no QtWidgets umbrella).
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScopeGuard>
#include <QTimer>
#include <QToolButton>
#include "../host/NumericLocale.hh"
#include "../host/GpuProjectReadback.hh"
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "BKE_image.hh"
#include "BKE_image_hosted.hh"
#include "BKE_image_gpu_override.hh"
#include "BKE_node.hh"
#include "BLI_listbase.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "IMB_imbuf_types.hh"
#include "GPU_texture.hh"
#include "../host/ViewportDemoCapture.hh"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
extern "C" unsigned long long BTS_cycles_graph_count();
extern "C" unsigned long long BTS_cycles_svm_count();
namespace bts {namespace {
using blender::G;
using blender::ID_Type; // DNA's GS macro uses this native enum unqualified.
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
void save_json(const QString &path,const QJsonObject &value){QFile file(path);require(file.open(QIODevice::WriteOnly),"Cannot retain displacement evidence");const auto bytes=QJsonDocument(value).toJson();require(file.write(bytes)==bytes.size(),"Short displacement evidence write");}
QString digest(const QByteArrayView bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
struct PaintEpoch {
 std::shared_ptr<CyclesLiveDisplacementProbe> observer;
 std::uint64_t begin_generation=0,begin_epoch=0,draw_generation=0,draw_epoch=0;
 bool failed=false;
};
class PaintFilter final:public QObject {
 std::shared_ptr<PaintEpoch> state;
 public:PaintFilter(QWidget *viewport,std::shared_ptr<PaintEpoch> value):QObject(viewport),state(std::move(value)){viewport->installEventFilter(this);}
 bool eventFilter(QObject *,QEvent *event)override {
  if(event->type()==QEvent::Paint&&state->observer){try{const auto s=state->observer->snapshot();state->begin_generation=s.generation;state->begin_epoch=s.terminal_epoch==s.epoch&&s.sample==128&&s.progress==1?s.epoch:0;}catch(...){state->failed=true;}}
  return false;
 }
};
class DisplacementArtist final:public QObject {
 QWidget *window,*viewport;QString output,mesh;BlenderHost *host;CyclesFixtureEdit edit;
 std::function<QJsonObject()> read;QTimer timer;QElapsedTimer deadline;
 std::shared_ptr<PaintEpoch> paint=std::make_shared<PaintEpoch>();
 std::shared_ptr<CyclesLiveDisplacementProbe> observer=std::make_shared<CyclesLiveDisplacementProbe>();
 std::unique_ptr<GpuProjectReadback> readback;
 std::uint32_t uid=0;int stage=0,history=0,painting_frame=0;double offset=0;
 std::uint64_t wanted_generation=0,wanted_epoch=0,draw_requested_epoch=0;
 unsigned long long graph=0,svm=0;bool cleanup_failed=false;
 QJsonObject original_source,original_handles,original_displacement,first_geometry,edited_geometry;
 QJsonValue original_view;QString profile;QJsonArray captures;
 QJsonObject tile_hashes;QString readback_stage;std::uint64_t readback_revision=0;
 template<class T>T *get(const char *name){auto *value=window->findChild<T *>(name);if(!value)throw std::runtime_error(std::string("Missing Displacement actor control: ")+name);return value;}
 void action(const char *name){get<QAction>(name)->trigger();}
 QJsonObject state(bool metadata=false){const auto old=window->property("guiProbeOnly");const bool had_flag=qEnvironmentVariableIsSet("BTS_LAYER_DROP_DEMO");const auto old_flag=qgetenv("BTS_LAYER_DROP_DEMO");const auto restore=qScopeGuard([&]{window->setProperty("guiProbeOnly",old);if(metadata){if(had_flag)qputenv("BTS_LAYER_DROP_DEMO",old_flag);else qunsetenv("BTS_LAYER_DROP_DEMO");}});window->setProperty("guiProbeOnly",!metadata);if(metadata)qputenv("BTS_LAYER_DROP_DEMO","1");return read();}
 void dump_probe(){save_json(output+"/completion-trace.json",QJsonDocument::fromJson(QByteArray::fromStdString(observer->trace_json())).object());}
 void cleanup()noexcept {
  try{if(readback){host->with_gpu([&]{readback->clear();});readback.reset();}}catch(...){qFatal("Displacement test GPU readback could not retire before host shutdown");}
  try{observer->restore();}catch(...){cleanup_failed=true;}
  try{dump_probe();}catch(...){cleanup_failed=true;}
 }
 blender::Image *image(){
  for(blender::Material &material:G_MAIN->materials)if(material.id.session_uid==uid){
   auto *node=material.nodetree?blender::bke::node_find_node_by_name(*material.nodetree,"__BTS_PixelsDisp"):nullptr;
   require(node&&node->id&&GS(node->id->name)==blender::ID_IM,"Missing actual shader-bound role16 image");return reinterpret_cast<blender::Image *>(node->id);
  }throw std::runtime_error("Missing Displacement fixture material");
 }
 QJsonObject handles(){QJsonObject result;for(blender::Image &native:G_MAIN->images)if(native.runtime&&(native.runtime->gpuflag&blender::IMA_HOSTED_GPU_OVERRIDE)){QJsonArray pointers;for(int i=0;i<blender::TEXTARGET_COUNT;++i)pointers.append(QString::number(quintptr(native.runtime->gputexture[i][0]),16));result[QString::number(native.id.session_uid)]=pointers;}return result;}
 QJsonObject source_state(){return host->displacement_preview_state();}
 static QJsonObject original_mesh_fields(const QJsonObject &state){QJsonObject result;for(const auto item:state["objects"].toArray()){const auto object=item.toObject();result[object["object"].toString()]=QJsonObject{{"original_mesh_hash",object["original_mesh_hash"]},{"source_uv_hash",object["source_uv_hash"]},{"source_geometry_hash",object["source_geometry_hash"]},{"source_vertices",object["source_vertices"]},{"level",object["level"]},{"preview_modifiers",object["preview_modifiers"]}};}QJsonArray materials;for(const auto item:state["materials"].toArray()){const auto material=item.toObject();materials.append(QJsonObject{{"material",material["material"]},{"method",material["method"]},{"evaluated_method",material["evaluated_method"]},{"subdivision_level",material["subdivision_level"]},{"inflate_bounds",material["inflate_bounds"]},{"content",material["content"]},{"linked",material["linked"]}});}result["_materials"]=materials;return result;}
 QJsonObject displacement_state(){const auto sets=state(true)["document"].toObject()["texture_sets"].toArray();require(sets.size()==1,"Displacement fixture must commit exactly one Texture Set");return sets[0].toObject()["displacement"].toObject();}
 void write_mesh(){
  // 64x32 localXY quads, +Z normals, U=0..2, V=0..1. Keep the
  // pinned OBJ importer rotation (+90degX), never reset the object. The
  // undeformed positions/UVs are specified here, not inferred from Cycles.
  QByteArray text="o BA_Displacement_Grid\ns off\n";
  for(int y=0;y<=32;++y)for(int x=0;x<=64;++x)text+="v "+QByteArray::number(x/32.-1,'g',17)+" "+QByteArray::number(y/32.-.5,'g',17)+" 0\n";
  for(int y=0;y<=32;++y)for(int x=0;x<=64;++x)text+="vt "+QByteArray::number(x/32.,'g',17)+" "+QByteArray::number(y/32.,'g',17)+"\n";
  for(int y=0;y<32;++y)for(int x=0;x<64;++x){const int a=y*65+x+1,b=a+1,d=a+65,c=d+1;text+="f ";for(int v:{a,b,c,d})text+=QByteArray::number(v)+"/"+QByteArray::number(v)+" ";text+='\n';}
  mesh=output+"/own-two-tile-grid.obj";QFile file(mesh);require(file.open(QIODevice::WriteOnly)&&file.write(text)==text.size(),"Cannot create independent dense plane fixture");save_json(output+"/fixture-source.json",{{"mesh_sha256",digest(text)},{"quads",2048},{"vertices",2145},{"uv_u",QJsonArray{0,2}},{"uv_v",QJsonArray{0,1}},{"object_transform_rows",QJsonArray{QJsonArray{1,0,0,0},QJsonArray{0,0,-1,0},QJsonArray{0,1,0,0}}},{"displacement_pattern","R=-.25 for frac(U)<.5, otherwise+.25; alpha1; nearest; periodic"},{"strength",.5},{"midlevel",0},{"subdivision",0},{"offsets",QJsonArray{0,.5}}});
 }
 void fixture(){bool called=false;edit([&](TextureSet &set){
  require(set.tiles==std::set<int>({1001,1002})&&set.texture_resolution==128,"Wrong two-tile Displacement fixture extent");uid=set.material_session_uid;
  TextureDocument::add_displacement_channel(set);set.displacement.strength=.5f;set.displacement.midlevel=0;set.displacement.subdivision=0;set.displacement.mode=0;set.displacement.precision=ChannelPrecision::L32F;
  TextureDocument::add_fill(set);auto layer=std::move(TextureDocument::layer(set,set.selected_layer));set.layers.clear();layer.parent={};layer.depth=0;layer.name="BA signed periodic Displacement";layer.channels={false,false,false,false,false};layer.ao_enabled=false;layer.displacement.enabled=true;layer.displacement.opacity=1;layer.displacement.blending=LayerBlend::Normal;
  auto texture=std::make_shared<FillTexture>();texture->asset="638dddf7-591a-4bb5-b250-c2b69845ba82";texture->name="Own periodic signed Displacement";texture->width=texture->height=128;texture->raw=true;texture->floating_point=true;texture->data_units=true;texture->range_valid=true;texture->minimum=-.25f;texture->maximum=.25f;
  auto pixels=std::make_shared<std::vector<FillTexture::Pixel>>(128*128);for(int y=0;y<128;++y)for(int x=0;x<128;++x){const float r=x<64?-.25f:.25f;(*pixels)[y*128+x]={r,r,r,1};}texture->pixels=pixels;texture->hash=digest(QByteArrayView(reinterpret_cast<const char *>(pixels->data()),qsizetype(pixels->size()*sizeof(FillTexture::Pixel)))).toStdString();texture->validate();
  fill_channel_texture(layer.fill,14)=texture;layer.fill.alpha_mask[14]=false;layer.fill.filtering=2;layer.fill.wrap=0;layer.fill.depth_culling=false;set.layers.push_back(std::move(layer));set.selected_mask=false;set.selected_effect={};called=true;
 });require(called,"Artist fixture edit callback did not execute");const auto committed=state(true);save_json(output+"/fixture-committed.json",committed);const auto sets=committed["document"].toObject()["texture_sets"].toArray();require(sets.size()==1&&sets[0].toObject()["layers"].toArray().size()==1&&sets[0].toObject()["layers"].toArray()[0].toObject()["name"].toString()=="BA signed periodic Displacement","Actual artist edit rejected Displacement fixture");
 }
 void arm(bool entry){const auto now=observer->snapshot();wanted_generation=now.generation+(entry?1:0);wanted_epoch=entry?1:now.epoch+1;draw_requested_epoch=0;if(entry)observer->arm_entry();observer->arm_geometry_capture(wanted_generation,wanted_epoch);}
 bool completed_draw(){const auto now=observer->snapshot();require(now.error.empty(),now.error.c_str());require(!paint->failed,"Real Paint epoch recording failed");require(now.generation<=wanted_generation,"Unexpected Cycles Session replacement");if(now.generation!=wanted_generation||now.epoch<wanted_epoch||now.terminal_epoch!=now.epoch||now.sample!=128||now.progress!=1||now.status.find("Rendering Done")==std::string::npos)return false;
  if(draw_requested_epoch!=now.epoch){draw_requested_epoch=now.epoch;viewport->update();return false;}return paint->draw_generation==now.generation&&paint->draw_epoch==now.epoch&&!observer->geometry_json().empty();
 }
 float expected_r(double u)const{u+=offset;u-=std::floor(u);return u<.5?-.25f:.25f;}
 const std::array<std::array<int,2>,6> points{{{16,32},{32,96},{48,32},{80,96},{96,32},{112,96}}};
 QJsonArray samples(bool check){QJsonArray all;for(int tile:{1001,1002}){
  const auto gpu=host->diagnostic_gpu_pixels(uid,16,points,tile);require(gpu.size()==points.size(),"Missing role16 GPU samples");blender::ImageUser user{};user.tile=tile;auto *native=image();auto *buffer=blender::BKE_image_acquire_ibuf(native,&user,nullptr);struct Release{blender::Image *image;blender::ImBuf *buffer;~Release(){if(buffer)blender::BKE_image_release_ibuf(image,buffer,nullptr);}} release{native,buffer};require(buffer&&buffer->float_data()&&buffer->channels==4&&buffer->x==128&&buffer->y==128,"Missing role16 float32 CPU mirror");
  QJsonArray records;for(std::size_t i=0;i<points.size();++i){const auto [x,y]=points[i];const float wanted=expected_r((x+.5)/128.);QJsonArray actual,mirror;for(int c=0;c<4;++c){const float cpu=buffer->float_data()[(y*128+x)*4+c];actual.append(gpu[i][c]);mirror.append(cpu);if(check){require(std::isfinite(gpu[i][c])&&std::isfinite(cpu)&&cpu==gpu[i][c],"Displacement CPU/GPU mirror differs");require(std::abs(gpu[i][c]-(c==3?1:wanted))<=3e-5,"Independent signed role16 texture formula differs");}}records.append(QJsonObject{{"xy",QJsonArray{x,y}},{"expected",wanted},{"gpu",actual},{"cpu",mirror}});}all.append(QJsonObject{{"tile",tile},{"samples",records}});
 }return all;}
 bool current(){for(int tile:{1001,1002}){const auto gpu=host->diagnostic_gpu_pixels(uid,16,points,tile);if(gpu.size()!=points.size())return false;auto *native=image();blender::ImageUser user{};user.tile=tile;auto *buffer=blender::BKE_image_acquire_ibuf(native,&user,nullptr);bool matches=buffer&&buffer->float_data()&&buffer->x==128&&buffer->y==128;
  if(matches)for(std::size_t i=0;i<points.size();++i){const auto [x,y]=points[i];const float expected=expected_r((x+.5)/128.);matches&=std::isfinite(gpu[i][0])&&std::abs(gpu[i][0]-expected)<=3e-5&&buffer->float_data()[(y*128+x)*4]==gpu[i][0];}
  if(buffer)blender::BKE_image_release_ibuf(native,buffer,nullptr);if(!matches)return false;
 }return true;}
 bool full_tiles(const QString &name){if(tile_hashes.contains(name))return true;auto *native=image();
  if(!readback){readback_stage=name;readback_revision=native->runtime->update_count;readback=std::make_unique<GpuProjectReadback>();host->with_gpu([&]{for(int tile:{1001,1002}){const auto gpu=blender::BKE_image_gpu_override_texture(native);blender::ImageGpuTileRegion region;require(gpu.texture&&*gpu.texture&&blender::BKE_image_gpu_override_tile_region(native,tile,region)&&region.width==128&&region.height==128,"Wrong role16 GPU tile descriptor");require(blender::GPU_texture_format(*gpu.texture)==blender::gpu::TextureFormat::SFLOAT_32_32_32_32,"Role16 is not actual float32");readback->submit(16,tile,*gpu.texture,region.x,region.y,region.layer,128,128);}});return false;}
  require(name==readback_stage,"Diagnostic role16 stage changed during readback");bool ready=false;host->with_gpu([&]{ready=readback->poll();});if(!ready)return false;QJsonArray hashes;bool exact=true;require(readback_revision==std::uint64_t(native->runtime->update_count),"Role16 revision changed during full-tile evidence");
  for(const auto &gpu:readback->images()){require(!gpu.uniform&&gpu.bytes()==128ULL*128*16,"Wrong full role16 readback shape");blender::ImageUser user{};user.tile=gpu.tile;auto *buffer=blender::BKE_image_acquire_ibuf(native,&user,nullptr);struct Release{blender::Image *image;blender::ImBuf *buffer;~Release(){if(buffer)blender::BKE_image_release_ibuf(image,buffer,nullptr);}} release{native,buffer};require(buffer&&buffer->float_data()&&buffer->x==128&&buffer->y==128&&buffer->channels==4,"Missing full role16 CPU mirror");const QByteArrayView raw(reinterpret_cast<const char *>(gpu.pixels),qsizetype(gpu.bytes())),cpu(reinterpret_cast<const char *>(buffer->float_data()),qsizetype(gpu.bytes()));for(const auto &[suffix,bytes]:{std::pair{QString("gpu"),raw},std::pair{QString("cpu"),cpu}}){QFile file(output+"/"+name+QString("-%1-%2.rgba32f").arg(gpu.tile).arg(suffix));require(file.open(QIODevice::WriteOnly)&&file.write(bytes.data(),bytes.size())==bytes.size(),"Cannot retain raw role16 evidence");}hashes.append(QJsonObject{{"tile",gpu.tile},{"gpu_sha256",digest(raw)},{"cpu_sha256",digest(cpu)},{"bytes",int(gpu.bytes())}});exact&=std::memcmp(raw.data(),cpu.data(),gpu.bytes())==0;}
  tile_hashes[name]=hashes;save_json(output+"/full-tiles.json",tile_hashes);host->with_gpu([&]{readback->clear();});readback.reset();require(hashes.size()==2&&exact,"Full role16 immutable CPU tiles differ from GPU");return true;
 }
 void geometry_check(const QJsonObject &geometry,const QString &name){
  require(geometry["available"].toBool(),"Geometry witness unavailable (nonblocking Scene contention)");require(geometry["generation"].toInteger()==qint64(wanted_generation)&&geometry["epoch"].toInteger()==qint64(observer->snapshot().epoch),"Wrong/current Cycles geometry generation/epoch");const auto meshes=geometry["meshes"].toArray(),objects=geometry["objects"].toArray();require(meshes.size()==1&&objects.size()==1,"Expected sole own dense plane in Cycles");const auto mesh=meshes[0].toObject();const auto positions=mesh["positions"].toArray(),original=mesh["undisplaced"].toArray();require(mesh["true_displacement"].toBool()&&positions.size()==2145&&original.size()==2145&&mesh["triangles"].toArray().size()==4096&&mesh["uv"].toArray().size()==4096*3,"Wrong actual Cycles displacement/geometry/UV shape");
  // importer_mesh_utils.cc calls mat3_from_axis_conversion(Y,Z,-Z,Y):
  // packed key0x351 selects LUT row21, columns[(1,0,0),(0,0,1),(0,-1,0)].
  // Cycles records row vectors. True displacement excludes transform baking
  // (scene/object.cpp), so the local signed normal oracle stays +/-Z.
  require(mesh["transform_applied"].isBool()&&!mesh["transform_applied"].toBool(),"True Displacement fixture transform was baked");
  const auto transform=objects[0].toObject()["transform"].toArray();const double imported[3][4]={{1,0,0,0},{0,0,-1,0},{0,1,0,0}};
  require(transform.size()==3,"Missing imported object rotation");for(int row=0;row<3;++row){const auto values=transform[row].toArray();require(values.size()==4,"Invalid imported rotation row");for(int column=0;column<4;++column)require(values[column].isDouble()&&std::isfinite(values[column].toDouble())&&std::abs(values[column].toDouble()-imported[row][column])<=3e-5,"Cycles object differs from independently pinned OBJ rotation");}std::set<std::pair<int,int>> grid;int checked=0;double maximum=0,maximum_transverse=0;
  for(int i=0;i<original.size();++i){const auto source=original[i].toArray(),actual=positions[i].toArray();require(source.size()==3&&actual.size()==3,"Invalid Cycles float3 array");for(int component=0;component<3;++component)require(source[component].isDouble()&&actual[component].isDouble()&&std::isfinite(source[component].toDouble())&&std::isfinite(actual[component].toDouble()),"Invalid/nonfinite original or displaced coordinate");const double x=source[0].toDouble(),y=source[1].toDouble();const int ix=int(std::lround((x+1)*32)),iy=int(std::lround((y+.5)*32));require(ix>=0&&ix<=64&&iy>=0&&iy<=32&&x==ix/32.-1&&y==iy/32.-.5&&source[2].toDouble()==0&&grid.insert({ix,iy}).second,"Undeformed Cycles source is not the independently authored plane");// Displacement forcibly uses decoded16-bit octahedral smooth normals:
   // exact+Z has encoded dx=dy32768, decoded transverse~1/65535.
   // Thus expect floating near-zero, not bit-exact X/Y (types_normal.h).
   const double transverse=std::max(std::abs(actual[0].toDouble()-x),std::abs(actual[1].toDouble()-y));maximum_transverse=std::max(maximum_transverse,transverse);require(transverse<=3e-5,"Displacement X/Y exceeded the existing component bound");
   const int local=ix%32;if((iy==8||iy==24)&&(local==4||local==8||local==12||local==20||local==24||local==28)){const double expected=.5*expected_r(ix/32.);const double error=std::abs(actual[2].toDouble()-expected);maximum=std::max(maximum,error);++checked;}
  }save_json(output+"/"+name+"-geometry-oracle.json",{{"checked_vertices",checked},{"checked_transverse_vertices",original.size()},{"maximum_transverse_error",maximum_transverse},{"maximum_z_error",maximum},{"bound",3e-5},{"oracle","original own plane UV and signed +/- .25 pattern, strength .5, midlevel0, +Z normal"}});require(checked==24&&maximum<=3e-5,"Actual Cycles displaced vertices did not follow current role16 pixels");
  if(name=="before")first_geometry=geometry;if(name=="edited")edited_geometry=geometry;
  if(name!="before"){const auto before=first_geometry["meshes"].toArray()[0].toObject();require(mesh["undisplaced"]==before["undisplaced"]&&mesh["triangles"]==before["triangles"]&&mesh["uv"]==before["uv"]&&geometry["objects"]==first_geometry["objects"],"Cycles original geometry/UV/topology/owners changed");}
  if(name=="undone")require(mesh["positions"]==first_geometry["meshes"].toArray()[0].toObject()["positions"],"Undo did not exactly restore displaced vertices");if(name=="redone")require(mesh["positions"]==edited_geometry["meshes"].toArray()[0].toObject()["positions"],"Redo did not exactly restore edited displaced vertices");
 }
 void offset_gesture(){
  auto *settings=get<QToolButton>("fillSettingsTab");if(!settings->isChecked())settings->click();
  auto *disclosure=get<QToolButton>("fillTransformDisclosure");if(!disclosure->isChecked())disclosure->click();
  auto *field=get<QDoubleSpinBox>("fillOffset0");for(auto *parent=field->parentWidget();parent;parent=parent->parentWidget())if(auto *scroll=qobject_cast<QScrollArea *>(parent))scroll->ensureWidgetVisible(field);
  auto *line=field->findChild<QLineEdit *>();require(line,"Fill offset has no real text editor");QJsonArray events;
  const auto record=[&](const QString &step){
   const auto metadata=state(true);QJsonValue model(QJsonValue::Null);const auto sets=metadata["document"].toObject()["texture_sets"].toArray();
   if(sets.size()==1){const auto layers=sets[0].toObject()["layers"].toArray();if(layers.size()==1){const auto values=layers[0].toObject()["fill_projection"].toObject()["offset"].toArray();if(!values.isEmpty())model=values[0];}}
   auto *focus=QApplication::focusWidget();auto *active=QApplication::activeWindow();
   events.append(QJsonObject{{"step",step},{"text",line->text()},{"value",field->value()},{"acceptable",field->hasAcceptableInput()},{"visible",field->isVisible()},{"enabled",field->isEnabled()},{"field_focus",field->hasFocus()},{"line_focus",line->hasFocus()},{"active",field->isActiveWindow()},{"focus",focus?focus->objectName():QString{}},{"active_window",active?active->objectName():QString{}},{"locale",field->locale().name()},{"decimal",field->locale().decimalPoint()},{"keyboard_tracking",field->keyboardTracking()},{"model_offset",model},{"undo_count",metadata["undo_count"]}});
   save_json(output+"/mapping-input.json",{{"route","synthetic Qt frontend keys on visible NumericSpinBox, explicit Return before Tab; no model setter"},{"events",events}});
  };
  record("before-visibility-assert");require(field->isVisible()&&field->isEnabled()&&!field->visibleRegion().isEmpty(),"Fill offset control is not artist-visible in Rendering");
  field->window()->activateWindow();field->setFocus();record("after-activate-focus");
  const auto key=[&](int code,const QString &text,Qt::KeyboardModifiers modifiers,const QString &step){QKeyEvent press(QEvent::KeyPress,code,modifiers,text),release(QEvent::KeyRelease,code,modifiers,text);QApplication::sendEvent(field,&press);QApplication::sendEvent(field,&release);record(step);};
  key(Qt::Key_A,"a",Qt::ControlModifier,"select-all");
  key(Qt::Key_0,"0",Qt::NoModifier,"type-0");key(Qt::Key_Period,".",Qt::NoModifier,"type-period");key(Qt::Key_5,"5",Qt::NoModifier,"type-5");
  // keyboardTracking=false: Return explicitly interprets pending text. Tab
  // alone depends on a real focusOut, which synthetic inactive tools may lack.
  key(Qt::Key_Return,{},Qt::NoModifier,"after-spin-return");key(Qt::Key_Tab,{},Qt::NoModifier,"after-tab");
  record("before-commit-assert");require(field->value()==.5,"Synthetic Qt Fill mapping input did not commit .5");viewport->setFocus();record("after-viewport-focus");offset=.5;
 }
 void capture(const QString &name){
  const auto geometry=QJsonDocument::fromJson(QByteArray::fromStdString(observer->geometry_json())).object();const auto completion=observer->snapshot();auto data=state(true);data["geometry"]=geometry;data["samples"]=samples(false);data["source_preview"]=source_state();data["offset"]=offset;data["stage"]=name;data["full_tiles"]=tile_hashes[name];data["completion"]=QJsonObject{{"generation",qint64(completion.generation)},{"epoch",qint64(completion.epoch)},{"terminal_epoch",qint64(completion.terminal_epoch)},{"sample",completion.sample},{"progress",completion.progress},{"draw_epoch",qint64(paint->draw_epoch)}};data["cycles_graph_count"]=QString::number(BTS_cycles_graph_count());data["cycles_svm_count"]=QString::number(BTS_cycles_svm_count());
  // Preserve all observations/screenshots BEFORE any correctness assertion.
  save_json(output+"/"+name+"-observed.json",data);dump_probe();const auto frame=capture_viewport_framebuffer(viewport);
  const bool frame_saved=frame.save(output+"/"+name+".png");
  const bool roi_saved=frame.copy(frame.width()/4,frame.height()/4,frame.width()/2,frame.height()/2).save(output+"/"+name+"-mesh-roi.png");
  const bool workspace_saved=capture_demo_window(window,viewport).save(output+"/"+name+"-workspace.png");
  require(frame_saved&&roi_saved&&workspace_saved,"Could not retain all three actual Displacement frame captures");
  require(get<QAction>("renderingMode")->isChecked(),"Displacement actor left actual Rendering");samples(true);geometry_check(geometry,name);require(handles()==original_handles,"Mirror/reload replaced authoritative GPU images");require(original_mesh_fields(source_state())==original_source,"Original Blender mesh/UV or subdivision changed");require(displacement_state()==original_displacement,"Pixel-only mapping edit changed Displacement settings");const auto current_profile=QString::fromStdString(observer->profile_json());const auto layers=data["document"].toObject()["texture_sets"].toArray()[0].toObject()["layers"].toArray();require(layers.size()==1&&layers[0].toObject()["fill_projection"].toObject()["offset"].toArray()[0].toDouble()==offset,"Actual document did not retain the requested mapping offset");if(name=="before"){history=data["undo_count"].toInt();profile=current_profile;original_view=data["view"];graph=BTS_cycles_graph_count();svm=BTS_cycles_svm_count();require(graph>0&&svm>0,"Real Cycles graph/SVM counter instrumentation is missing");}else require(current_profile==profile&&data["view"]==original_view,"Controlled camera/light/color/sampling state changed");require(BTS_cycles_graph_count()==graph&&BTS_cycles_svm_count()==svm,"Pixel-only displacement edit rebuilt Cycles shader graph/SVM");require(data["undo_count"].toInt()==history+((name=="edited"||name=="redone")?1:0),"Displacement mapping edit/Undo/Redo was not one history gesture");captures.append(data);save_json(output+"/verified-stages.json",{{"profile","live-displacement-controlled-v1"},{"captures",captures},{"performance_claim",false},{"production_changed",false}});
 }
 void fail(const std::exception &error)noexcept {timer.stop();try{auto failure=state(true);failure["stage"]=stage;failure["error"]=error.what();failure["geometry"]=QJsonDocument::fromJson(QByteArray::fromStdString(observer->geometry_json())).object();save_json(output+"/failure.json",failure);capture_viewport_framebuffer(viewport).save(output+"/failure.png");}catch(...){}cleanup();std::fprintf(stderr,"BA_DISPLACEMENT_FAILED stage=%d %s\n",stage,error.what());QCoreApplication::exit(1);}
 void advance(){require(deadline.elapsed()<300000,"Bounded Displacement actor timeout");const auto current_state=state();if(stage>1&&(current_state["composite_pending"].toBool()||current_state["property_pending"].toBool()))return;
  if(stage>=5&&stage<=8){if(!completed_draw()||!current())return;const QString name=stage==5?"before":stage==6?"edited":stage==7?"undone":"redone";if(!full_tiles(name))return;capture(name);}
  std::printf("BTS_BA_DISPLACEMENT_STAGE step=%d\n",stage);std::fflush(stdout);
  switch(stage){
   case 0:write_mesh();action("newProject");break;
   case 1:{auto *dialog=get<QDialog>("newProjectDialog");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);dialog->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);auto *resolution=dialog->findChild<QComboBox *>("documentResolution");require(resolution->findData(128)>=0,"Missing real128 resolution choice");resolution->setCurrentIndex(resolution->findData(128));dialog->findChild<QPushButton *>("createProject")->click();break;}
   case 2:fixture();painting_frame=state()["render_frames"].toInt()+1;viewport->update();break;
   case 3:{
    if(current_state["render_frames"].toInt()<painting_frame)return;
    const auto preview=source_state();const auto materials=preview["materials"].toArray();
    require(materials.size()==1,"Fixture must have exactly one Displacement material");
    if(!materials[0].toObject()["linked"].toBool()||!materials[0].toObject()["content"].toBool())return;
    require(materials[0].toObject()["method"].toInt()==blender::MA_DISPLACEMENT_BOTH,"Fixture did not activate real Displacement+Bump");
    QJsonArray painting_samples;bool painting_exact=true;
    for(int tile:{1001,1002}){const auto gpu=host->diagnostic_gpu_pixels(uid,16,points,tile);painting_exact&=gpu.size()==points.size();QJsonArray values;for(std::size_t i=0;i<gpu.size();++i){QJsonArray rgba;for(float c:gpu[i])rgba.append(c);values.append(rgba);if(i<points.size())painting_exact&=std::isfinite(gpu[i][0])&&std::abs(gpu[i][0]-expected_r((points[i][0]+.5)/128.))<=3e-5&&gpu[i][3]==1;}painting_samples.append(QJsonObject{{"tile",tile},{"samples",values}});}
    // Existing FBO capture only; never acquire an ImBuf before the first real
    // Cycles entry/mirror, which could hide an unloaded-cache loader bug.
    const bool frame_saved=capture_viewport_framebuffer(viewport).save(output+"/painting-before.png");
    save_json(output+"/painting-before.json",{{"view",current_state["view"]},{"source_preview",preview},{"role",16},{"gpu",painting_samples},{"cpu_cache_probed",false}});
    require(frame_saved&&painting_exact,"Initial EEVEE frame/publication or signed role16 formula differs");
    original_handles=handles();original_source=original_mesh_fields(source_state());original_displacement=displacement_state();require(!original_source.isEmpty()&&!original_handles.isEmpty()&&original_displacement["strength"].toDouble()==.5&&original_displacement["midlevel"].toDouble()==0,"Fixture Displacement settings/source were not committed");observer->install_controlled_profile();const auto config=QJsonDocument::fromJson(QByteArray::fromStdString(observer->profile_json())).object();require(config["samples"].toInt()==128&&!config["adaptive"].toBool(true)&&!config["pixel_jitter"].toBool(true)&&!config["denoise"].toBool(true),"Controlled sampler settings did not apply");get<QToolButton>("propertiesUndock")->click();break;}
   case 4:arm(true);action("renderingMode");break;
   case 5:arm(false);offset_gesture();break;
   case 6:arm(false);action("undoDocument");offset=0;break;
   case 7:arm(false);action("redoDocument");offset=.5;break;
   case 8:action("paintingMode");break;
   case 9:cleanup();require(!cleanup_failed,"Displacement observer cleanup failed");timer.stop();save_json(output+"/verified.json",{{"profile","live-displacement-controlled-v1"},{"captures",captures},{"production_changed",false},{"performance_claim",false},{"cpu_composite",false}});std::puts("BTS_BA_DISPLACEMENT_DONE");QCoreApplication::quit();return;
  }++stage;
 }
 public:DisplacementArtist(QWidget *w,QWidget *v,QString dir,std::function<QJsonObject()> snapshot,BlenderHost *native,CyclesFixtureEdit fixture,CyclesFrameHook hook):QObject(w),window(w),viewport(v),output(std::move(dir)),host(native),edit(std::move(fixture)),read(std::move(snapshot)){
  require(bool(hook),"Missing actual viewport frame observer");require(QDir().mkpath(output),"Cannot create fresh Displacement evidence directory");require(!QFile::exists(output+"/fixture-source.json"),"Use a fresh Displacement runtime attempt");paint->observer=observer;new PaintFilter(viewport,paint);hook([shared=paint]{shared->draw_generation=shared->begin_generation;shared->draw_epoch=shared->begin_epoch;});connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,this,[this]{cleanup();});deadline.start();timer.setInterval(100);connect(&timer,&QTimer::timeout,this,[this]{try{advance();}catch(const std::exception &error){fail(error);}});timer.start();
 }
};
}}
namespace bts {void start_cycles_displacement_artist_demo(QWidget *w,QWidget *v,const QString &output,const QString &,int resolution,std::function<QJsonObject()> read,BlenderHost *host,CyclesFixtureEdit edit,CyclesFrameHook hook){require(resolution==128,"Displacement profile requires its explicit128 extent");new DisplacementArtist(w,v,output,std::move(read),host,std::move(edit),std::move(hook));}}
