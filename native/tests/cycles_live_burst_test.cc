// QtCore first, then epoxy/GPU headers before the fixture pulls QOpenGLWidget.
#include <QtCore>
#include "../host/blender/GpuProjectReadback.hh"
// Include once, replacing the ordinary UDIM case object in this executable.
// The sole fixture-source overlay is its friend declaration. Its own timer is
// stopped synchronously below, before any event-loop delivery is possible.
#include "cycles_live_udim_artist_test.cc"
#include "CyclesLiveBurst.hh"
#include "../host/diagnostics/PropertyLatency.hh"
#include "BKE_image_hosted.hh"
#include "BKE_image_gpu_override.hh"
#include <QCryptographicHash>
#include <QPointer>
#include <cstring>
#include <map>

namespace bts {namespace {
class CyclesLiveBurst final:public QObject {
 CyclesUdimArtist *fixture_;QTimer timer_;QElapsedTimer clock_,drag_;
 QPointer<QWidget> viewport_,wheel_;QPointer<QDialog> popup_;
 QPointer<ColorPicker> picker_;
 int stage_=0,ticks_=0,moves_=0,undo_before_=0;
 bool held_=false,queued_=false,terminal_=false;QPointF point_;
 std::uint64_t upload_before_=0;PickerColor final_{};QString set_id_;
 QJsonArray held_frames_,issued_,captures_;QJsonObject baseline_doc_;
 std::unique_ptr<GpuProjectReadback> readback_;QString readback_stage_;
 std::map<QString,QJsonArray> hashes_;
 static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
 qint64 ns()const{return clock_.nsecsElapsed();}
 void write(){QFile file(fixture_->output+"/cycles-live-burst.json");require(file.open(QIODevice::WriteOnly),"Cannot retain burst evidence");file.write(QJsonDocument(QJsonObject{{"schema",1},{"profile","held-picker-burst-6s-v1"},{"held_frames",held_frames_},{"issued",issued_},{"captures",captures_},{"moves",moves_},{"held_seconds",6},{"during_hold_gpu_readbacks",0},{"during_hold_full_tile_hashes",0},{"cpu_composite",false},{"cycles_render_completion_claim",false}}).toJson());}
 void retire(){if(readback_){fixture_->host->with_gpu([&]{readback_->clear();});readback_.reset();}}
 void retire_terminal()noexcept{try{retire();}catch(...){qFatal("Burst GPU resources could not retire before owner shutdown");}}
 void fail(const char *message){if(terminal_)return;terminal_=true;held_=false;timer_.stop();retire_terminal();try{write();}catch(...){}std::fprintf(stderr,"BTS_BA_BURST_FAILED %s\n",message);QCoreApplication::exit(1);}
 QJsonArray handles(){QJsonArray values;for(int channel:{0,2}){auto *image=fixture_->image(channel);const auto gpu=blender::BKE_image_gpu_override_texture(image);require(gpu.texture&&*gpu.texture,"Missing burst authoritative allocation");values.append(QJsonObject{{"channel",channel},{"image_uid",qint64(image->id.session_uid)},{"texture",QString::number(quintptr(*gpu.texture),16)}});}return values;}
 QJsonArray samples(){
  // Existing immutable cache references only. No GPU read, loader preparation,
  // full-buffer copy/hash, or pixel compositing occurs during the held window.
  QJsonArray values;
  for(int channel:{0,2})for(int tile:{1001,1002}){auto *image=fixture_->image(channel);blender::ImageUser user{};user.tile=tile;auto *buffer=blender::BKE_image_acquire_ibuf(image,&user,nullptr);
   struct Release{Image *image;blender::ImBuf *buffer;~Release(){blender::BKE_image_release_ibuf(image,buffer,nullptr);}} release{image,buffer};
   require(buffer&&buffer->float_data()&&buffer->channels==4&&buffer->x==fixture_->resolution&&buffer->y==fixture_->resolution,"Burst immutable cache unexpectedly missing");
   const auto [x,y]=fixture_->points[0];const auto *pixel=buffer->float_data()+(std::size_t(y)*buffer->x+x)*4;
   values.append(QJsonObject{{"channel",channel},{"tile",tile},{"image_uid",qint64(image->id.session_uid)},{"update_count",QString::number(image->runtime->update_count)},{"buffer",QString::number(quintptr(buffer),16)},{"pixel",QJsonArray{pixel[0],pixel[1],pixel[2],pixel[3]}}});
  }return values;
 }
 void send(QEvent::Type type,QPointF point){require(wheel_&&wheel_->isVisible()&&wheel_->isEnabled(),"Held Color wheel is not visible/enabled");QMouseEvent event(type,point,wheel_->mapToGlobal(point),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(wheel_,&event);}
 void issue(QEvent::Type type,QPointF point){send(type,point);const auto color=picker_->color();issued_.append(QJsonObject{{"ns",QString::number(ns())},{"kind",type==QEvent::MouseButtonPress?"press":type==QEvent::MouseButtonRelease?"release":"move"},{"xy",QJsonArray{point.x(),point.y()}},{"color",QJsonArray{color[0],color[1],color[2],color[3]}}});}
 QJsonObject owner(){
  const auto queues=fixture_->host->diagnostic_native_source_queues();const auto bindings=queues["bindings"].toObject();const auto uid=QString::number(fixture_->uid);
  require(bindings.size()==1&&bindings.contains(uid),"Burst must have only its sole fixture material binding");
  require(queues["failures"].toObject().isEmpty()&&queues["pending_cpu"].toInteger(-1)==-1&&queues["running_cpu"].toInteger(-1)==-1,"Burst producer failure/legacy CPU work makes result inconclusive");
  return {{"material_uid",qint64(fixture_->uid)},{"set_id",set_id_},{"color_image_uid",qint64(fixture_->image(0)->id.session_uid)},{"snapshot_generation",bindings[uid].toObject()["snapshot_generation"]}};
 }
 void frame(){
  if(!held_||queued_||terminal_)return;
  queued_=true;const auto self=QPointer<CyclesLiveBurst>(this);
  QMetaObject::invokeMethod(this,[self]{if(!self)return;self->queued_=false;if(!self->held_||self->terminal_)return;try{
   require(self->fixture_->get<QAction>("renderingMode")->isChecked(),"Rendering stopped during held drag");
   require(BTS_cycles_graph_count()==self->fixture_->graph&&BTS_cycles_svm_count()==self->fixture_->svm,"Held pixel edit recompiled Cycles graph/SVM");
   const auto state=self->fixture_->state();const auto render=state["render"].toObject();const auto status=render["status"].toString();
   const auto handles=self->handles();require(handles==self->captures_[0].toObject()["handles"].toArray(),"Held drag changed authoritative GPU handles");
   self->held_frames_.append(QJsonObject{{"ns",QString::number(self->ns())},{"frame",state["render_frames"]},{"render",render},{"color_upload_callbacks",QString::number(property_upload_counts[0])},{"owner",self->owner()},{"handles",handles},{"cpu",self->samples()}});
   require(!render["image_mirror_paused"].toBool()&&!status.contains("error",Qt::CaseInsensitive)&&!status.contains("failed",Qt::CaseInsensitive),"Burst renderer/mirror failed; result is an error, not revision starvation");
   if(self->drag_.elapsed()>=6000){self->release();return;}
   // Remain inside the visible SV square; use a different point on each frame.
   const int n=self->moves_++;self->point_={65.+(n%47)*1.8,72.+(n%31)*2.1};self->issue(QEvent::MouseMove,self->point_);
  }catch(const std::exception &error){self->fail(error.what());}},Qt::QueuedConnection);
 }
 void begin(){
  timer_.stop(); // Popup layout below may deliver events; do not reenter advance.
  require(property_timing_enabled()&&performance::enabled(),"Burst requires existing upload callbacks and post-write performance markers enabled at process start; no latency claim");
  picker_=static_cast<ColorPicker *>(fixture_->get<QWidget>("materialColorPicker"));auto *swatch=picker_->findChild<QPushButton *>("colorSwatch");require(swatch,"Missing visible Color swatch");
  for(auto *parent=swatch->parentWidget();parent;parent=parent->parentWidget())if(auto *scroll=qobject_cast<QScrollArea *>(parent))scroll->ensureWidgetVisible(swatch);
  require(swatch->isVisible()&&swatch->isEnabled(),"Color swatch unavailable in Rendering");swatch->click();popup_=picker_->findChild<QDialog *>("colorPickerDialog");wheel_=popup_?popup_->findChild<QWidget *>("colorWheel"):nullptr;
  QApplication::processEvents(); // Outside the measured/held interval: deliver real popup layout/paint.
  require(popup_&&popup_->isVisible()&&wheel_&&!wheel_->visibleRegion().isEmpty(),"Real Color picker did not open");
  undo_before_=fixture_->state()["undo_count"].toInt();upload_before_=property_upload_counts[0];point_={75,100};held_=true;drag_.start();
  std::printf("BTS_BA_BURST_BEGIN ns=%lld upload_callbacks=%llu\n",static_cast<long long>(ns()),static_cast<unsigned long long>(upload_before_));std::fflush(stdout);issue(QEvent::MouseButtonPress,point_);viewport_->update();
 }
 void release(){
  issue(QEvent::MouseButtonRelease,point_);final_=picker_->color();fixture_->expected=final_;held_=false;popup_->hide();
  require(moves_>=12,"Too few real held MouseMoves: burst is inconclusive");require(property_upload_counts[0]>=upload_before_+8,"Too few upload callbacks: burst is inconclusive");
  std::printf("BTS_BA_BURST_END ns=%lld moves=%d upload_callbacks=%llu\n",static_cast<long long>(ns()),moves_,static_cast<unsigned long long>(property_upload_counts[0]-upload_before_));std::fflush(stdout);write();stage_=7;viewport_->update();
 }
 bool full(const QString &name){
  require(!held_,"Full readback attempted during held drag");if(hashes_.contains(name))return true;
  if(!readback_){const auto bytes=resource_product(resource_product(fixture_->resolution,fixture_->resolution),16*4);readback_=std::make_unique<GpuProjectReadback>();readback_stage_=name;
   fixture_->host->with_gpu([&]{for(int channel:{0,2})for(int tile:{1001,1002}){auto *image=fixture_->image(channel);const auto gpu=blender::BKE_image_gpu_override_texture(image);blender::ImageGpuTileRegion region;require(gpu.texture&&*gpu.texture&&blender::BKE_image_gpu_override_tile_region(image,tile,region),"Missing actual burst tile region");require(region.width==fixture_->resolution&&region.height==fixture_->resolution,"Wrong burst tile extent");readback_->submit(channel,tile,*gpu.texture,region.x,region.y,region.layer,region.width,region.height);}});return false;
  }
  require(readback_stage_==name,"Post-gesture stage changed mid-readback");bool ready=false;fixture_->host->with_gpu([&]{ready=readback_->poll();});if(!ready)return false;
  QJsonArray hashes;for(const auto &gpu:readback_->images()){auto *image=fixture_->image(int(gpu.uid));blender::ImageUser user{};user.tile=gpu.tile;auto *cpu=blender::BKE_image_acquire_ibuf(image,&user,nullptr);struct Release{Image *image;blender::ImBuf *buffer;~Release(){blender::BKE_image_release_ibuf(image,buffer,nullptr);}} release{image,cpu};
   require(cpu&&cpu->float_data()&&cpu->channels==4&&cpu->x==gpu.width&&cpu->y==gpu.height,"Post-gesture CPU tile missing");const auto bytes=gpu.bytes();
   if(gpu.uniform){for(std::size_t i=0;i<std::size_t(gpu.width)*gpu.height;++i)require(std::memcmp(gpu.pixels,cpu->float_data()+i*4,16)==0,"Uniform full GPU/CPU bytes differ");}
   else require(std::memcmp(gpu.pixels,cpu->float_data(),bytes)==0,"Post-gesture full GPU/CPU bytes differ");
   const QByteArrayView raw(reinterpret_cast<const char *>(cpu->float_data()),qsizetype(bytes));hashes.append(QJsonObject{{"channel",int(gpu.uid)},{"tile",gpu.tile},{"bytes",QString::number(bytes)},{"sha256",QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex())},{"gpu_cpu_bit_exact",true}});
  }retire();hashes_[name]=hashes;return true;
 }
 void capture(const QString &name){
  const auto metadata=fixture_->state(true);if(name=="baseline"){baseline_doc_=metadata["document"].toObject();const auto sets=baseline_doc_["texture_sets"].toArray();require(sets.size()==1,"Burst has unrelated Texture Sets");set_id_=sets[0].toObject()["id"].toString();require(!set_id_.isEmpty(),"Missing burst set identity");}
  if(name=="undone")require(metadata["document"].toObject()==baseline_doc_,"Burst Undo document did not restore baseline");
  const auto probes=fixture_->probe(true);if(!captures_.isEmpty())require(handles()==captures_[0].toObject()["handles"].toArray(),"Burst changed authoritative GPU allocations");
  if(!captures_.isEmpty())require(BTS_cycles_graph_count()==fixture_->graph&&BTS_cycles_svm_count()==fixture_->svm,"Burst/release/Undo recompiled Cycles graph/SVM");
  else{fixture_->graph=BTS_cycles_graph_count();fixture_->svm=BTS_cycles_svm_count();}
  captures_.append(QJsonObject{{"stage",name},{"ns",QString::number(ns())},{"owner",owner()},{"handles",handles()},{"probes",probes},{"full_tiles",hashes_.at(name)},{"undo_count",metadata["undo_count"]},{"graph",QString::number(BTS_cycles_graph_count())},{"svm",QString::number(BTS_cycles_svm_count())}});write();
 }
 void advance(){
  require(++ticks_<3600,"Burst diagnostic timed out");if(held_)return;auto state=fixture_->state();if(stage_>1&&(state["composite_pending"].toBool()||state["property_pending"].toBool()))return;
  if(stage_>=5&&(!state["render"].toObject()["status"].toString().contains("Rendering Done")||!fixture_->current()))return;
  switch(stage_){
   case 0:fixture_->action("newProject");break;
   case 1:{auto *dialog=fixture_->get<QDialog>("newProjectDialog");dialog->findChild<QLineEdit *>("meshPath")->setText(fixture_->mesh);dialog->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);auto *size=dialog->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(fixture_->resolution));fixture_->get<QPushButton>("createProject")->click();break;}
   case 2:fixture_->fixture();break;
   case 3:fixture_->require_target_formats();fixture_->get<QToolButton>("propertiesUndock")->click();break;
   case 4:fixture_->action("renderingMode");break;
   case 5:if(!full("baseline"))return;capture("baseline");begin();stage_=6;timer_.start();return;
   case 7:if(!full("released"))return;capture("released");require(fixture_->state()["undo_count"].toInt()==undo_before_+1,"Held drag did not produce exactly one Undo");fixture_->action("undoDocument");fixture_->expected={1,1,1,1};break;
   case 8:if(!full("undone"))return;capture("undone");require(hashes_.at("undone")==hashes_.at("baseline"),"Burst Undo full bytes did not restore baseline");fixture_->action("redoDocument");fixture_->expected=final_;break;
   case 9:if(!full("redone"))return;capture("redone");require(hashes_.at("redone")==hashes_.at("released"),"Burst Redo full bytes did not restore final value");terminal_=true;timer_.stop();std::puts("BTS_BA_BURST_DONE");QCoreApplication::quit();return;
  }++stage_;
 }
 public:CyclesLiveBurst(QWidget *window,QWidget *viewport,const QString &out,const QString &mesh,int resolution,std::function<QJsonObject()> snapshot,BlenderHost *host,CyclesFixtureEdit edit,CyclesBurstFrameHook hook):QObject(window),viewport_(viewport){
  require(bool(hook),"Burst needs actual frame_done registration");fixture_=new CyclesUdimArtist(window,viewport,out,mesh,resolution,std::move(snapshot),host,std::move(edit),CyclesFrameHook{});fixture_->timer.stop();clock_.start();
  hook([self=QPointer<CyclesLiveBurst>(this)]{if(self)self->frame();});
  connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,this,[this]{terminal_=true;held_=false;timer_.stop();retire_terminal();});
  timer_.setInterval(50);connect(&timer_,&QTimer::timeout,this,[this]{try{advance();}catch(const std::exception &error){fail(error.what());}});timer_.start();
 }
};
}}
namespace bts {void start_cycles_live_burst(QWidget *window,QWidget *viewport,const QString &out,const QString &mesh,int resolution,std::function<QJsonObject()> snapshot,BlenderHost *host,CyclesFixtureEdit edit,CyclesBurstFrameHook hook){new CyclesLiveBurst(window,viewport,out,mesh,resolution,std::move(snapshot),host,std::move(edit),std::move(hook));}}
