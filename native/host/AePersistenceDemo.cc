#include "AePersistenceDemo.hh"
#include "Performance.hh"
#include "ColorPicker.hh"
#include <QImage>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
class Demo final:public QObject {
 QWidget *window_,*channel_parent_=nullptr;QString directory_,mesh_;QTimer timer_,ping_;QElapsedTimer clock_,heartbeat_,operation_;
 std::size_t first_record_=0;int stage_=0,set_=0,layer_=0;bool measuring_=false;int settled_=0;QColor set_a_color_;double max_ping_=0;QJsonArray operations_;QJsonObject final_;
 template<class T>T *get(const char *name){auto *value=window_->findChild<T *>(name);if(!value)qFatal("AE missing control: %s",name);return value;}
 void action(const char *name){get<QAction>(name)->trigger();}
 QJsonObject state(){action("guiAeState");return window_->property("aeState").value<QJsonObject>();}
 template<class F>void timed(const char *label,F &&fn){performance::begin(label);const auto start=performance::Clock::now();fn();operations_.append(QJsonObject{{"operation",label},{"handler_ms",performance::ms(start)}});}
 QColor capture_pixel(const char *label){
  const auto path=directory_+"/"+label+".png";window_->setProperty("guiCapturePath",path);action("guiCaptureViewport");QImage image(path);if(image.isNull())qFatal("AE viewport capture is empty");return image.pixelColor(image.width()/2+31,image.height()/2+29);
 }
 void reopen(){qputenv("BTS_AE_PROJECT",(directory_+"/ae.blend").toUtf8());action("guiAeOpen");stage_=9;}
 void finish(const QString &error={}){
  timer_.stop();ping_.stop();QFile file(directory_+"/ae-persistence.json");if(!file.open(QIODevice::WriteOnly))qFatal("AE proof output failed");
  file.write(QJsonDocument(QJsonObject{{"error",error},{"operations",operations_},{"max_ping_ms",max_ping_},{"final",final_},{"phases",performance::snapshot()},{"duration_ms",clock_.elapsed()}}).toJson());file.close();QCoreApplication::exit(error.isEmpty()?0:1);
 }
 void tick(){
  if(get<QWidget>("fillChannelsPanel")->parentWidget()!=channel_parent_){finish("Channel controls were reparented during layer selection");return;}
  if(clock_.elapsed()>180000){finish("AE persistence timeout");return;}
  const auto current=state();
  switch(stage_){
   case 0:if(qEnvironmentVariableIsSet("BTS_AE_PROJECT")){action("guiAeOpen");stage_=2;}else{action("newProject");stage_=1;}return;
   case 1:{auto *dialog=get<QDialog>("newProjectDialog");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh_);auto *resolution=dialog->findChild<QComboBox *>("documentResolution");const int requested=qEnvironmentVariableIntValue("BTS_AE_RESOLUTION");resolution->setCurrentIndex(resolution->findData(requested?requested:4096));dialog->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);dialog->findChild<QPushButton *>("createProject")->click();stage_=2;return;}
   case 2:if(current["gpu_pending"].toBool())return;{std::lock_guard lock(performance::mutex);first_record_=performance::records.size();}std::puts("BTS_AE_INTERACTION_BEGIN");std::fflush(stdout);performance::begin("ae_new_fill");heartbeat_.restart();measuring_=true;if(qEnvironmentVariableIsSet("BTS_AE_PAINT"))timed("ae_paint",[&]{action("guiAePaint");});stage_=3;return;
   case 3:
    if(current["gpu_pending"].toBool())return;
    if(layer_++<4){timed("ae_new_fill",[&]{get<QToolButton>("addFillLayer")->click();});return;}
    if(++set_<current["sets"].toArray().size()){get<QListWidget>("textureSets")->setCurrentRow(set_);layer_=0;return;}
    timed("ae_manual_save",[&]{action("guiAeSave");});operation_.start();stage_=4;return;
   case 4:
    if(current["save_pending"].toBool())return;
    if(!QFile::exists(directory_+"/ae.blend")){finish("Manual save did not produce a file: "+current["save_error"].toString());return;}
    operations_.append(QJsonObject{{"operation","ae_manual_save_total"},{"wall_ms",operation_.elapsed()}});
    timed("ae_property",[&]{auto *rough=get<QSlider>("roughnessSlider");rough->setSliderDown(true);rough->setValue(730);rough->setSliderDown(false);});operation_.restart();stage_=5;return;
   case 5:
    if(current["save_pending"].toBool()||current["gpu_pending"].toBool()||current["recovered_revision"]!=current["revision"])return;
    operations_.append(QJsonObject{{"operation","ae_autosave_total_including_idle_delay"},{"wall_ms",operation_.elapsed()}});final_=current;
    measuring_=false;ping_.stop();{QJsonObject maxima;std::lock_guard lock(performance::mutex);for(std::size_t i=first_record_;i<performance::records.size();++i){const auto &r=performance::records[i];if(!r.ui)continue;const auto key=QString::fromStdString(r.phase);maxima[key]=std::max(maxima[key].toDouble(),r.wall);}final_["interaction_ui_phase_max_ms"]=maxima;}std::puts("BTS_AE_INTERACTION_END");std::fflush(stdout);
    if(current["sets"].toArray().size()<2){final_["uv_set_switch_pixels_checked"]=false;reopen();return;}
    get<QComboBox>("viewLayoutControl")->setCurrentIndex(1);get<QListWidget>("textureSets")->setCurrentRow(0);static_cast<ColorPicker *>(get<QWidget>("materialColorPicker"))->accept_sample({.6f,.02f,.02f,1});settled_=0;stage_=6;return;
   case 6:
    if(current["gpu_pending"].toBool()||++settled_<2)return;
    set_a_color_=capture_pixel("ae-uv-set-A");get<QListWidget>("textureSets")->setCurrentRow(1);settled_=0;stage_=7;return;
   case 7:{
    if(current["gpu_pending"].toBool()||++settled_<2)return;
    const auto other=capture_pixel("ae-uv-set-B");const int delta=std::max({std::abs(other.red()-set_a_color_.red()),std::abs(other.green()-set_a_color_.green()),std::abs(other.blue()-set_a_color_.blue())});
    if(delta<20){finish("UV renderer retained the preceding texture set image");return;}
    final_["uv_set_color_difference_codes"]=delta;get<QListWidget>("textureSets")->setCurrentRow(0);settled_=0;stage_=8;return;
   }
   case 8:{
    if(current["gpu_pending"].toBool()||++settled_<2)return;
    const auto returned=capture_pixel("ae-uv-set-A-return");const int delta=std::max({std::abs(returned.red()-set_a_color_.red()),std::abs(returned.green()-set_a_color_.green()),std::abs(returned.blue()-set_a_color_.blue())});
    if(delta>2){finish("UV renderer changed pixels after returning to the texture set");return;}
    final_["uv_set_switch_pixels_checked"]=true;final_["uv_return_difference_codes"]=delta;reopen();return;
   }
   case 9:
    if(current["gpu_pending"].toBool())return;
    if(current["sets"]!=final_["sets"]){finish("Native reopen changed texture-set dimensions, tiles or layer counts");return;}
    final_["native_reopen"]=true;final_["channel_controls_parent_stable"]=true;finish();return;
  }
 }
 public:Demo(QWidget *window,const QString &directory,const QString &mesh):QObject(window),window_(window),directory_(directory),mesh_(mesh){QDir().mkpath(directory_);channel_parent_=get<QWidget>("fillChannelsPanel")->parentWidget();clock_.start();heartbeat_.start();timer_.setInterval(16);connect(&timer_,&QTimer::timeout,this,[this]{tick();});ping_.setInterval(10);ping_.setTimerType(Qt::PreciseTimer);connect(&ping_,&QTimer::timeout,this,[this]{const auto elapsed=heartbeat_.nsecsElapsed()/1e6;heartbeat_.restart();if(measuring_){max_ping_=std::max(max_ping_,elapsed);if(elapsed>40)std::printf("BTS_AE_HEARTBEAT wall_ms=%.3f stage=%d\n",elapsed,stage_);}});ping_.start();timer_.start();}
};
}
void start_ae_persistence_demo(QWidget *w,const QString &directory,const QString &mesh){new Demo(w,directory,mesh);}
}
