#include "ViewportDemoCapture.hh"
#include "SubsurfaceDemo.hh"
#include "ColorPicker.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
class Demo final:public QObject {
 QWidget *w,*v;QString directory,mesh;int size,phase=0,ticks=0,after_frame=0,settled=0;QTimer timer;QJsonArray captures;std::function<QJsonObject()> state;
 template<class T>T *get(const char *name){auto *value=w->findChild<T *>(name);if(!value)qFatal("Subsurface control missing: %s",name);return value;}
 void action(const char *name){get<QAction>(name)->trigger();}
 void capture(const char *name){auto data=state();data["stage"]=name;data["samples_editable"]=get<QSpinBox>("subsurfacePreviewSamples")->isEnabled();data["shader_fields_editable"]=get<QDoubleSpinBox>("shaderSubsurfaceScale")->isEnabled();captures.append(data);QFile file(directory+"/subsurface.json");if(!file.open(QIODevice::WriteOnly))qFatal("Subsurface proof unavailable");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());file.close();capture_viewport_framebuffer(v).save(directory+"/"+name+"-viewport.png");if(QString(name)=="scattering-one"){get<QScrollArea>("displaySettingsScroll")->ensureWidgetVisible(get<QWidget>("subsurfacePreviewEnabled"));capture_demo_window(w,v).save(directory+"/subsurface-workspace.png");get<QDialog>("shaderSettings")->grab().save(directory+"/shader-panel.png");}}
 void advance(){if(++ticks>1000)qFatal("Subsurface demo timed out");const auto s=state();if(phase>1&&(s["composite_pending"].toBool()||s["property_pending"].toBool()||s["render_frames"].toInt()<after_frame))return;if(s["render_ui"].toBool()){const auto text=s["render"].toObject()["status"].toString();if(text.contains("failed",Qt::CaseInsensitive)||text.contains("error",Qt::CaseInsensitive))qFatal("Subsurface Cycles failed: %s",qPrintable(text));if(!text.contains("Rendering Done")){settled=0;return;}if(++settled<4)return;}settled=0;
 switch(phase++){
 case 0:action("newProject");break;
 case 1:{auto *dialog=get<QDialog>("newProjectDialog");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *resolution=dialog->findChild<QComboBox *>("documentResolution");resolution->setCurrentIndex(resolution->findData(size));auto *create=dialog->findChild<QPushButton *>("createProject");if(!create->isEnabled())qFatal("Subsurface fixture invalid");create->click();break;}
 case 2:{action("guiSkewPrepare");auto *list=get<QListWidget>("textureSets");for(int i=0;i<list->count();++i)if(list->item(i)->text().contains("Sphere")){list->setCurrentRow(i);break;}get<QToolButton>("addFillLayer")->click();get<QDoubleSpinBox>("metallicControl")->setValue(0);get<QDoubleSpinBox>("roughnessControl")->setValue(.5);action("addScatteringChannel");get<QToolButton>("displaySettingsButton")->click();get<QToolButton>("shaderSettingsButton")->click();break;}
 case 3:capture("initial");get<QCheckBox>("subsurfacePreviewEnabled")->setChecked(true);break;
 case 4:capture("enabled-zero");get<QDoubleSpinBox>("userChannelValue0")->setValue(1);break;
 case 5:capture("scattering-one");get<QCheckBox>("subsurfacePreviewEnabled")->setChecked(false);break;
 case 6:capture("global-off");get<QToolButton>("undoToolbar")->click();break;
 case 7:capture("global-undo");get<QToolButton>("redoToolbar")->click();break;
 case 8:capture("global-redo");get<QCheckBox>("subsurfacePreviewEnabled")->setChecked(true);get<QCheckBox>("shaderSubsurfaceEnabled")->setChecked(false);break;
 case 9:capture("shader-off");get<QCheckBox>("shaderSubsurfaceEnabled")->setChecked(true);get<QDoubleSpinBox>("shaderSubsurfaceScale")->setValue(0);break;
 case 10:capture("scale-zero");get<QDoubleSpinBox>("shaderSubsurfaceScale")->setValue(.5);get<QComboBox>("shaderSubsurfaceType")->setCurrentIndex(1);break;
 case 11:capture("translucent");get<QComboBox>("shaderSubsurfaceType")->setCurrentIndex(0);static_cast<ColorPicker *>(get<QWidget>("shaderSubsurfaceColor"))->accept_sample({1,.1f,.04f,1});break;
 case 12:capture("colored");get<QPushButton>("shaderUndo")->click();break;
 case 13:capture("shader-undo");get<QPushButton>("shaderRedo")->click();break;
 case 14:capture("shader-redo");get<QSpinBox>("subsurfacePreviewSamples")->setValue(32);break;
 case 15:capture("samples32");action("guiSkewSave");break;
 case 16:capture("saved");action("guiSkewOpen");break;
 case 17:capture("opened");action("renderingMode");break;
 case 18:capture("cycles");get<QCheckBox>("subsurfacePreviewEnabled")->setChecked(false);break;
 case 19:capture("cycles-off");action("paintingMode");break;
 case 20:capture("painting-after-render");timer.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
 }after_frame=state()["render_frames"].toInt()+20;
 }
 public:Demo(QWidget *window,QWidget *viewport,QString dir,QString fixture,int resolution,std::function<QJsonObject()> snapshot):QObject(window),w(window),v(viewport),directory(dir),mesh(fixture),size(resolution),state(std::move(snapshot)){QDir().mkpath(dir);timer.setInterval(250);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_subsurface_demo(QWidget *w,QWidget *v,const QString &dir,const QString &mesh,int size,std::function<QJsonObject()> state){new Demo(w,v,dir,mesh,size,std::move(state));}
}
