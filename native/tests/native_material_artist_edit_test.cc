// Exercise the production artist callbacks, frame queue and history unchanged.
// A test-only call bridge links either exact baseline or exact fixed diff code;
// the production artist callbacks, queues and history remain identical.
#define layer_edit_channels native_artist_unused_inline_diff
#include "LayerEditDiff.hh"
#undef layer_edit_channels
namespace bts { CompositeChannels native_artist_edit_diff(const TextureSet &,const TextureSet &,const QJsonObject &,const QJsonObject &); }
#define layer_edit_channels native_artist_edit_diff
#define main bts_native_artist_main
#define start_gui_fix_demo start_native_artist_edit_demo
#ifndef BTS_NATIVE_EDIT_MAIN
#  define BTS_NATIVE_EDIT_MAIN "../host/main.cc"
#endif
#include BTS_NATIVE_EDIT_MAIN
#undef start_gui_fix_demo
#undef main
#undef layer_edit_channels
#include "GPU_pass.hh"
#include "ViewportDemoCapture.hh"
#include <QKeyEvent>
namespace bts {namespace {
class NativeArtistEdit final:public QObject {
 QWidget *window,*viewport;QString output,mesh,library;std::function<QJsonObject()> snapshot;
 QTimer timer,dialogs;int stage=0,ticks=0,after_frame=0,history=0;bool busy=false;QJsonArray captures;
 std::uint64_t compilations=0,recipes=0;QJsonObject source_descriptor;
 template<class T>T *control(const char *name){auto *widget=window->findChild<T *>(name);if(!widget)throw std::runtime_error(std::string("Missing artist control: ")+name);return widget;}
 static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
 void action(const char *name){control<QAction>(name)->trigger();}
 QJsonObject selected_native(const QJsonObject &state){for(const auto &set:state["document"].toObject()["texture_sets"].toArray())for(const auto &layer:set.toObject()["layers"].toArray()){const auto description=layer.toObject()["fill_projection"].toObject()["native_material"];if(description.isObject())return description.toObject();}throw std::runtime_error("Artist document has no native fill");}
 double roughness(const QJsonObject &description){for(const auto &value:description["parameters"].toArray()){const auto parameter=value.toObject();if(parameter["id"]=="Roughness")return parameter["value"].toArray().first().toDouble();}throw std::runtime_error("Native Roughness input missing");}
 void capture(const char *name,double expected){
  auto state=snapshot();const auto description=selected_native(state);require(std::abs(roughness(description)-expected)<1e-6,"Artist parameter value was not committed");
  const auto samples=state["material_gpu_samples"].toArray();require(samples.size()==3&&samples[2].toArray().size()==4,"Artist GPU sample unavailable");const auto sample=samples[2].toArray();
  const double error=std::abs(sample[0].toDouble()-expected);if(error>2e-6)throw std::runtime_error(QString("Native artist edit GPU stale: stage=%1 descriptor=%2 GPU=%3 error=%4").arg(name).arg(expected,0,'g',9).arg(sample[0].toDouble(),0,'g',9).arg(error,0,'g',9).toStdString());
  require(sample[3].toDouble()>.999,"Native artist fill lost coverage");state["stage"]=name;state["native_descriptor"]=description;state["gpu_error"]=error;captures.append(state);capture_viewport_framebuffer(viewport).save(output+"/"+name+".png");
 }
 void advance(){
  if(busy)return;if(++ticks>1800)throw std::runtime_error("Native artist edit timed out");const auto state=snapshot();
  if(stage>1&&(state["composite_pending"].toBool()||state["property_pending"].toBool()||state["render_frames"].toInt()<after_frame))return;
  if(window->property("blendMaterialImporting").toBool())return;
  switch(stage){
   case 0:++stage;action("newProject");return;
   case 1:{auto *dialog=control<QDialog>("newProjectDialog");dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=dialog->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(128));++stage;busy=true;control<QPushButton>("createProject")->click();busy=false;break;}
   case 2:control<QComboBox>("channelPrecision2")->setCurrentIndex(int(ChannelPrecision::L32F));++stage;break;
   case 3:++stage;QTimer::singleShot(0,this,[this]{action("importNativeBlendMaterialAction");});break;
   case 4:{auto *list=window->findChild<QListWidget *>("blendMaterialList");if(!list||!list->isEnabled())return;QListWidgetItem *chosen=nullptr;for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole)=="Copper constant")chosen=list->item(i);require(chosen,"Native fixture material not listed");list->setCurrentItem(chosen);++stage;control<QPushButton>("importBlendMaterial")->click();break;}
   case 5:{auto *list=control<QListWidget>("shelfAssets");QListWidgetItem *chosen=nullptr;for(int i=0;i<list->count();++i)if(list->item(i)->text()=="Copper constant")chosen=list->item(i);if(!chosen)return;list->setCurrentItem(chosen);list->itemDoubleClicked(chosen);++stage;break;}
   case 6:{auto *number=window->findChild<QDoubleSpinBox *>("nativeValue_Roughness_0");if(!number)return;capture("before",.23);source_descriptor=selected_native(snapshot());history=snapshot()["undo_count"].toInt();compilations=blender::GPU_pass_global_compilation_count();recipes=PaintPixels::recipe_materializations();
    number->setFocus();auto *edit=number->findChild<QLineEdit *>();require(edit,"Native numeric editor missing");QKeyEvent select(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier,"a");QApplication::sendEvent(edit,&select);QKeyEvent text(QEvent::KeyPress,Qt::Key_0,Qt::NoModifier,"0.73");QApplication::sendEvent(edit,&text);QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(edit,&enter);++stage;break;}
   case 7:capture("edited",.73);require(snapshot()["undo_count"].toInt()==history+1,"Native numeric edit was not one history gesture");++stage;action("undoDocument");break;
   case 8:capture("undone",.23);require(snapshot()["undo_count"].toInt()==history,"Native Undo history count changed");require(selected_native(snapshot())==source_descriptor,"Native Undo did not restore exact descriptor");++stage;action("redoDocument");break;
   case 9:{capture("redone",.73);require(snapshot()["undo_count"].toInt()==history+1,"Native Redo history count changed");require(blender::GPU_pass_global_compilation_count()==compilations,"Artist parameter edit/Undo/Redo recompiled native shaders");require(PaintPixels::recipe_materializations()==recipes,"Artist native edit materialized CPU paint recipes");timer.stop();dialogs.stop();QFile file(output+"/native-artist-edit.json");require(file.open(QIODevice::WriteOnly),"Cannot write native artist proof");file.write(QJsonDocument(QJsonObject{{"production_main_callbacks",true},{"input","Native numeric editor Qt key events; actual Undo/Redo QActions"},{"captures",captures},{"one_gesture",true},{"zero_shader_recompiles",true},{"cpu_recipe_materializations",0},{"precision","L32F"},{"native_gpu_oracle",true}}).toJson());file.close();std::puts("BTS_NATIVE_ARTIST_EDIT PASS");QTimer::singleShot(0,qApp,&QCoreApplication::quit);return;}
  }
  after_frame=snapshot()["render_frames"].toInt()+8;
 }
 public:
 NativeArtistEdit(QWidget *w,QWidget *v,QString directory,QString source,std::function<QJsonObject()> state):QObject(w),window(w),viewport(v),output(std::move(directory)),mesh(std::move(source)),library(qEnvironmentVariable("BTS_BLEND_MATERIAL_SOURCE")),snapshot(std::move(state)){
  connect(&timer,&QTimer::timeout,this,[this]{try{advance();}catch(const std::exception &error){timer.stop();dialogs.stop();std::fprintf(stderr,"BTS_NATIVE_ARTIST_EDIT_FAILED %s\n",error.what());QCoreApplication::exit(1);}});timer.start(100);
  connect(&dialogs,&QTimer::timeout,this,[this]{for(auto *widget:QApplication::topLevelWidgets()){
   if(auto *file=qobject_cast<QFileDialog *>(widget);file&&file->isVisible()&&file->objectName()=="blendMaterialFileDialog"){file->selectFile(library);QMetaObject::invokeMethod(file,"accept",Qt::QueuedConnection);}
   if(auto *box=qobject_cast<QMessageBox *>(widget);box&&box->isVisible()){if(box->icon()==QMessageBox::Critical){std::fprintf(stderr,"BTS_NATIVE_ARTIST_EDIT_DIALOG_FAILED %s\n",qPrintable(box->text()));QCoreApplication::exit(1);}else if(auto *discard=box->button(QMessageBox::Discard))discard->click();}
  }});dialogs.start(30);
 }
};
}
void start_native_artist_edit_demo(QWidget *workspace,QWidget *viewport,const QString &directory,const QString &mesh,int,std::function<QJsonObject()> state){new NativeArtistEdit(workspace,viewport,directory,mesh,std::move(state));}
}
int main(int argc,char **argv){return bts_native_artist_main(argc,argv);}
