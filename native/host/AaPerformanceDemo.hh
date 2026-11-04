#pragma once
#include "Performance.hh"
#include "PropertyLatency.hh"
#include "GuiFileSelection.hh"
#include <cmath>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
namespace bts {
// Invisible native Qt workspace benchmark. Full CPU oracles run only after
// timed input; the polling snapshot intentionally excludes pixel hashes.
class AaPerformanceDemo final:public QObject {
 QWidget *w_,*v_,*wheel_=nullptr;QString dir_,mesh_;std::function<QJsonObject()> state_;QTimer timer_,ping_;QElapsedTimer heartbeat_,clock_,deadline_;QJsonArray calls_,pings_,boundaries_,modal_log_;std::vector<std::pair<double,double>> modal_windows_;double modal_started_=-1;std::vector<performance::Record> history_;int stage_=0,index_=0,stroke_=0,setup_=0;QString flow_;QSlider *slider_=nullptr;QPointF point_;int undo_before_=0;QString paint_id_;bool stroke_oracle_checked_=false;int repeat_seconds_=qEnvironmentVariableIntValue("BTS_AB_RESOLUTION_SECONDS");
 template<class T>T *get(const char *name){auto *value=w_->findChild<T *>(name);if(!value)qFatal("AA missing control: %s",name);return value;}
 void action(const char *name){get<QAction>(name)->trigger();}
 QJsonObject probe(){w_->setProperty("aaProbeOnly",true);auto s=state_();w_->setProperty("aaProbeOnly",false);return s;}
 bool pending(){const auto s=probe();return s["composite_pending"].toBool()||s["property_pending"].toBool()||s["texture_loading"].toBool();}
 void collect(){std::lock_guard lock(performance::mutex);for(auto &record:performance::records)history_.push_back(std::move(record));performance::records.clear();}
 QJsonArray phases(){collect();QJsonArray out;for(const auto &r:history_)out.append(QJsonObject{{"flow",QString::fromStdString(r.flow)},{"phase",QString::fromStdString(r.phase)},{"wall_ms",r.wall},{"cpu_ms",r.cpu},{"queue_ms",r.queue},{"ui",r.ui}});return out;}
 void flow(const char *label){collect();modal_windows_.clear();modal_started_=-1;flow_=label;performance::begin(label);clock_.restart();index_=0;heartbeat_.restart();}
 template<class F>void timed(F fn){const auto start=performance::Clock::now();performance::Scope timing("aa_input_handler");fn();calls_.append(QJsonObject{{"flow",flow_},{"wall_ms",performance::ms(start)}});}
 void mouse(QEvent::Type type,QPointF point,QWidget *target=nullptr){if(!target)target=v_;QMouseEvent event(type,point,target->mapToGlobal(point.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(target,&event);}
 void boundary(const char *name){collect();std::printf("BTS_AA_STAGE stage=%s wall_ms=%.3f\n",name,clock_.nsecsElapsed()/1e6);boundaries_.append(QJsonObject{{"stage",name},{"undo_count",probe()["undo_count"]},{"wall_ms",clock_.nsecsElapsed()/1e6}});}
 void start_slider(const char *name,const char *label){flow(label);slider_=get<QSlider>(name);undo_before_=probe()["undo_count"].toInt();slider_->setSliderDown(true);}
 void advance(){
 if(deadline_.elapsed()>std::max(600000,(repeat_seconds_+120)*1000))qFatal("AA benchmark timeout at stage %d",stage_);
 switch(stage_){
 case 0:if(qEnvironmentVariableIsSet("BTS_AA_PROJECT")){if(!probe()["has_document"].toBool()||pending())return;stage_=2;return;}action("newProject");stage_=1;return;
 case 1:{auto *d=get<QDialog>("newProjectDialog");d->findChild<QLineEdit *>("meshPath")->setText(mesh_);auto *size=d->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(2048));d->findChild<QPushButton *>("createProject")->click();stage_=2;return;}
 case 2:if(pending())return;action("guiAaPrepare");get<QToolButton>("addPaintLayer")->click();stage_=3;return;
 case 3:if(pending())return;if(paint_id_.isEmpty())paint_id_=get<QListWidget>("layerStack")->currentItem()->data(Qt::UserRole).toString();get<QDoubleSpinBox>("brushSize")->setValue(3);get<QDoubleSpinBox>("brushSpacing")->setValue(10);flow("aa_stroke");point_={v_->width()*.38,v_->height()*.5};stage_=4;return;
 case 4:timed([&]{if(index_==0)mouse(QEvent::MouseButtonPress,point_);else if(index_<=20)mouse(QEvent::MouseMove,point_+QPointF(index_*2,12*std::sin(index_*.25+stroke_*.15)));else mouse(QEvent::MouseButtonRelease,point_+QPointF(40,12*std::sin(5+stroke_*.15)));});if(++index_>21){index_=0;if(++stroke_==20){boundary("20-strokes-20-moves");stage_=5;}}return;
 case 5:if(setup_>=10){stage_=6;return;}if(setup_==0&&!stroke_oracle_checked_){if(pending())return;boundary("stroke-gpu-settled");{QFile checkpoint(dir_+"/aa-stroke-timing.json");if(!checkpoint.open(QIODevice::WriteOnly))qFatal("AA stroke timing output");checkpoint.write(QJsonDocument(QJsonObject{{"calls",calls_},{"pings",pings_},{"phases",phases()},{"boundaries",boundaries_}}).toJson());}flow("");if(repeat_seconds_==0)action("checkGuiGpuStack");stroke_oracle_checked_=true;return;}if(setup_==0)flow("aa_layer_setup");timed([&]{get<QToolButton>("addFillLayer")->click();});if(++setup_==10)stage_=6;return;
 case 6:if(pending())return;flow("aa_layer_select");stage_=7;return;
 case 7:timed([&]{auto *list=get<QListWidget>("layerStack");list->setCurrentRow(index_%list->count());});if(++index_==10){boundary("10-layer-selections");stage_=8;}return;
 case 8:if(pending())return;get<QListWidget>("layerStack")->setCurrentRow(0);flow("aa_opacity");action("editLayerOpacity");start_slider("layerChannelOpacitySlider","aa_opacity");stage_=9;return;
 case 9:timed([&]{property_event("opacity");slider_->setValue(200+index_*20);});if(++index_==30){timed([&]{slider_->setSliderDown(false);slider_->window()->hide();});stage_=10;boundary("opacity-release");}return;
 case 10:if(pending())return;start_slider("roughnessSlider","aa_roughness");stage_=11;return;
 case 11:timed([&]{property_event("roughness");slider_->setValue(200+index_*20);});if(++index_==30){timed([&]{slider_->setSliderDown(false);});stage_=12;boundary("roughness-release");}return;
 case 12:if(pending())return;flow("aa_color");get<QWidget>("materialColorPicker")->findChild<QPushButton *>("colorSwatch")->click();wheel_=get<QDialog>("colorPickerDialog")->findChild<QWidget *>("colorWheel");mouse(QEvent::MouseButtonPress,{65,100},wheel_);stage_=13;return;
 case 13:timed([&]{property_event("color");mouse(QEvent::MouseMove,{65+index_*2.5,100.0-index_},wheel_);});if(++index_==30){timed([&]{mouse(QEvent::MouseButtonRelease,{140,70},wheel_);get<QDialog>("colorPickerDialog")->hide();});stage_=14;boundary("color-release");}return;
 case 14:if(pending())return;flow("aa_combobox");timed([&]{get<QComboBox>("fillProjectionMode")->setCurrentIndex(6);});stage_=15;return;
 case 15:if(pending())return;timed([&]{get<QComboBox>("fillProjectionMode")->setCurrentIndex(0);});stage_=16;return;
 case 16:if(pending())return;flow("aa_import");{const auto image=qEnvironmentVariable("BTS_AA_IMAGE");if(image.isEmpty()){stage_=18;return;}select_gui_test_file(w_,"fillTextureFileDialog",image);timed([&]{get<QComboBox>("fillBaseColorResource")->setCurrentIndex(2);});}stage_=17;return;
 case 17:if(pending())return;boundary("2x4K-import-final");stage_=18;return;
 case 18:if(pending())return;if(repeat_seconds_>0&&deadline_.elapsed()<repeat_seconds_*1000){flow("");stroke_=0;auto *list=get<QListWidget>("layerStack");for(int row=0;row<list->count();++row)if(list->item(row)->data(Qt::UserRole).toString()==paint_id_){list->setCurrentRow(row);break;}stage_=3;return;}timer_.stop();ping_.stop();performance::begin("");if(repeat_seconds_==0)action("checkGuiGpuStack");{QFile file(dir_+"/aa-performance.json");if(!file.open(QIODevice::WriteOnly))qFatal("AA proof output");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"cpu_oracle_in_this_run",repeat_seconds_==0},{"calls",calls_},{"ping_intervals",pings_},{"modal_windows",modal_log_},{"boundaries",boundaries_},{"phases",phases()},{"final",repeat_seconds_>0?probe():state_()}}).toJson());file.close();}QTimer::singleShot(100,qApp,&QCoreApplication::quit);return;
 }
 }
 protected:
 bool eventFilter(QObject *object,QEvent *event) override {
  if(flow_=="aa_import"&&object->objectName()=="fillTextureFileDialog"){
   const double now=clock_.nsecsElapsed()/1e6;
   if(event->type()==QEvent::Show&&modal_started_<0)modal_started_=now;
   if(event->type()==QEvent::Hide&&modal_started_>=0){
    modal_windows_.emplace_back(modal_started_,now);
    modal_log_.append(QJsonObject{{"flow",flow_},{"begin_ms",modal_started_},{"end_ms",now}});modal_started_=-1;
   }
  }
  return QObject::eventFilter(object,event);
 }
 public:AaPerformanceDemo(QWidget *w,QWidget *v,const QString &dir,const QString &mesh,std::function<QJsonObject()> state):QObject(w),w_(w),v_(v),dir_(dir),mesh_(mesh),state_(std::move(state)){QDir().mkpath(dir_);qApp->installEventFilter(this);deadline_.start();heartbeat_.start();clock_.start();timer_.setInterval(16);connect(&timer_,&QTimer::timeout,this,[this]{advance();});ping_.setInterval(10);ping_.setTimerType(Qt::PreciseTimer);connect(&ping_,&QTimer::timeout,this,[this]{const auto interval=heartbeat_.nsecsElapsed()/1e6;heartbeat_.restart();if(!flow_.isEmpty()){
 const double end=clock_.nsecsElapsed()/1e6,begin=end-interval;double modal=0;
 const auto overlap=[&](double first,double last){return std::max(0.,std::min(end,last)-std::max(begin,first));};
 for(const auto &[first,last]:modal_windows_)modal+=overlap(first,last);if(modal_started_>=0)modal+=overlap(modal_started_,end);
 modal=std::clamp(modal,0.,interval);
 pings_.append(QJsonObject{{"flow",flow_},{"stage",stage_},{"flow_elapsed_ms",end},{"interval_ms",interval},{"modal_overlap_ms",modal},{"nonmodal_interval_ms",interval-modal}});
 }});timer_.start();ping_.start();}
};
}
