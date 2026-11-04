#include "CyclesLiveUdimArtist.hh"
#ifdef BTS_BA_CONTROLLED_PROFILE_TEST
#include "CyclesLiveCompletionProbe.hh"
#include "../host/GpuProjectReadback.hh"
#endif
#include "BKE_image_hosted.hh"
#include "BKE_image_gpu_override.hh"
#include "DEG_depsgraph.hh"
#include <QCryptographicHash>
#include <QEvent>
#include <cstring>
#include <map>
#include <chrono>
#include "../host/ColorPicker.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <cstdio>
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "BKE_image.hh"
#include "BLI_listbase.h"
#include "DNA_image_types.h"
#include "IMB_imbuf_types.hh"
#include "GPU_texture.hh"
#include "../host/ViewportDemoCapture.hh"
extern "C" unsigned long long BTS_cycles_graph_count();
extern "C" unsigned long long BTS_cycles_svm_count();
namespace bts {namespace {
using blender::G;
using blender::Image;
#ifdef BTS_BA_CONTROLLED_PROFILE_TEST
struct CompletionPaintState {
 std::shared_ptr<CyclesLiveCompletionProbe> observer;
 std::uint64_t begin_generation=0,begin_epoch=0,draw_generation=0,draw_epoch=0,draws=0;
 long long begin_ns=0,done_ns=0;bool failed=false;
};
class CompletionPaintFilter final:public QObject {
 std::shared_ptr<CompletionPaintState> state;
 public:CompletionPaintFilter(QWidget *viewport,std::shared_ptr<CompletionPaintState> value):QObject(viewport),state(std::move(value)){viewport->installEventFilter(this);}
 bool eventFilter(QObject *,QEvent *event)override {
  if(event->type()==QEvent::Paint&&state->observer){try{const auto sample=state->observer->snapshot();state->begin_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();state->begin_generation=sample.generation;state->begin_epoch=sample.terminal_epoch==sample.epoch&&sample.sample==128&&sample.progress==1?sample.epoch:0;}catch(...){state->failed=true;}}
  return false;
 }
};
#endif
class CyclesUdimArtist final:public QObject {
 friend class CyclesLiveBurst; // Separate test actor reuses this exact fixture; no production hook.
 QWidget *window,*viewport;QString output,mesh;int resolution;BlenderHost *host;CyclesFixtureEdit edit_fixture;
 std::function<QJsonObject()> snapshot;QTimer timer;QElapsedTimer interval;
 int stage=0,ticks=0,settled=0,minimum_frame=0;std::uint32_t uid=0;
 PickerColor expected{1,1,1,1},edited{};float roughness=.23f;
 std::array<std::array<int,2>,12> points;
 QJsonArray captures;QJsonObject handles;unsigned long long graph=0,svm=0;
 QImage before_roi,edited_roi;
 bool controlled=false,noop_pending=false,camera_end_marked=false;
#ifdef BTS_BA_CONTROLLED_PROFILE_TEST
 std::shared_ptr<CompletionPaintState> paint=std::make_shared<CompletionPaintState>();
 std::shared_ptr<CyclesLiveCompletionProbe> observer;
 std::uint64_t wanted_generation=0,wanted_epoch=0,requested_generation=0,requested_epoch=0;
 QString controlled_profile;QJsonValue original_view,original_document;
 std::unique_ptr<GpuProjectReadback> diagnostic_readback;QString diagnostic_stage;QJsonArray diagnostic_targets;
 std::map<QString,QJsonArray> full_hashes;
 bool cleanup_failed=false;
 void cleanup_test_resources()noexcept {
  try{if(diagnostic_readback){host->with_gpu([&]{diagnostic_readback->clear();});diagnostic_readback.reset();}}catch(...){qFatal("Controlled diagnostic GPU resources could not retire before owner teardown");}
  try{if(observer)observer->restore();}catch(...){cleanup_failed=true;}
  try{dump_observer();}catch(...){cleanup_failed=true;}
 }
 void dump_observer(){if(!observer)return;QFile file(output+"/completion-trace.json");require(file.open(QIODevice::WriteOnly),"Cannot retain completion trace");file.write(QByteArray::fromStdString(observer->trace_json()));}
 void next_epoch(){if(!observer)return;const auto now=observer->snapshot();wanted_generation=now.generation;wanted_epoch=now.epoch+1;}
 void enter_observed(){if(!observer)return;const auto now=observer->snapshot();wanted_generation=now.generation+1;wanted_epoch=1;observer->arm_entry();}
 bool completed_draw(){
  if(!observer)return true;const auto now=observer->snapshot();require(now.error.empty(),now.error.c_str());require(!paint->failed,"Completion paint observer failed");
  if(now.generation<wanted_generation||now.epoch<wanted_epoch||now.terminal_epoch!=now.epoch||now.sample!=128||now.progress!=1||now.status.find("Rendering Done")==std::string::npos)return false;
  require(now.generation==wanted_generation,"Unexpected Cycles Session generation");
  if(requested_generation!=now.generation||requested_epoch!=now.epoch){requested_generation=now.generation;requested_epoch=now.epoch;viewport->update();return false;}
  return paint->draw_generation==now.generation&&paint->draw_epoch==now.epoch;
 }
 bool full_tiles_ready(const QString &name){
  if(!observer||full_hashes.contains(name))return true;
  if(!diagnostic_readback){

   diagnostic_stage=name;diagnostic_targets={};diagnostic_readback=std::make_unique<GpuProjectReadback>();
   host->with_gpu([&]{for(int tile:{1001,1002})for(int channel:{0,2}){auto *native=image(channel);const auto gpu=blender::BKE_image_gpu_override_texture(native);blender::ImageGpuTileRegion region;require(gpu.texture&&*gpu.texture&&blender::BKE_image_gpu_override_tile_region(native,tile,region),"Missing real diagnostic GPU tile region");require(region.width==resolution&&region.height==resolution,"Wrong full hash GPU tile extent");
    diagnostic_readback->submit(channel,tile,*gpu.texture,region.x,region.y,region.layer,region.width,region.height);
    diagnostic_targets.append(QJsonObject{{"channel",channel},{"tile",tile},{"image_update_count",QString::number(native->runtime->update_count)},{"gpu_format",int(blender::GPU_texture_format(*gpu.texture))},{"authoritative_pointer",QString::number(quintptr(*gpu.texture),16)}});
   }});
   std::printf("BTS_BA_DIAGNOSTIC_FULL_TILE_READBACK stage=%s tiles=4 float_bytes=%llu production_mirror=0\n",qPrintable(name),(unsigned long long)(std::uint64_t(resolution)*resolution*16*4));std::fflush(stdout);return false;
  }
  require(diagnostic_stage==name,"Diagnostic full hash stage changed mid-capture");bool ready=false;host->with_gpu([&]{ready=diagnostic_readback->poll();});if(!ready)return false;
  QJsonArray hashes;const auto &images=diagnostic_readback->images();require(images.size()==4,"Wrong complete diagnostic tile count");
  for(std::size_t i=0;i<images.size();++i){const auto &gpu=images[i];require(!gpu.uniform,"Controlled full tile witness unexpectedly uniform");auto *native=image(int(gpu.uid));auto record=diagnostic_targets[int(i)].toObject();require(record["image_update_count"].toString()==QString::number(native->runtime->update_count),"Full hash source generation changed");
   blender::ImageUser user{};user.tile=gpu.tile;auto *cpu=blender::BKE_image_acquire_ibuf(native,&user,nullptr);struct Release {Image *image;blender::ImBuf *buffer;~Release(){blender::BKE_image_release_ibuf(image,buffer,nullptr);}} release{native,cpu};
   require(cpu&&cpu->float_data()&&cpu->channels==4&&cpu->x==resolution&&cpu->y==resolution,"Invalid full CPU mirror for hash");const auto bytes=gpu.bytes();const QByteArrayView raw(reinterpret_cast<const char *>(gpu.pixels),qsizetype(bytes)),mirror(reinterpret_cast<const char *>(cpu->float_data()),qsizetype(bytes));
   record["bytes"]=QString::number(bytes);record["gpu_sha256"]=QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex());record["cpu_sha256"]=QString::fromLatin1(QCryptographicHash::hash(mirror,QCryptographicHash::Sha256).toHex());require(std::memcmp(raw.data(),mirror.data(),bytes)==0,"Full GPU and immutable CPU tile bytes differ");hashes.append(record);
  }
  host->with_gpu([&]{diagnostic_readback->clear();});diagnostic_readback.reset();full_hashes[name]=hashes;
  if(name=="cycles-noop-control"||name=="cycles-color-undone"){const auto baseline=full_hashes.at("cycles-before");for(int i=0;i<hashes.size();++i)require(hashes[i].toObject()["gpu_sha256"]==baseline[i].toObject()["gpu_sha256"],"No-op/Undo full tile hash did not restore baseline");}
  if(name=="cycles-color-redone"){const auto baseline=full_hashes.at("cycles-color-edited");for(int i=0;i<hashes.size();++i)require(hashes[i].toObject()["gpu_sha256"]==baseline[i].toObject()["gpu_sha256"],"Redo full tile hash did not restore edited bytes");}
  return true;
 }
 void install_controlled(){if(!controlled)return;observer=std::make_shared<CyclesLiveCompletionProbe>();observer->install_controlled_profile();controlled_profile=QString::fromStdString(observer->profile_json());const auto recorded=QJsonDocument::fromJson(controlled_profile.toUtf8()).object();require(!recorded["adaptive"].toBool(true)&&!recorded["denoise"].toBool(true)&&!recorded["animated_seed"].toBool(true)&&!recorded["pixel_jitter"].toBool(true)&&recorded["samples"].toInt()==128,"Controlled sampler was not actually applied");paint->observer=observer;dump_observer();}
#else
 bool cleanup_failed=false;
 void cleanup_test_resources()noexcept{}
 void dump_observer(){}
 void next_epoch(){}
 void enter_observed(){}
 void install_controlled(){}
 bool completed_draw(){return true;}
 bool full_tiles_ready(const QString &){return true;}
#endif
 template<class T>T *get(const char *name){auto *v=window->findChild<T *>(name);if(!v)throw std::runtime_error(std::string("Missing BA UDIM control: ")+name);return v;}
 static void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
 void action(const char *name){get<QAction>(name)->trigger();}
 void camera_gesture(){
  const QPointF from(viewport->width()*.5,viewport->height()*.5),to=from+QPointF(30,-15);
  QMouseEvent press(QEvent::MouseButtonPress,from,viewport->mapToGlobal(from),Qt::LeftButton,Qt::LeftButton,Qt::AltModifier);QApplication::sendEvent(viewport,&press);
  QMouseEvent move(QEvent::MouseMove,to,viewport->mapToGlobal(to),Qt::NoButton,Qt::LeftButton,Qt::AltModifier);QApplication::sendEvent(viewport,&move);
  QMouseEvent release(QEvent::MouseButtonRelease,to,viewport->mapToGlobal(to),Qt::LeftButton,Qt::NoButton,Qt::AltModifier);QApplication::sendEvent(viewport,&release);
 }
 QJsonObject state(bool metadata=false){
  // The ordinary fallback snapshot performs optional diagnostic pixel samples.
  // Status/idle polling must take the existing early, probe-only return instead.
  // The metadata-only Layer Drop branch exposes the committed document without
  // acquiring Image buffers or evaluating/compositing any CPU pixels.
  qunsetenv("BTS_BLEND_MATERIAL_DEMO");const auto previous=window->property("guiProbeOnly");window->setProperty("guiProbeOnly",!metadata);
  if(metadata)qputenv("BTS_LAYER_DROP_DEMO","1");auto result=snapshot();if(metadata)qunsetenv("BTS_LAYER_DROP_DEMO");window->setProperty("guiProbeOnly",previous);return result;
 }
 float mask_value(int tile,int x,int y)const{
  // Independently specified nearest 32x32 Position input, in native low-Y row
  // order. Native WorldHeight(axisZ, contrast1,bias0,linear,opacity1) is z.
  const int sx=std::clamp(int((double(x)+.5)*32/resolution),0,31),sy=std::clamp(int((double(y)+.5)*32/resolution),0,31);
  return tile==1001?.125f+.75f*float(sx)/31.f:.1875f+.625f*float(sy)/31.f;
 }
 Image *image(int channel){for(blender::Image &v:G_MAIN->images)if(std::string_view(v.id.name+2)=="BTS Composite "+std::to_string(uid)+(channel==0?" Base Color":" Roughness"))return &v;throw std::runtime_error("Missing native canonical image");}
 QJsonArray probe(bool check){
  QJsonArray result;
  for(int tile:{1001,1002})for(int channel:{0,2}){
   const auto gpu=host->diagnostic_gpu_pixels(uid,channel,points,tile);require(gpu.size()==points.size(),"Missing native GPU UDIM probes");
   auto *native=image(channel);blender::ImageUser user{};user.tile=tile;auto *cpu=blender::BKE_image_acquire_ibuf(native,&user,nullptr);
   struct Release {Image *image;blender::ImBuf *buffer;~Release(){blender::BKE_image_release_ibuf(image,buffer,nullptr);}} release{native,cpu};
   require(cpu&&cpu->float_data()&&cpu->x==resolution&&cpu->y==resolution,"Missing full-resolution CPU UDIM mirror");
   QJsonArray samples;for(std::size_t i=0;i<points.size();++i){
    const auto [x,y]=points[i];const auto alpha=mask_value(tile,x,y);QJsonArray raw,mirror,formula;
    for(int c=0;c<4;++c){const float wanted=c==3?alpha:alpha*(channel==0?expected[c]:roughness);const auto actual=cpu->float_data()[(std::size_t(y)*resolution+x)*4+c];raw.append(gpu[i][c]);mirror.append(actual);formula.append(wanted);
     if(check){if(std::abs(gpu[i][c]-wanted)>3e-5f||actual!=gpu[i][c])std::fprintf(stderr,"BA_UDIM_TEXEL tile=%d channel=%d xy=%d,%d component=%d GPU=%.9g CPU=%.9g formula=%.9g alpha=%.9g\n",tile,channel,x,y,c,gpu[i][c],actual,wanted,alpha);require(std::abs(gpu[i][c]-wanted)<=3e-5f,"Native WorldHeight/associated channel formula mismatch");require(actual==gpu[i][c],"GPU to immutable CPU mirror was not bit-exact");}}
    require(alpha>0&&alpha<1,"Fixture must witness genuinely partial associated alpha");
    samples.append(QJsonObject{{"xy",QJsonArray{x,y}},{"gpu",raw},{"cpu",mirror},{"expected",formula}});
   }result.append(QJsonObject{{"tile",tile},{"channel",channel},{"samples",samples},{"image_update_count",QString::number(native->runtime->update_count)}});
  }return result;
 }
 bool current(){
  // Checking the known target GPU value plus its mirror prevents a previous
  // render's Done status from satisfying a newly queued artist edit.
  for(int tile:{1001,1002})for(int channel:{0,2}){
   const auto gpu=host->diagnostic_gpu_pixels(uid,channel,std::span(points).first(1),tile);if(gpu.size()!=1)return false;
   const auto alpha=mask_value(tile,points[0][0],points[0][1]);const float wanted=alpha*(channel==0?expected[0]:roughness);
   if(std::abs(gpu[0][0]-wanted)>3e-5f){
    static bool reported=false;if(!reported){reported=true;std::fprintf(stderr,"BA_UDIM_WAIT tile=%d channel=%d xy=%d,%d GPU=%.9g expected=%.9g alpha=%.9g\n",tile,channel,points[0][0],points[0][1],gpu[0][0],wanted,alpha);std::fflush(stderr);}return false;
   }
   if(!get<QAction>("renderingMode")->isChecked())continue;
   auto *native=image(channel);blender::ImageUser user{};user.tile=tile;auto *buffer=blender::BKE_image_acquire_ibuf(native,&user,nullptr);bool matches=buffer&&buffer->float_data()&&buffer->x==resolution&&buffer->y==resolution&&std::abs(buffer->float_data()[(std::size_t(points[0][1])*resolution+points[0][0])*4]-wanted)<=3e-5f;blender::BKE_image_release_ibuf(native,buffer,nullptr);if(!matches)return false;
  }return true;
 }
 void fixture(){bool prepared=false;edit_fixture([&](TextureSet &set){
  uid=set.material_session_uid;require(set.tiles==std::set<int>({1001,1002}),"Fixture did not preserve both native UDIM tiles");
  require(set.width()==resolution&&set.rows()==resolution,"Fixture did not retain requested resolution");
  set.precision[0]=ChannelPrecision::RGB32F;set.precision[2]=ChannelPrecision::L32F;
  // The real insertion API requires an existing selection. Keep the newly
  // inserted fill only after that API has selected it, removing the opaque base.
  TextureDocument::add_fill(set);auto fill=std::move(TextureDocument::layer(set,set.selected_layer));set.layers.clear();fill.parent={};fill.depth=0;set.layers.push_back(std::move(fill));
  auto &layer=TextureDocument::layer(set,set.selected_layer);layer.name="BA native WorldHeight masked fill";layer.channels={true,false,true,false,false};layer.ao_enabled=false;layer.values.base_color=expected;layer.values.roughness=roughness;
  TextureDocument::add_mask(set,0);MaskEffect generator;generator.kind=MaskEffectKind::Generator;generator.generator=1;generator.axis=2;generator.curve=0;generator.contrast=1;generator.bias=0;generator.opacity=1;layer.mask->effects={generator};
  MeshMap position;position.name="BA independently specified Position input";
  for(int tile:{1001,1002}){std::vector<float> rgba(32*32*4);for(int y=0;y<32;++y)for(int x=0;x<32;++x){const auto at=(y*32+x)*4;rgba[at+2]=tile==1001?.125f+.75f*float(x)/31.f:.1875f+.625f*float(y)/31.f;rgba[at+3]=1;}
   auto pixels=std::make_shared<MeshMapPixels>(32,std::move(rgba));position.tiles[tile]=MeshMapPlane{32,pixels->hash,std::move(pixels),32};}
  set.mesh_maps[5]=std::move(position);set.selected_mask=false;set.selected_effect=QUuid();prepared=true;
 });
  require(prepared,"BA fixture callback was rejected before completion");
  const auto committed=state(true);QFile file(output+"/fixture-committed.json");require(file.open(QIODevice::WriteOnly),"Cannot save committed fixture evidence");file.write(QJsonDocument(committed).toJson());file.close();
  const auto sets=committed["document"].toObject()["texture_sets"].toArray();require(sets.size()==1,"BA fixture document is missing its sole Texture Set");const auto set=sets[0].toObject();const auto layers=set["layers"].toArray();
  require(layers.size()==1&&layers[0].toObject()["name"].toString()=="BA native WorldHeight masked fill","Real artist transaction rejected the BA fixture");
  const auto precision=set["precision"].toArray();require(precision[0].toInt()==int(ChannelPrecision::RGB32F)&&precision[2].toInt()==int(ChannelPrecision::L32F),"Committed BA fixture did not retain requested channel precision");
  const auto mask=layers[0].toObject()["mask"].toObject();const auto effects=mask["effects"].toArray();require(mask["background"].toDouble()==0&&effects.size()==1&&effects[0].toObject()["generator"].toInt()==1,"Committed BA fixture lost its black mask/WorldHeight generator");
  require(set["mesh_maps"].toArray()[5].toObject()["tiles"].toArray().size()==2,"Committed BA fixture lost its two Position map planes");
 }
 void require_target_formats(){
  for(int channel:{0,2}){auto *native=image(channel);auto *target=native->runtime->gputexture[blender::TEXTARGET_2D_ARRAY][0];require(target,"BA fixture has no actual two-UDIM GPU target");const auto format=blender::GPU_texture_format(target);
   std::printf("BTS_BA_UDIM_TARGET channel=%d requested=%s actual_format=%d high_bitdepth=%d\n",channel,channel==0?"RGB32F":"L32F",int(format),bool(native->flag&blender::IMA_HIGH_BITDEPTH));
   require(format==blender::gpu::TextureFormat::SFLOAT_32_32_32_32,"BA fixture actual GPU target is not requested float32");}
  for(blender::Image &native:G_MAIN->images)if(std::string_view(native.id.name+2).starts_with("BTS Composite "))handles[native.id.name+2]=QJsonArray{QString::number(quintptr(native.runtime->gputexture[0][0]),16),QString::number(quintptr(native.runtime->gputexture[1][0]),16),QString::number(quintptr(native.runtime->gputexture[2][0]),16)};
  QFile before(output+"/painting-preentry-handles.json");require(before.open(QIODevice::WriteOnly),"Cannot retain pre-Cycles GPU handles");before.write(QJsonDocument(handles).toJson());
 }
 void color_gesture(double red){
  auto *picker=static_cast<ColorPicker *>(get<QWidget>("materialColorPicker"));auto *swatch=picker->findChild<QPushButton *>("colorSwatch");
  require(swatch,"Missing artist Color swatch");for(auto *parent=swatch->parentWidget();parent;parent=parent->parentWidget())if(auto *scroll=qobject_cast<QScrollArea *>(parent)){scroll->ensureWidgetVisible(swatch);break;}QApplication::processEvents();
  require(swatch->isVisible()&&swatch->isEnabled()&&!swatch->visibleRegion().isEmpty(),"Artist Color swatch is not visible/enabled during Rendering");swatch->click();QApplication::processEvents();
  auto *popup=picker->findChild<QDialog *>("colorPickerDialog");popup->findChild<QCheckBox *>("colorAdvanced")->setChecked(true);QApplication::processEvents();auto *field=picker->linear_fields()[0];
  require(field->isVisible()&&field->isEnabled(),"Artist scene-linear Color field is not visible/enabled");field->setFocus();auto *line=field->findChild<QLineEdit *>();require(line,"Missing scene-linear Color editor");
  QKeyEvent select(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier,"a");QApplication::sendEvent(line,&select);QKeyEvent text(QEvent::KeyPress,Qt::Key_0,Qt::NoModifier,QString::number(red));QApplication::sendEvent(line,&text);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(line,&enter);
  require(std::abs(field->value()-red)<1e-6,"Visible scene-linear Color gesture did not reach target");popup->grab().save(output+QString("/color-input-%1.png").arg(red));
  expected=picker->color();picker->findChild<QDialog *>("colorPickerDialog")->hide();
 }
 void capture(const char *name,bool first=false){
  auto data=state(true);data["stage"]=name;
#ifdef BTS_BA_CONTROLLED_PROFILE_TEST
  if(observer){if(QString(name)=="cycles-before")original_document=data["document"];if(QString(name)=="cycles-noop-control")require(data["document"]==original_document,"No-op reload changed authored document JSON");}
  if(observer){const auto current_profile=QString::fromStdString(observer->profile_json());const auto settings=QJsonDocument::fromJson(current_profile.toUtf8()).object();require(!settings["adaptive"].toBool(true)&&!settings["pixel_jitter"].toBool(true)&&!settings["animated_seed"].toBool(true)&&!settings["denoise"].toBool(true)&&settings["samples"].toInt()==128&&settings["seed"].toInt(-1)==0&&settings["frame"].toInt(-1)==1,"Actual controlled sampling settings changed");if(QString(name)=="cycles-before")controlled_profile=current_profile;if(QString(name)!="cycles-camera-only"&&QString(name)!="cycles-reentered-latest")require(current_profile==controlled_profile,"Controlled sampler/lighting state changed before declared camera phase");data["controlled_settings"]=settings;const auto completion=observer->snapshot();data["actual_completion"]=QJsonObject{{"generation",QString::number(completion.generation)},{"epoch",QString::number(completion.epoch)},{"terminal_epoch",QString::number(completion.terminal_epoch)},{"sequence",QString::number(completion.sequence)},{"sample",completion.sample},{"progress",completion.progress},{"status",QString::fromStdString(completion.status)},{"draw_generation",QString::number(paint->draw_generation)},{"draw_epoch",QString::number(paint->draw_epoch)},{"draws",QString::number(paint->draws)},{"paint_begin_ns",QString::number(paint->begin_ns)},{"paint_done_ns",QString::number(paint->done_ns)}};data["full_tile_hashes"]=full_hashes.at(name);if(QString(name)=="cycles-before")original_view=data["view"];if(QString(name)=="cycles-noop-control"||QString(name)=="cycles-color-edited"||QString(name)=="cycles-color-undone"||QString(name)=="cycles-color-redone")require(data["view"]==original_view,"Controlled camera changed before declared camera phase");dump_observer();}
#endif
  data["resolution"]=resolution;data["tiles"]=QJsonArray{1001,1002};data["expected_color"]=QJsonArray{expected[0],expected[1],expected[2],expected[3]};data["samples"]=probe(true);
  QJsonObject current_handles;for(blender::Image &native:G_MAIN->images)if(std::string_view(native.id.name+2).starts_with("BTS Composite "))current_handles[native.id.name+2]=QJsonArray{QString::number(quintptr(native.runtime->gputexture[0][0]),16),QString::number(quintptr(native.runtime->gputexture[1][0]),16),QString::number(quintptr(native.runtime->gputexture[2][0]),16)};
  if(handles.isEmpty())handles=current_handles;else require(current_handles==handles,"BA mirror replaced authoritative native GPU allocations");data["authoritative_handles"]=current_handles;
  if(first){graph=BTS_cycles_graph_count();svm=BTS_cycles_svm_count();}
  if(get<QAction>("renderingMode")->isChecked()){require(BTS_cycles_graph_count()==graph&&BTS_cycles_svm_count()==svm,"Pixel-only/camera update recompiled Cycles graph/SVM");}
  data["cycles_graph_count"]=QString::number(BTS_cycles_graph_count());data["cycles_svm_count"]=QString::number(BTS_cycles_svm_count());captures.append(data);
  QFile file(output+"/cycles-udim.json");require(file.open(QIODevice::WriteOnly),"Cannot save BA UDIM evidence");file.write(QJsonDocument(QJsonObject{{"captures",captures},{"profile",controlled?"controlled-fixed-sampling-v1":"default"},{"cpu_composite",false},{"generator","native WorldHeight from independent32x32 Position inputs"},{"source_main_unchanged_demo_call_augmented",true}}).toJson());file.close();
  const auto frame=capture_viewport_framebuffer(viewport);frame.save(output+"/"+name+".png");const QRect roi(frame.width()/3,frame.height()/3,frame.width()/4,frame.height()/4);auto cropped=frame.copy(roi).convertToFormat(QImage::Format_RGB32);cropped.save(output+"/"+name+"-mesh-roi.png");
  if(QString(name)=="cycles-before")before_roi=cropped;
  if(QString(name)=="cycles-color-edited"){
   require(cropped!=before_roi,"Cycles mesh pixels stayed stale after Color edit");double red_difference=0;
   for(int y=0;y<cropped.height();++y)for(int x=0;x<cropped.width();++x)red_difference+=qRed(before_roi.pixel(x,y))-qRed(cropped.pixel(x,y));
   require(red_difference/(cropped.width()*cropped.height())>5,"Cycles mesh did not show the independently known negative-red Color change");edited_roi=cropped;
  }
  if(QString(name)=="cycles-noop-control")require(cropped==before_roi,"Cycles mesh no-op reload did not exactly restore original pixels");
  if(QString(name)=="cycles-color-undone")require(cropped==before_roi,"Cycles mesh Undo did not exactly restore original pixels");
  if(QString(name)=="cycles-color-redone")require(cropped==edited_roi,"Cycles mesh Redo did not exactly restore edited pixels");
  capture_demo_window(window,viewport).save(output+"/"+name+"-workspace.png");
  if(auto *floating=window->findChild<QDialog *>("propertiesFloat"))floating->grab().save(output+"/"+name+"-properties.png");
 }
 void advance(){
  require(++ticks<3600,"BA UDIM artist proof timed out");auto s=state();
  if(stage>1&&(s["composite_pending"].toBool()||s["property_pending"].toBool()||s["render_frames"].toInt()<minimum_frame))return;
  if(s["render_ui"].toBool()){
   const auto status=s["render"].toObject()["status"].toString();require(!status.contains("error",Qt::CaseInsensitive)&&!status.contains("failed",Qt::CaseInsensitive),qPrintable(status));
   if(!status.contains("Rendering Done")){settled=0;return;}if(++settled<3)return;
  }
  if(s["render_ui"].toBool()&&!completed_draw())return;
  if(stage>=5&&stage!=9&&stage!=10&&!current()){settled=0;return;}settled=0;
  std::printf("BTS_BA_UDIM_STAGE step=%d frames=%d\n",stage,s["render_frames"].toInt());std::fflush(stdout);
  if(stage==10&&!camera_end_marked){std::puts("BTS_BA_CAMERA_END");camera_end_marked=true;}
  const char *capture_name=stage==5?(noop_pending?"cycles-noop-control":"cycles-before"):stage==6?"cycles-color-edited":stage==7?"cycles-color-undone":stage==8?"cycles-color-redone":stage==10?"cycles-camera-only":stage==12?"cycles-reentered-latest":nullptr;
  if(capture_name&&!full_tiles_ready(capture_name))return;
  if(stage==5&&noop_pending){std::puts("BTS_BA_NOOP_RELOAD_END");capture("cycles-noop-control");noop_pending=false;next_epoch();color_gesture(-.25);edited=expected;++stage;minimum_frame=state()["render_frames"].toInt()+1;return;}
  switch(stage){
   case 0:action("newProject");break;
   case 1:{auto *dialog=get<QDialog>("newProjectDialog");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);dialog->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);auto *size=dialog->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));get<QPushButton>("createProject")->click();break;}
   case 2:fixture();break;
   case 3:require_target_formats();install_controlled();get<QToolButton>("propertiesUndock")->click();break;
   case 4:std::puts("BTS_BA_UDIM_ENTRY_BEGIN");enter_observed();action("renderingMode");break;
   case 5:capture("cycles-before",true);if(controlled){next_epoch();blender::DEG_id_tag_update_ex(G_MAIN,&image(0)->id,blender::ID_RECALC_SOURCE);std::puts("BTS_BA_NOOP_RELOAD_BEGIN");noop_pending=true;viewport->update();minimum_frame=state()["render_frames"].toInt()+1;return;}color_gesture(-.25);edited=expected;break;
   case 6:capture("cycles-color-edited");next_epoch();action("undoDocument");expected={1,1,1,1};break;
   case 7:capture("cycles-color-undone");next_epoch();action("redoDocument");expected=edited;break;
   case 8:capture("cycles-color-redone");interval.start();std::puts("BTS_BA_IDLE_BEGIN");std::fflush(stdout);++stage;minimum_frame=0;return;
   case 9:if(interval.elapsed()<2000)return;std::puts("BTS_BA_IDLE_END");std::puts("BTS_BA_CAMERA_BEGIN");next_epoch();camera_gesture();break;
   case 10:capture("cycles-camera-only");action("paintingMode");break;
   case 11:color_gesture(2.5);std::puts("BTS_BA_REENTRY_BEGIN");enter_observed();action("renderingMode");break;
   case 12:capture("cycles-reentered-latest",true);action("paintingMode");break;
   case 13:cleanup_test_resources();require(!cleanup_failed,"Controlled diagnostic cleanup failed");timer.stop();std::puts("BTS_BA_UDIM_DONE");QCoreApplication::quit();return;
  }
  ++stage;minimum_frame=stage==4?0:state()["render_frames"].toInt()+1;std::fflush(stdout);
 }
 public:CyclesUdimArtist(QWidget *w,QWidget *v,QString dir,QString path,int size,std::function<QJsonObject()> read,BlenderHost *native,CyclesFixtureEdit fixture,CyclesFrameHook frame_hook):QObject(w),window(w),viewport(v),output(std::move(dir)),mesh(std::move(path)),resolution(size),host(native),edit_fixture(std::move(fixture)),snapshot(std::move(read)){
#ifdef BTS_BA_CONTROLLED_PROFILE_TEST
  controlled=qEnvironmentVariableIsSet("BTS_BA_CONTROLLED_PROFILE");if(controlled){require(bool(frame_hook),"Missing real frame observer registration");new CompletionPaintFilter(viewport,paint);frame_hook([shared=paint]{shared->draw_generation=shared->begin_generation;shared->draw_epoch=shared->begin_epoch;shared->done_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();++shared->draws;});}
#else
  require(!qEnvironmentVariableIsSet("BTS_BA_CONTROLLED_PROFILE"),"Controlled profile requires explicit test-overlay compilation");
#endif
  for(int i=0;i<12;++i)points[i]={resolution*(i%4+1)/5,resolution*(i/4+1)/4};
  connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,this,[this]{cleanup_test_resources();});
  timer.setInterval(100);connect(&timer,&QTimer::timeout,this,[this]{try{advance();}catch(const std::exception &e){timer.stop();cleanup_test_resources();std::fprintf(stderr,"BA_UDIM_FAILED %s\n",e.what());QCoreApplication::exit(1);}});timer.start();
 }
};
}}
namespace bts {void start_cycles_udim_artist_demo(QWidget *w,QWidget *v,const QString &dir,const QString &mesh,int resolution,std::function<QJsonObject()> read,BlenderHost *host,CyclesFixtureEdit fixture,CyclesFrameHook hook){new CyclesUdimArtist(w,v,dir,mesh,resolution,std::move(read),host,std::move(fixture),std::move(hook));}}
