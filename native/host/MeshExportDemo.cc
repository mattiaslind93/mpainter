#include "ViewportDemoCapture.hh"
#include "MeshExportDemo.hh"
#include "GuiFileSelection.hh"
#include <QImage>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QToolButton>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include "ViewportWidget.hh"
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
namespace bts {namespace {
class Demo final:public QObject {
 QWidget *w,*v;QString dir,mesh;int size,phase=0,ticks=0,after_frame=0;bool prepared=false;QTimer timer;QJsonArray captures,results;std::function<QJsonObject()> state;
 template<class T>T *get(const char *name){auto *p=w->findChild<T *>(name);if(!p)qFatal("Mesh export control missing: %s",name);return p;}
 void action(const char *name){get<QAction>(name)->trigger();}
 void capture(const char *name){auto data=state();data["stage"]=name;captures.append(data);QFile file(dir+"/mesh-export.json");if(!file.open(QIODevice::WriteOnly))qFatal("Mesh export proof unavailable");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures},{"exports",results}}).toJson());file.close();capture_viewport_framebuffer(v).save(dir+"/"+name+"-viewport.png");}
 void export_mesh(bool triangles,QString name={},bool want_displaced=false,bool recompute=true,bool available=false){if(name.isEmpty())name=triangles?"triangles":"polygons";timer.stop();auto *poll=new QTimer(this);poll->setInterval(50);bool *started=new bool(false),*picked=new bool(false);connect(poll,&QTimer::timeout,this,[this,poll,started,picked,triangles,name,want_displaced,recompute,available]{auto *dialog=w->findChild<QDialog *>("meshExportDialog");if(!dialog)return;
  if(!*started){*started=true;auto *base=dialog->findChild<QRadioButton *>("meshExportBase"),*displaced=dialog->findChild<QRadioButton *>("meshExportDisplaced");if(!base->isChecked()||displaced->isEnabled()!=available)qFatal("Base export defaults/gating wrong");dialog->findChild<QCheckBox *>("meshExportTriangulate")->setChecked(triangles);displaced->setChecked(want_displaced);auto *normal=dialog->findChild<QCheckBox *>("meshExportRecomputeNormals");if(normal->isEnabled()!=want_displaced)qFatal("Recompute normals gate mismatch");normal->setChecked(recompute);dialog->grab().save(dir+"/options.png");auto *run=dialog->findChild<QPushButton *>("runMeshExport");QTimer::singleShot(0,run,&QPushButton::click);return;}
  if(auto *picker=dialog->findChild<QFileDialog *>("meshExportPicker");picker&&picker->isVisible()&&!*picked){*picked=true;const QString output=name+(qEnvironmentVariableIsSet("BTS_MESH_EXPORT_USD")?(triangles?".usdc":".usdz"):".obj");picker->setDirectory(dir);if(qEnvironmentVariableIsSet("BTS_MESH_EXPORT_USD"))picker->selectNameFilter(triangles?"USD binary (*.usdc)":"USD package (*.usdz)");picker->selectFile(output);if(auto *field=picker->findChild<QLineEdit *>("fileNameEdit"))field->setText(output);qInfo("BTS_MESH_PICK requested=%s actual=%s",qPrintable(output),qPrintable(picker->selectedFiles().join(";")));QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);return;}
  if(!dialog->property("meshExportError").toString().isEmpty())qFatal("Mesh export failed: %s",qPrintable(dialog->property("meshExportError").toString()));if(!dialog->property("meshExportFinished").toBool())return;
  results.append(QJsonObject{{"name",name},{"displaced",want_displaced},{"recompute",recompute},{"triangulate",triangles},{"path",dialog->property("meshExportPath").toString()},{"source",dialog->property("meshExportSource").toJsonObject()},{"status",dialog->findChild<QLabel *>("meshExportStatus")->text()}});dialog->grab().save(dir+"/"+name+"-result.png");poll->stop();dialog->reject();poll->deleteLater();delete started;delete picked;timer.start(250);
 });poll->start();action("exportMesh");}
 void advance(){if(++ticks>1000)qFatal("Mesh export demo timed out");const auto s=state();if(phase>1&&(s["composite_pending"].toBool()||s["property_pending"].toBool()||s["render_frames"].toInt()<after_frame))return;if(qEnvironmentVariableIsSet("BTS_MESH_EXPORT_DISPLACED")){advance_displaced();after_frame=state()["render_frames"].toInt()+20;return;}switch(phase++){
 case 0:action("newProject");break;
 case 1:{auto *dialog=get<QDialog>("newProjectDialog");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *resolution=dialog->findChild<QComboBox *>("documentResolution");resolution->setCurrentIndex(resolution->findData(size));if(qEnvironmentVariableIsSet("BTS_MESH_EXPORT_AUTO_UV"))dialog->findChild<QCheckBox *>("newAutoUnwrap")->setChecked(true);dialog->findChild<QPushButton *>("createProject")->click();break;}
 case 2:if(!prepared){prepared=true;action("guiSkewPrepare");--phase;break;}capture("before");export_mesh(true);break;
 case 3:capture("after-triangles");export_mesh(false);break;
 case 4:capture("after-polygons");QTimer::singleShot(100,this,[this]{get<QDialog>("meshExportDialog")->reject();});action("exportMesh");break;
 case 5:capture("after-cancel");timer.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
 }after_frame=state()["render_frames"].toInt()+20;}
 void number(const char *name,double value){get<QDoubleSpinBox>(name)->setValue(value);}
 void advance_displaced(){switch(phase++){
 case 0:action("newProject");break;
 case 1:{auto *dialog=get<QDialog>("newProjectDialog");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *resolution=dialog->findChild<QComboBox *>("documentResolution");resolution->setCurrentIndex(resolution->findData(size));dialog->findChild<QPushButton *>("createProject")->click();break;}
 case 2:action("guiSkewPrepare");capture("base-before");export_mesh(false,"base-before");break;
 case 3:capture("base-after");get<QToolButton>("addFillLayer")->click();get<QCheckBox>("displacementChannel")->setChecked(true);get<QToolButton>("brushDisplacement")->setChecked(true);get<QSpinBox>("displacementSubdivisionCount")->setValue(0);number("displacementStrength",.25);number("displacementMidlevel",.5);number("materialDisplacement",.8);break;
 case 4:capture("level0-before");export_mesh(true,"level0",true,true,true);break;
 case 5:capture("level0-after");export_mesh(true,"normals-retained",true,false,true);break;
 case 6:capture("normals-retained-after");number("displacementMidlevel",1);break;
 case 7:capture("negative-before");export_mesh(true,"negative",true,true,true);break;
 case 8:capture("negative-after");number("displacementMidlevel",.5);get<QSpinBox>("displacementSubdivisionCount")->setValue(2);break;
 case 9:capture("uniform-before");export_mesh(true,"uniform",true,true,true);break;
 case 10:capture("uniform-after");{auto *mode=get<QComboBox>("displacementSubdivisionMode");mode->setCurrentIndex(mode->findData(2));}break;
 case 11:capture("catmull-before");export_mesh(true,"catmull",true,true,true);break;
 case 12:{capture("catmull-after");get<QSpinBox>("displacementSubdivisionCount")->setValue(0);auto *mode=get<QComboBox>("displacementSubdivisionMode");mode->setCurrentIndex(mode->findData(0));QImage ramp(256,256,QImage::Format_RGB32);for(int y=0;y<256;++y)for(int x=0;x<256;++x)ramp.setPixelColor(x,y,QColor(x,x,x));const auto path=dir+"/displacement-ramp.png";if(!ramp.save(path))qFatal("Displacement ramp fixture unavailable");timer.stop();select_gui_test_file(w,"fillTextureFileDialog",path);get<QComboBox>("fillChannelResource14")->setCurrentIndex(1);timer.start(250);break;}
 case 13:capture("texture-before");export_mesh(true,"texture-recomputed",true,true,true);break;
 case 14:capture("texture-after");export_mesh(true,"texture-retained",true,false,true);break;
 case 15:capture("texture-retained-after");get<QCheckBox>("displacementEnabled")->setChecked(false);break;
 case 16:capture("disabled-before");export_mesh(false,"base-disabled",false,true,false);break;
 case 17:capture("disabled-after");timer.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
 }}
 public:Demo(QWidget *window,QWidget *viewport,QString directory,QString fixture,int resolution,std::function<QJsonObject()> snapshot):QObject(window),w(window),v(viewport),dir(directory),mesh(fixture),size(resolution),state(std::move(snapshot)){QDir().mkpath(dir);timer.setInterval(250);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_mesh_export_demo(QWidget *w,QWidget *v,const QString &dir,const QString &mesh,int size,std::function<QJsonObject()> state){new Demo(w,v,dir,mesh,size,std::move(state));}
}
