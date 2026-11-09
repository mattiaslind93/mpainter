// SPDX-License-Identifier: GPL-3.0-or-later
// Replace only GuiFixDemo.cc.o in the coherent ordinary host. All project,
// policy, candidate publication and history callbacks are production main.
#include <QtCore>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QPixmap>
#include <QRegion>
#include <QScrollArea>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include "GuiFixDemo.hh"
#include "ChannelPrecision.hh"
#include "ViewportDemoCapture.hh"
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "BLI_listbase.h"
#include "DNA_image_types.h"
#include "GPU_texture.hh"
#include "IMB_colormanagement.hh"
#include "IMB_imbuf.hh"
#include <cmath>
#include <algorithm>
#include <memory>

extern "C" unsigned long long BTS_cycles_graph_count();
extern "C" unsigned long long BTS_cycles_svm_count();

namespace bts {namespace {
using blender::G;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
QJsonObject recorded_height_actor_context(){
 using namespace blender;QJsonArray recorded,active;const auto actual=IMB_colormanagement_get_scene_linear_to_xyz();
 if(G_MAIN)for(int c=0;c<3;++c)for(int r=0;r<3;++r)recorded.append(G_MAIN->colorspace.scene_linear_to_xyz[c][r]);
 for(int c=0;c<3;++c)for(int r=0;r<3;++r)active.append(actual[c][r]);
 return {{"recorded_name",G_MAIN?QString::fromUtf8(G_MAIN->colorspace.scene_linear_name):QString()},{"recorded_scene_linear_to_xyz",recorded},{"active_name",QString::fromUtf8(IMB_colormanagement_working_space_get())},{"active_scene_linear_to_xyz",active},{"context_modified",false}};
}

// Bare production mode never calls start_gui_fix_demo. A test-only startup
// timer observes its real private GPU framebuffer without creating a shell.
void install_bare_height_smoke(){
 if(!qEnvironmentVariableIsSet("BTS_NATIVE_HEIGHT_BARE_SMOKE"))return;
 QTimer::singleShot(0,qApp,[]{
  auto *timer=new QTimer(qApp);auto clock=std::make_shared<QElapsedTimer>();clock->start();
  QObject::connect(timer,&QTimer::timeout,qApp,[timer,clock]{try{
   check(clock->elapsed()<120000,"Bare viewport did not reach its private GPU framebuffer");
   const auto widgets=QApplication::allWidgets();const bool workspace=std::any_of(widgets.begin(),widgets.end(),[](const QWidget *widget){return widget->objectName()=="layerSettingsTabs";});
   check(workspace==qEnvironmentVariableIsSet("BTS_NATIVE_HEIGHT_STARTUP_EXPECT_WORKSPACE"),"Startup smoke constructed the wrong actual WorkspaceShell mode");
   for(auto *widget:widgets){
    auto *capture=dynamic_cast<ViewportCapture *>(widget);if(!capture||!widget->property("headlessGpuReady").toBool())continue;
    const auto image=capture->capture_framebuffer();check(!image.isNull()&&image.width()>0&&image.height()>0,"Initialized bare viewport has no positive-sized private framebuffer");
    const auto directory=qEnvironmentVariable("BTS_NATIVE_HEIGHT_BARE_OUTPUT");check(!directory.isEmpty(),"Bare smoke output path missing");QDir().mkpath(directory);QSaveFile file(directory+"/bare-verified.json");const QJsonObject proof{{"workspace",workspace},{"new_project_requested",false},{"private_gpu_framebuffer_ready",true},{"headless_gpu_ready_property",true},{"read_only_current_context",recorded_height_actor_context()},{"width",image.width()},{"height",image.height()},{"diagnostic_framebuffer_readbacks",1},{"desktop_grab_or_input",false},{"performance_claim",false}};check(file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(proof).toJson())>=0&&file.commit(),"Cannot record bare smoke");timer->stop();std::puts("HEIGHT_POLICY_BARE PASS");QCoreApplication::quit();return;
   }
  }catch(const std::exception &error){timer->stop();std::fprintf(stderr,"HEIGHT_POLICY_BARE_FAILED %s\n",error.what());QCoreApplication::exit(1);}});timer->start(30);
 });
}
Q_COREAPP_STARTUP_FUNCTION(install_bare_height_smoke)
class NativeHeightArtist final:public QObject {
 QWidget *window_,*viewport_;QString directory_,mesh_,library_,project_;std::function<QJsonObject()> snapshot_;
 QTimer timer_,dialogs_;int stage_=0,last_stage_=-1,after_frame_=0,history_=0,settle_=0;bool busy_=false,staged_cancel_seen_=false,properties_revealed_=false;
 const QString quit_mode_=qEnvironmentVariable("BTS_NATIVE_HEIGHT_ACTUAL_QUIT");
 const bool rendering_=qEnvironmentVariableIsSet("BTS_NATIVE_HEIGHT_RENDERING");
 QElapsedTimer lifetime_;QJsonObject rejected_,initial_,raw_,off_,gpu_stage_witness_;QJsonArray captures_,raw_pixels_;
 QJsonArray rendering_handles_,normal_cpu_,normal_gpu_;QJsonValue rendering_catalog_;
 QImage rendering_before_,rendering_edited_;unsigned long long rendering_graph_=0,rendering_svm_=0;
 QString fixture_name()const{return rendering_?"Native Rendering scalar fixture":"Native raw Height fixture";}
 template<class T>T *control(const char *name){auto *value=window_->findChild<T *>(name);if(!value)throw std::runtime_error(std::string("Missing raw Height artist control: ")+name);return value;}
 void action(const char *name){control<QAction>(name)->trigger();}
 QJsonObject state(){return snapshot_();}
 QJsonObject native(const QJsonObject &s){for(const auto &set:s["document"].toObject()["texture_sets"].toArray())for(const auto &layer:set.toObject()["layers"].toArray()){const auto d=layer.toObject()["fill_projection"].toObject()["native_material"];if(d.isObject())return d.toObject();}throw std::runtime_error("Artist document has no native Height target");}
 bool is_raw(const QJsonObject &s){return native(s)["height_policy"]=="authored-height-raw-v1";}
 QJsonObject admission(const QJsonObject &s){QJsonObject result;for(const char *key:{"document","undo_count","redo_count","document_revision","native_mapping_queues","native_mapping_catalog","native_mapping_property"}){check(!s[key].isUndefined(),"Raw Height actor admission instrumentation missing");result[key]=s[key];}return result;}
 void unchanged(){const auto now=state();check(admission(now)==rejected_,"Cancelled/rejected Height request changed document/history/catalog/queue/gesture");check(!is_raw(now),"Cancelled raw policy activated later");auto *choice=control<QComboBox>("nativeHeightPolicy");check(choice->isEnabled()&&choice->currentIndex()==0,"Cancelled Height control did not reset to Off");check(!control<QPushButton>("cancelNativeHeight")->isVisible(),"Cancelled Height request left Cancel visible");}
 void request(int index){auto *choice=control<QComboBox>("nativeHeightPolicy");check(choice->isEnabled()&&choice->isVisible(),"Raw Height selector is not an artist-visible enabled control");choice->setCurrentIndex(index);check(QMetaObject::invokeMethod(choice,"activated",Qt::DirectConnection,Q_ARG(int,index)),"Cannot activate actual Height combo signal");}
 bool pending(){auto *choice=window_->findChild<QComboBox *>("nativeHeightPolicy");return choice&&!choice->isEnabled();}
 double parameter(const QJsonObject &description,const char *name){for(const auto &entry:description["parameters"].toArray()){const auto p=entry.toObject();if(p["name"]==name){const auto values=p["value"].toArray();check(values.size()==1&&values[0].isDouble()&&std::isfinite(values[0].toDouble()),"Artist scalar descriptor is malformed");return values[0].toDouble();}}throw std::runtime_error("Artist scalar parameter missing");}
 void edit_scalar(const char *name,double value){for(const auto &entry:native(state())["parameters"].toArray()){const auto p=entry.toObject();if(p["name"]!=name)continue;auto *number=window_->findChild<QDoubleSpinBox *>("nativeValue_"+p["id"].toString()+"_0");check(number&&number->isVisible()&&number->isEnabled(),"Native Height scalar editor is not actionable");if(rendering_)check(!number->visibleRegion().isEmpty(),"Native Rendering gesture control is clipped");number->setFocus();auto *edit=number->findChild<QLineEdit *>();check(edit,"Native Height scalar editor has no text input");QKeyEvent select(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier,"a"),text(QEvent::KeyPress,Qt::Key_0,Qt::NoModifier,QString::number(value,'g',8)),enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&select);QApplication::sendEvent(edit,&text);QApplication::sendEvent(edit,&enter);return;}throw std::runtime_error("Native Height scalar widget missing");}
 QString rendering_uid(const QJsonObject &s){const auto bindings=s["native_mapping_queues"].toObject()["bindings"].toObject();check(bindings.size()==1,"Native Rendering fixture must have one material and two owners");return bindings.begin().key();}
 blender::Image *rendering_image(const QString &uid,const char *role){const auto name=("BTS Composite "+uid+" "+role).toUtf8();for(auto &image:G_MAIN->images)if(name==image.id.name+2)return &image;throw std::runtime_error("Native Rendering canonical image missing");}
 QJsonArray rendering_pixels(const QJsonObject &s,double wanted){
  const auto uid=rendering_uid(s);const auto tiles=s["image_tiles"].toArray();if(tiles.size()!=2)return {};
  QJsonArray result;
  for(int i=0;i<2;++i){const auto row=tiles[i].toObject();check(row["tile"]==1001+i,"Native Rendering UDIM identity changed");const auto samples=row["samples"].toArray();if(samples.size()!=4)return {};
   for(int channel:{2,4}){const auto value=samples[channel==2?2:3];if(!value.isArray()||value.toArray().size()!=4)return {};const auto gpu=value.toArray();
    auto *image=rendering_image(uid,channel==2?"Roughness":"Bump");const auto texture=blender::BKE_image_gpu_override_texture(image);if(!texture.texture||!*texture.texture)return {};
    check(blender::GPU_texture_format(*texture.texture)==blender::gpu::TextureFormat::SFLOAT_32_32_32_32,"Native Rendering requested L32F target is not actual float32 RGBA");
    blender::ImageUser user{};user.tile=1001+i;auto *cpu=blender::BKE_image_acquire_ibuf(image,&user,nullptr);const auto release=qScopeGuard([&]{blender::BKE_image_release_ibuf(image,cpu,nullptr);});if(!cpu||!cpu->float_data())return {};
    check(cpu->x==128&&cpu->y==128&&cpu->channels==4,"Native Rendering CPU mirror has wrong shape");QJsonArray mirror;
    for(int c=0;c<4;++c){check(gpu[c].isDouble()&&std::isfinite(gpu[c].toDouble()),"Native Rendering GPU scalar is malformed/nonfinite");const float value=cpu->float_data()[(64*128+64)*4+c];check(std::isfinite(value),"Native Rendering CPU scalar is nonfinite");if(std::abs(gpu[c].toDouble()-(c==3?1:wanted))>=3e-5||gpu[c].toDouble()!=double(value))return {};mirror.append(value);}
    QJsonArray footprint;if(channel==4)for(int y=63;y<=65;++y)for(int x=63;x<=65;++x){QJsonArray pixel;for(int c=0;c<4;++c){const float value=cpu->float_data()[(y*128+x)*4+c];check(std::isfinite(value),"Constant H footprint is nonfinite");if(std::abs(double(value)-(c==3?1:wanted))>=3e-5)return {};pixel.append(value);}footprint.append(pixel);}
    result.append(QJsonObject{{"tile",1001+i},{"channel",channel},{"xy",QJsonArray{64,64}},{"gpu",gpu},{"cpu",mirror},{"expected",QJsonArray{wanted,wanted,wanted,1}},{"actual_gpu_format","SFLOAT_32_32_32_32"},{"height_footprint_xywh",channel==4?QJsonArray{63,63,3,3}:QJsonArray{}},{"height_footprint_rgba",footprint}});
   }
  }return result;
 }
 QJsonArray rendering_normal_cpu(const QString &uid){
  QJsonArray result;auto *image=rendering_image(uid,"Normal");
  for(int tile:{1001,1002}){blender::ImageUser user{};user.tile=tile;auto *cpu=blender::BKE_image_acquire_ibuf(image,&user,nullptr);const auto release=qScopeGuard([&]{blender::BKE_image_release_ibuf(image,cpu,nullptr);});check(cpu&&cpu->float_data()&&cpu->channels==4&&cpu->x==128&&cpu->y==128,"Hidden complete Normal CPU mirror is missing/malformed");QJsonArray rgba;for(int c=0;c<4;++c){const auto value=cpu->float_data()[(64*128+64)*4+c];check(std::isfinite(value),"Hidden Normal witness is nonfinite");rgba.append(value);}result.append(QJsonObject{{"tile",tile},{"xy",QJsonArray{64,64}},{"rgba",rgba}});
  }return result;
 }
 QJsonArray rendering_normal_gpu(){
  // Existing read-only demo snapshot exposes actual GPU Normal at1001/64,64.
  // Save/restore the test environment synchronously; no event loop is pumped.
  const bool had=qEnvironmentVariableIsSet("BTS_HEIGHT_NEUTRAL_DEMO");const auto old=qgetenv("BTS_HEIGHT_NEUTRAL_DEMO");const auto restore=qScopeGuard([&]{if(had)qputenv("BTS_HEIGHT_NEUTRAL_DEMO",old);else qunsetenv("BTS_HEIGHT_NEUTRAL_DEMO");});qputenv("BTS_HEIGHT_NEUTRAL_DEMO","1");
  const auto pixel=state()["height_values"].toObject()["normal"].toArray();check(pixel.size()==4,"Existing GPU Normal diagnostic missing");for(const auto &v:pixel)check(v.isDouble()&&std::isfinite(v.toDouble()),"GPU Normal diagnostic is nonfinite/malformed");return pixel;
 }
 void capture_rendering(const char *name,const QJsonObject &s,double wanted){
  // Retain the existing single framebuffer read and unchanged crop before any
  // numeric/ROI assertion. Observations are deliberately never a PASS artifact.
  const QString label=name;const auto prefix=directory_+"/native-rendering-"+label;
  const auto frame=capture_viewport_framebuffer(viewport_);const QRect region(frame.width()/3,frame.height()/3,frame.width()/4,frame.height()/4);const auto roi=frame.copy(region).convertToFormat(QImage::Format_RGB32);
  const bool frame_saved=frame.save(prefix+".png"),roi_saved=roi.save(prefix+"-roi.png");
  QJsonObject observed{{"classification","observed/incomplete"},{"accepted",false},{"stage",label},{"snapshot",s},{"expected_scalar",wanted},{"fixed_bound",3e-5},{"frame_dimensions",QJsonArray{frame.width(),frame.height()}},{"roi_xywh",QJsonArray{region.x(),region.y(),region.width(),region.height()}},{"frame_png_saved",frame_saved},{"roi_png_saved",roi_saved}};
  const auto retain=[&]{QSaveFile file(prefix+"-observed.json");check(file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(observed).toJson())>=0&&file.commit(),"Cannot retain incomplete native Rendering observation");};
  const auto &reference=label=="redone"?rendering_edited_:rendering_before_;
  QJsonObject difference{{"reference_stage",label=="redone"?"edited":"before"},{"available",!reference.isNull()&&!roi.isNull()&&reference.size()==roi.size()&&reference.format()==roi.format()},{"reference_dimensions",QJsonArray{reference.width(),reference.height()}},{"format","RGB32"}};
  if(difference["available"].toBool()){
   qint64 pixels=0;int maximum=0;QJsonArray first,first_actual,first_reference;
   for(int y=0;y<roi.height();++y)for(int x=0;x<roi.width();++x){const auto actual=roi.pixel(x,y),expected=reference.pixel(x,y);const int delta=std::max({std::abs(qRed(actual)-qRed(expected)),std::abs(qGreen(actual)-qGreen(expected)),std::abs(qBlue(actual)-qBlue(expected)),std::abs(qAlpha(actual)-qAlpha(expected))});maximum=std::max(maximum,delta);if(delta){++pixels;if(first.isEmpty()){first={x,y};first_actual={qRed(actual),qGreen(actual),qBlue(actual),qAlpha(actual)};first_reference={qRed(expected),qGreen(expected),qBlue(expected),qAlpha(expected)};}}}
   difference["different_pixels"]=pixels;difference["maximum_component_delta"]=maximum;difference["first_roi_xy"]=first;difference["first_actual_rgba8"]=first_actual;difference["first_reference_rgba8"]=first_reference;difference["exact_equal"]=roi==reference;
  }
  observed["roi_comparison"]=difference;retain();
  check(!frame.isNull(),"Native Rendering private FBO missing");check(!roi.isNull(),"Native Rendering mesh ROI missing");check(frame_saved&&roi_saved,"Cannot retain native Rendering pixels");
  check(s["render_ui"].toBool()&&is_raw(s),"Native scalar capture left Rendering/Raw policy");auto descriptor=native(s);observed["native"]=descriptor;retain();const auto capabilities=descriptor["channels"].toArray();check(capabilities.contains(2)&&capabilities.contains(4)&&!capabilities.contains(3),"Native Rendering exposed artist Normal or lost scalar capabilities");auto parameters=descriptor["parameters"].toArray();check(parameters.size()==1&&parameters[0].toObject()["name"]=="Scalar","Native Rendering source interface changed");auto scalar=parameters[0].toObject();scalar["value"]=QJsonArray{.25};parameters[0]=scalar;descriptor["parameters"]=parameters;check(descriptor==raw_,"Native Rendering edited source/policy metadata beyond the scalar input");const auto pixels=rendering_pixels(s,wanted);observed["samples"]=pixels;retain();check(pixels.size()==4,"Native Rendering scalar GPU/CPU packet is stale");
  const auto uid=rendering_uid(s);const auto handles=s["native_mapping_queues"].toObject()["bindings"].toObject()[uid].toObject()["handles"].toArray();observed["handles"]=handles;retain();check(!handles.isEmpty()&&handles==rendering_handles_,"Native Rendering replaced authoritative image handles");check(s["native_mapping_catalog"]==rendering_catalog_,"Native Rendering mutated immutable source/catalog metadata");
  const auto cpu=rendering_normal_cpu(uid);observed["hidden_normal_cpu_both_tiles"]=cpu;retain();const auto gpu=rendering_normal_gpu();observed["hidden_normal_gpu_tile1001"]=gpu;retain();if(normal_cpu_.isEmpty()){normal_cpu_=cpu;normal_gpu_=gpu;}else check(cpu==normal_cpu_&&gpu==normal_gpu_,"Constant authored H changed hidden complete Normal");
  const auto graph=BTS_cycles_graph_count(),svm=BTS_cycles_svm_count();observed["cycles_graph_count"]=QString::number(graph);observed["cycles_svm_count"]=QString::number(svm);retain();if(!rendering_graph_){rendering_graph_=graph;rendering_svm_=svm;}else check(graph==rendering_graph_&&svm==rendering_svm_,"Native scalar edit/Undo/Redo recompiled Cycles graph/SVM");
  if(label=="before")rendering_before_=roi;else if(label=="edited"){check(roi!=rendering_before_,"Native parameter edit did not change actual shaded mesh ROI");rendering_edited_=roi;}else if(label=="undone")check(roi==rendering_before_,"Native Rendering Undo did not restore exact shaded ROI");else if(label=="redone")check(roi==rendering_edited_,"Native Rendering Redo did not restore exact shaded ROI");
  captures_.append(QJsonObject{{"stage",label},{"render_state",s["render"]},{"render_frames",s["render_frames"]},{"expected_scalar",wanted},{"fixed_bound",3e-5},{"samples",pixels},{"native",native(s)},{"undo_count",s["undo_count"]},{"handles",handles},{"hidden_normal_cpu_both_tiles",cpu},{"hidden_normal_gpu_tile1001",gpu},{"cycles_graph_count",QString::number(graph)},{"cycles_svm_count",QString::number(svm)}});
 }
 void advance_rendering(const QJsonObject &s){
  if(stage_==200){if(pending())return;check(window_->property("nativeHeightError").toString().isEmpty()&&is_raw(s),"Native Rendering Raw activation failed");check(parameter(native(s),"Scalar")==.25&&s["undo_count"].toInt()==history_+1,"Native Rendering activation changed scalar/history");raw_=native(s);history_=s["undo_count"].toInt();rendering_catalog_=s["native_mapping_catalog"];check(rendering_catalog_.isArray()&&!rendering_catalog_.toArray().isEmpty(),"Native Rendering source catalog is absent");const auto uid=rendering_uid(s);rendering_handles_=s["native_mapping_queues"].toObject()["bindings"].toObject()[uid].toObject()["handles"].toArray();control<QToolButton>("propertiesUndock")->click();stage_=201;settle_=0;return;}
  if(stage_==201){QDoubleSpinBox *number=nullptr;for(const auto &entry:native(s)["parameters"].toArray()){const auto p=entry.toObject();if(p["name"]=="Scalar")number=window_->findChild<QDoubleSpinBox *>("nativeValue_"+p["id"].toString()+"_0");}check(number,"Native Rendering scalar control missing");for(auto *parent=number->parentWidget();parent;parent=parent->parentWidget())if(auto *scroll=qobject_cast<QScrollArea *>(parent)){scroll->ensureWidgetVisible(number);break;}if(++settle_<3)return;check(number->isVisible()&&number->isEnabled()&&!number->visibleRegion().isEmpty(),"Native Rendering scalar is not actually visible");stage_=202;settle_=0;action("renderingMode");return;}
  check(s["render_ui"].toBool(),"Native input edit left Rendering mode");const auto render=s["render"].toObject();check(render["active"].toBool()&&!render["image_mirror_paused"].toBool(),"Native Rendering engine/mirror is not active");const auto status=render["status"].toString();check(!status.contains("error",Qt::CaseInsensitive)&&!status.contains("failed",Qt::CaseInsensitive),"Native Rendering reported a failure");const double wanted=stage_==203||stage_==205?.75:.25;
  if(!status.contains("Rendering Done")||rendering_pixels(s,wanted).size()!=4){settle_=0;return;}if(++settle_<3)return;settle_=0;
  if(stage_==202){capture_rendering("before",s,.25);stage_=203;edit_scalar("Scalar",.75);}
  else if(stage_==203){check(parameter(native(s),"Scalar")==.75&&s["undo_count"].toInt()==history_+1,"Native Rendering scalar was not one real gesture");capture_rendering("edited",s,.75);stage_=204;action("undoDocument");}
  else if(stage_==204){check(native(s)==raw_&&s["undo_count"].toInt()==history_,"Native Rendering Undo changed source/instance/history");capture_rendering("undone",s,.25);stage_=205;action("redoDocument");}
  else if(stage_==205){check(parameter(native(s),"Scalar")==.75&&s["undo_count"].toInt()==history_+1,"Native Rendering Redo did not restore the gesture");capture_rendering("redone",s,.75);timer_.stop();dialogs_.stop();QSaveFile file(directory_+"/native-rendering-verified.json");const QJsonObject proof{{"production_main_callbacks",true},{"raw_activated_in_painting",true},{"rendering_active_all_edits",true},{"source_catalog_unchanged",true},{"one_history_gesture",true},{"rendered_roi_undo_exact",true},{"rendered_roi_redo_exact",true},{"native_program_compile_count_measured",false},{"captures",captures_},{"gpu_scalar_tiles",QJsonArray{1001,1002}},{"gpu_scalar_channels",QJsonArray{2,4}},{"scalar_precision","L32F"},{"hidden_normal_gpu_scope","tile1001 at64,64 only"},{"hidden_normal_cpu_scope","center64,64 both128UDIM mirrors; constant covered3x3 H footprint"},{"cpu_interactive_composition",0},{"native_bump_response_equivalence",false},{"performance_claim",false}};check(file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(proof).toJson())>=0&&file.commit(),"Cannot retain native Rendering proof");std::puts("HEIGHT_POLICY_NATIVE_RENDERING PASS");QCoreApplication::quit();return;}
  after_frame_=state()["render_frames"].toInt()+3;
 }
 void capture(const char *name,double expected,bool raw=true,bool exact_restore=false){
  auto s=state();check(!s["composite_pending"].toBool(),"Captured raw Height before publication");const auto tiles=s["image_tiles"].toArray();check(tiles.size()==2,"Raw Height artist must expose both actual UDIM targets");QJsonArray pixels;double maximum=0;
  for(int i=0;i<2;++i){const auto tile=tiles[i].toObject();check(tile["tile"]==1001+i,"Raw Height artist UDIM order changed");const auto samples=tile["samples"].toArray();check(samples.size()==4,"Raw Height artist channel snapshot is incomplete");const auto pixel=samples[3].toArray();
   if(pixel.isEmpty()){check(!raw&&expected==.5,"Raw authored Height GPU target is absent");pixels.append(QJsonArray{});continue;}
   check(pixel.size()==4,"Raw Height artist Bump sample must have four components");for(const auto &value:pixel)check(value.isDouble()&&std::isfinite(value.toDouble()),"Raw Height artist sample is nonfinite/malformed");
   const double alpha=pixel[3].toDouble();check(raw?std::abs(alpha-1)<3e-5:std::abs(alpha)<3e-5,"Raw/Off Height authored coverage is stale");
   for(int c=0;c<3;++c){const double displayed=pixel[c].toDouble()+.5*(1-alpha),error=std::abs(displayed-expected);maximum=std::max(maximum,error);check(error<3e-5,"Artist controls did not evaluate the independent constant scalar Height");}pixels.append(pixel);
  }
  if(exact_restore)check(!raw_pixels_.isEmpty()&&pixels==raw_pixels_,"Artist Undo/Redo/reset did not restore exact GPU Height texels");
  if(raw&&raw_pixels_.isEmpty()&&expected==.2)raw_pixels_=pixels;
  s["stage"]=name;s["independent_expected_height"]=expected;s["maximum_scalar_error"]=maximum;s["fixed_bound"]=3e-5;s["oracle"]=raw?"actual UI Gain=0; H=Bias regardless of surface/owner":"Off has no authored Height; unchanged 0.5 neutral";captures_.append(s);check(capture_viewport_framebuffer(viewport_).save(directory_+"/"+name+".png"),"Cannot capture raw Height artist viewport");
 }
 void quit_pending(const char *phase){
  const auto before_quit=state();check(pending()&&!is_raw(before_quit)&&admission(before_quit)==rejected_,"Pending actual quit already changed document/history/catalog/queue/gesture");
  auto *cancel=control<QPushButton>("cancelNativeHeight");check(cancel->isVisible()&&cancel->isEnabled(),"Actual quit has no real actionable Cancel control");
  timer_.stop();dialogs_.stop();
  const QJsonObject proof{{"phase",phase},{"real_application_quit_requested",true},{"selector_disabled",true},{"real_cancel_visible",true},{"instance_still_none",true},{"admission_unchanged",true},{"gpu_stage_signal_witness",gpu_stage_witness_},{"read_only_current_context",recorded_height_actor_context()},{"worker_instruction_execution_claim",false},{"physical_qt_context_loss_claim",false},{"before_native_close_exclusive_attribution_claim",false},{"performance_claim",false}};
  QSaveFile output(directory_+"/quit-request.json");check(output.open(QIODevice::WriteOnly)&&output.write(QJsonDocument(proof).toJson())>=0&&output.commit(),"Cannot record actual pending quit");
  std::fprintf(stderr,"HEIGHT_POLICY_ACTUAL_QUIT phase=%s\n",phase);QCoreApplication::quit();
 }
 void observed_gpu_stage(const QString &message){
  if((stage_!=8&&stage_!=101)||!message.contains("Preparing isolated native Height GPU")||!gpu_stage_witness_.isEmpty())return;
  // Queued delivery owns the emitted QString. Unlike polling currentMessage,
  // a later frame's status replacement cannot erase this stage observation.
  // poll() stages synchronously without pumping Qt, then returns before its
  // separate publication tick; this slot must still prove it arrived in time.
  const auto now=state();auto *cancel=control<QPushButton>("cancelNativeHeight");
  gpu_stage_witness_={{"message",message},{"queued_delivery",true},{"actor_stage",stage_},{"elapsed_ms",lifetime_.elapsed()},
   {"pending",pending()},{"instance_still_none",!is_raw(now)},{"admission_unchanged",admission(now)==rejected_},
   {"cancel_visible",cancel->isVisible()},{"cancel_enabled",cancel->isEnabled()}};
  check(gpu_stage_witness_["pending"].toBool()&&gpu_stage_witness_["instance_still_none"].toBool()&&gpu_stage_witness_["admission_unchanged"].toBool(),"Queued GPU stage observation arrived after publication or target change");
  check(cancel->isVisible()&&cancel->isEnabled(),"Owner-stage Height Cancel is not actionable");
  if(stage_==101){quit_pending("owner-gpu-staged-before-publication");return;}
  cancel->click();staged_cancel_seen_=true;
 }
 void save_as(){for(auto *item:window_->findChildren<QAction *>()){auto text=item->text();text.remove('&');if(text.startsWith("Save As")){busy_=true;item->trigger();busy_=false;return;}}throw std::runtime_error("Raw Height Save As action missing");}
 void finish(){timer_.stop();dialogs_.stop();const bool cold=qEnvironmentVariableIsSet("BTS_NATIVE_HEIGHT_ARTIST_REOPEN");QJsonObject proof{{"production_main_callbacks",true},{"captures",captures_},{"source_library",library_},{"precision","L32F"},{"cpu_interactive_composition",0},{"fresh_open",cold},{"gpu_stage_signal_witness",gpu_stage_witness_}};if(!cold){proof["actual_selector_cancel_reset_undo_redo"]=true;proof["default_policy_off"]=true;proof["cpu_cancel_exact_old_state"]=true;proof["owner_stage_cancel_exact_old_state"]=staged_cancel_seen_;proof["policy_reset_undoable"]=true;proof["one_history_gesture_per_parameter"]=true;}QSaveFile file(directory_+"/artist-verified.json");check(file.open(QIODevice::WriteOnly)&&file.write(QJsonDocument(proof).toJson())>=0&&file.commit(),"Cannot write raw Height artist evidence");std::puts("HEIGHT_POLICY_ARTIST PASS");QTimer::singleShot(0,qApp,&QCoreApplication::quit);}
 void advance(){if(busy_)return;check(lifetime_.elapsed()<240000,"Raw Height artist timed out");if(stage_!=last_stage_){std::fprintf(stderr,"HEIGHT_POLICY_ARTIST_STAGE %d\n",stage_);last_stage_=stage_;}auto s=state();if(window_->property("blendMaterialImporting").toBool())return;if(stage_>1&&(s["composite_pending"].toBool()||s["property_pending"].toBool()||s["render_frames"].toInt()<after_frame_))return;if(const auto error=window_->property("projectOpenError").toString();!error.isEmpty())throw std::runtime_error(("Raw Height artist Open failed: "+error).toStdString());if(auto *open=window_->findChild<QProgressDialog *>("projectOpenProgress");open&&open->isVisible())return;
  if(rendering_&&stage_>=200){advance_rendering(s);return;}
  switch(stage_){
   case 0:++stage_;action("newProject");return;
   case 1:{auto *dialog=control<QDialog>("newProjectDialog");dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);dialog->findChild<QLineEdit *>("meshPath")->setText(mesh_);auto *size=dialog->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(128));dialog->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);++stage_;busy_=true;control<QPushButton>("createProject")->click();busy_=false;break;}
   case 2:control<QComboBox>("channelPrecision4")->setCurrentIndex(int(ChannelPrecision::L32F));if(rendering_)control<QComboBox>("channelPrecision2")->setCurrentIndex(int(ChannelPrecision::L32F));++stage_;break;
   case 3:++stage_;QTimer::singleShot(0,this,[this]{action("importNativeBlendMaterialAction");});break;
   case 4:{auto *list=window_->findChild<QListWidget *>("blendMaterialList");if(!list||!list->isEnabled())return;QListWidgetItem *chosen=nullptr;for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole)==fixture_name())chosen=list->item(i);check(chosen,"Own authored Height library material not listed");list->setCurrentItem(chosen);++stage_;control<QPushButton>("importBlendMaterial")->click();break;}
   case 5:{auto *list=control<QListWidget>("shelfAssets");QListWidgetItem *chosen=nullptr;for(int i=0;i<list->count();++i)if(list->item(i)->text()==fixture_name())chosen=list->item(i);if(!chosen)return;list->setCurrentItem(chosen);list->itemDoubleClicked(chosen);++stage_;break;}
   case 6:{auto *choice=window_->findChild<QComboBox *>("nativeHeightPolicy");if(!choice||!choice->isVisible())return;check(choice->currentIndex()==0&&!is_raw(s),"Raw Height default was not Off");auto *note=control<QLabel>("nativeHeightPolicyWarning");check(note->text().contains("0.5")&&note->text().contains("Strength")&&note->text().contains("does not reproduce"),"Raw Height artist warning is incomplete");if(rendering_){check(!s["render_ui"].toBool(),"Raw must activate in Painting");history_=s["undo_count"].toInt();stage_=200;request(1);return;}if(!quit_mode_.isEmpty()){
     auto *scroll=window_->findChild<QScrollArea *>("propertiesScroll");check(scroll&&scroll->widget()&&scroll->widget()->isAncestorOf(note),"Raw Height warning is not in actual Properties scroll content");
     if(!properties_revealed_){scroll->ensureWidgetVisible(note,0,48);properties_revealed_=true;settle_=0;return;}
     if(++settle_<3)return;
     check(scroll->viewport()->rect().contains(QRect(note->mapTo(scroll->viewport(),QPoint(0,0)),note->size()))&&!choice->visibleRegion().isEmpty(),"Properties scroll did not reveal the complete warning and selector");
     check(quit_mode_=="cpu"||quit_mode_=="gpu","Unknown actual Height quit test mode");
     if(quit_mode_=="cpu"){check(note->isVisible()&&!note->visibleRegion().isEmpty(),"Raw Height warning is not visible in the private Properties widget");auto *owner=note->window();check(owner&&owner->rect().contains(QRect(note->mapTo(owner,QPoint(0,0)),note->size())),"Raw Height warning is outside its private Properties owner");check(owner->grab().save(directory_+"/height-properties-before-pending.png"),"Cannot capture private Properties controls before quit");}
     const auto ready=state();initial_=native(ready);history_=ready["undo_count"].toInt();rejected_=admission(ready);
     stage_=quit_mode_=="cpu"?100:101;request(1);
     if(quit_mode_=="cpu")quit_pending("worker-result-not-delivered");
     return;
    }
    initial_=native(s);history_=s["undo_count"].toInt();rejected_=admission(s);stage_=7;request(1);check(control<QPushButton>("cancelNativeHeight")->isVisible(),"Real Height Cancel button did not appear");control<QPushButton>("cancelNativeHeight")->click();return;}
   case 7:if(pending())return;if(++settle_<5)return;unchanged();settle_=0;stage_=8;request(1);return;
   case 8:if(!staged_cancel_seen_||pending())return;if(++settle_<5)return;unchanged();settle_=0;stage_=9;request(1);return;
   case 9:if(pending())return;check(window_->property("nativeHeightError").toString().isEmpty(),"Actual raw Height activation was rejected");check(is_raw(s)&&s["undo_count"].toInt()==history_+1,"Raw Height activation was not one atomic history entry");stage_=90;edit_scalar("Gain",0);break;
   case 90:check(parameter(native(s),"Gain")==0&&s["undo_count"].toInt()==history_+2,"Artist Gain=0 did not commit exactly one gesture");stage_=91;edit_scalar("Bias",.2);break;
   case 91:check(parameter(native(s),"Gain")==0&&std::abs(parameter(native(s),"Bias")-.2)<1e-7&&s["undo_count"].toInt()==history_+3,"Artist Bias=.2 did not commit exactly one gesture");raw_=native(s);off_=initial_;off_["parameters"]=raw_["parameters"];capture("raw-edited-constant",.2);stage_=10;action("undoDocument");break;
   case 10:check(is_raw(s)&&parameter(native(s),"Gain")==0&&parameter(native(s),"Bias")==.5&&s["undo_count"].toInt()==history_+2,"Raw scalar Undo did not restore Bias=.5 in one gesture");capture("raw-bias-undone",.5);stage_=11;action("redoDocument");break;
   case 11:check(native(s)==raw_&&is_raw(s)&&s["undo_count"].toInt()==history_+3,"Raw scalar Redo did not restore exact instance policy/parameters");capture("raw-redone",.2,true,true);stage_=12;request(0);return;
   case 12:if(pending())return;check(native(s)==off_&&!is_raw(s)&&s["undo_count"].toInt()==history_+4,"Actual Height Off/reset did not restore source capabilities and retain parameters");capture("raw-off-neutral",.5,false);stage_=13;action("undoDocument");break;
   case 13:{check(native(s)==raw_&&is_raw(s)&&s["undo_count"].toInt()==history_+3,"Undo of Height Off/reset did not restore raw policy");capture("raw-reset-undone",.2,true,true);QSaveFile descriptor(directory_+"/artist-descriptor.json");check(descriptor.open(QIODevice::WriteOnly)&&descriptor.write(QJsonDocument(raw_).toJson())>=0&&descriptor.commit(),"Cannot save artist raw descriptor oracle");stage_=14;save_as();break;}
   case 14:{action("guiAeState");const auto io=window_->property("aeState").value<QJsonObject>();check(io["save_error"].toString().isEmpty(),"Raw Height artist Save failed");if(io["save_pending"].toBool())return;check(QFileInfo(project_).size()>0,"Raw Height artist packed Save did not finish");finish();return;}
   case 20:stage_=21;busy_=true;action("openProject");busy_=false;return;
   case 21:{auto *choice=window_->findChild<QComboBox *>("nativeHeightPolicy");if(!choice)return;check(is_raw(s)&&choice->currentIndex()==1,"Fresh artist Open did not restore selected raw policy");QFile oracle(qEnvironmentVariable("BTS_NATIVE_HEIGHT_ARTIST_DESCRIPTOR"));check(oracle.open(QIODevice::ReadOnly)&&native(s)==QJsonDocument::fromJson(oracle.readAll()).object(),"Fresh artist Open changed exact raw/source descriptor");check(parameter(native(s),"Gain")==0&&std::abs(parameter(native(s),"Bias")-.2)<1e-7,"Fresh actor expected a different authored constant oracle");capture("raw-fresh-packed-open",.2);finish();return;}
  }
  after_frame_=state()["render_frames"].toInt()+3;
 }
 void choose_files(){for(auto *widget:QApplication::topLevelWidgets()){
  if(auto *dialog=dynamic_cast<QFileDialog *>(widget);dialog&&dialog->isVisible()){
   const bool project=dialog->windowTitle().contains("Save project")||dialog->windowTitle().contains("Open project");if(project||dialog->objectName()=="blendMaterialFileDialog"){const auto path=project?project_:library_;check(!path.isEmpty(),"Raw Height file chooser has no own fixture target");const auto key=dialog->windowTitle()+path;if(dialog->property("heightTestChoice").toString()!=key){dialog->setDirectory(QFileInfo(path).absolutePath());dialog->setProperty("heightTestChoice",key);}auto *name=dialog->findChild<QLineEdit *>("fileNameEdit");check(name,"Raw Height chooser filename editor missing");name->setText(path);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);}}
  if(auto *box=qobject_cast<QMessageBox *>(widget);box&&box->isVisible()){if(box->icon()==QMessageBox::Critical)throw std::runtime_error(box->text().toStdString());if(auto *discard=box->button(QMessageBox::Discard))discard->click();}
 }}
 void failed(const std::exception &error){timer_.stop();dialogs_.stop();const QJsonObject failure{{"actor_error",QString::fromUtf8(error.what())},{"project_open_error",window_->property("projectOpenError").toString()},{"stage",stage_},{"gpu_stage_signal_witness",gpu_stage_witness_},{"read_only_current_context",recorded_height_actor_context()}};QSaveFile output(directory_+"/artist-failure.json");if(output.open(QIODevice::WriteOnly)){output.write(QJsonDocument(failure).toJson());output.commit();}std::fprintf(stderr,"HEIGHT_POLICY_ARTIST_FAILED %s\nHEIGHT_POLICY_ARTIST_FAILURE_DETAIL %s\n",error.what(),QJsonDocument(failure).toJson(QJsonDocument::Compact).constData());QCoreApplication::exit(1);}
 public:
 NativeHeightArtist(QWidget *window,QWidget *viewport,QString directory,QString mesh,std::function<QJsonObject()> snapshot):QObject(window),window_(window),viewport_(viewport),directory_(std::move(directory)),mesh_(std::move(mesh)),library_(qEnvironmentVariable("BTS_BLEND_MATERIAL_SOURCE")),project_(qEnvironmentVariable("BTS_NATIVE_HEIGHT_ARTIST_PROJECT")),snapshot_(std::move(snapshot)){
  if(qEnvironmentVariableIsSet("BTS_NATIVE_HEIGHT_ARTIST_REOPEN"))stage_=20;
  auto *status=window_->findChild<QStatusBar *>();check(status,"Actual Height status signal source missing");
  connect(status,&QStatusBar::messageChanged,this,[this](QString message){try{observed_gpu_stage(message);}catch(const std::exception &error){failed(error);}},Qt::QueuedConnection);
  lifetime_.start();connect(&timer_,&QTimer::timeout,this,[this]{try{advance();}catch(const std::exception &error){failed(error);}});timer_.start(30);
  connect(&dialogs_,&QTimer::timeout,this,[this]{try{choose_files();}catch(const std::exception &error){failed(error);}});dialogs_.start(20);
 }
};
}
void start_gui_fix_demo(QWidget *workspace,QWidget *viewport,const QString &directory,const QString &mesh,int,std::function<QJsonObject()> state){new NativeHeightArtist(workspace,viewport,directory,mesh,std::move(state));}
}
