#include "PythonConsole.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMainWindow>
#include <QMouseEvent>
#include "ViewportWidget.hh"
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
QString quote(const QString &value){const auto bytes=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);return QString::fromUtf8(bytes.mid(1,bytes.size()-2));}
class Demo final:public QObject {
 QMainWindow *window;QString directory;std::function<bool()> busy;std::function<QJsonObject()> state;QTimer timer;int phase=0,ticks=0;QJsonArray captures;
 template<class T>T *get(const char *name){auto *value=window->findChild<T *>(name);if(!value)qFatal("Warp geometry control missing: %s",name);return value;}
 void command(const QString &code){get<QLineEdit>("pythonCommand")->setText("exec("+quote(code)+")");get<QPushButton>("executePythonCommand")->click();if(!get<QDockWidget>("pythonConsoleDock")->property("lastExecutionSucceeded").toBool())qFatal("Warp geometry script failed: %s",qPrintable(get<QPlainTextEdit>("pythonOutput")->toPlainText()));}
 void capture(const char *name){if(qEnvironmentVariableIsSet("BTS_GPU_DENSE_ORACLE"))get<QAction>("checkGuiGpuStack")->trigger();auto data=state();data["stage"]=name;data["toggle_visible"]=get<QToolButton>("warpToGeometry")->isVisible();data["toggle_checked"]=get<QToolButton>("warpToGeometry")->isChecked();captures.append(data);QFile file(directory+"/warp-geometry.json");if(!file.open(QIODevice::WriteOnly))qFatal("Cannot write warp geometry proof");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());window->setProperty("guiCapturePath",directory+"/"+name+"-viewport.png");get<QAction>("guiCaptureViewport")->trigger();}
 void move(double value){get<QDoubleSpinBox>("fill3d0_0")->setValue(value);}
 void view_edit(){auto *viewport=window->findChild<bts::ViewportWidget *>();get<QComboBox>("warpSelectedPoint")->setCurrentIndex(0);const auto points=state()["warp_geometry"].toObject()["screen"].toArray();if(points.size()!=9)qFatal("Warp geometry expected 3x3 grid");const auto center=points[4].toArray();const QPointF point(center[0].toDouble(),center[1].toDouble());const auto send=[&](QEvent::Type type,QPointF at){QMouseEvent event(type,at,viewport->mapToGlobal(at.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(viewport,&event);};send(QEvent::MouseButtonPress,point);send(QEvent::MouseMove,point+QPointF(14,-5));send(QEvent::MouseButtonRelease,point+QPointF(14,-5));}
 void advance(){if(++ticks>1400)qFatal("Warp geometry demo timed out");auto *view=window->findChild<bts::ViewportWidget *>();if(!view||!view->isValid()||(phase!=1&&busy()))return;
  switch(phase++){
  case 0:get<QAction>("showPythonConsole")->trigger();command("import bts,pathlib\nroot=pathlib.Path("+quote(directory)+")\nC=bts.textureset.ChannelType");get<QAction>("newProject")->trigger();break;
  case 1:{auto *dialog=get<QDialog>("newProjectDialog");auto *preset=dialog->findChild<QComboBox *>("projectTemplate");preset->setCurrentIndex(preset->findData(":pbr"));dialog->findChild<QLineEdit *>("meshPath")->setText(directory+"/curved.obj");auto *size=dialog->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(128));dialog->findChild<QPushButton *>("createProject")->click();break;}
  case 2:get<QToolButton>("addFillLayer")->click();break;
  case 3:command("stack=bts.textureset.get_active_stack()\nfill=bts.layerstack.get_root_layer_nodes(stack)[0]\nasset=bts.resource.import_image(root/'decal.png',scope='project',usage='texture')\nfill.active_channels={C.BaseColor}\nfill.set_source(C.BaseColor,asset['id'])\np=fill.get_projection_parameters();p.mode=5;p.scale3=[.4,.4,.4];p.linked3=False;p.filtering=2;p.wrap=1\nfill.set_projection_parameters(p)");get<QComboBox>("viewLayoutControl")->setCurrentIndex(2);break;
  case 4:capture("flat");get<QToolButton>("warpToGeometry")->setChecked(true);break;
  case 5:capture("fitted");move(.16);break;
  case 6:capture("moved");{auto *points=get<QComboBox>("warpSelectedPoint");const int index=points->findData(4);if(index<0)qFatal("Warp grid point missing");points->setCurrentIndex(index);get<QDoubleSpinBox>("warpVector0_0")->setValue(.12);get<QComboBox>("warpEdition")->setCurrentIndex(1);}break;
  case 7:capture("deformed");move(-.12);break;
  case 8:capture("moved-deformed");view_edit();break;
  case 9:capture("view-edited");move(.08);break;
  case 10:capture("retained");get<QToolButton>("warpToGeometry")->setChecked(false);break;
  case 11:capture("off");move(.2);break;
  case 12:capture("off-moved");get<QToolButton>("warpToGeometry")->setChecked(true);break;
  case 13:capture("resumed");get<QAction>("undoDocument")->trigger();break;
  case 14:capture("undo");get<QAction>("redoDocument")->trigger();break;
  case 15:capture("redo");command("bts.project.save_as(root/'on.blend')");break;
  case 16:capture("saved");if(!QFile::remove(directory+"/decal.png"))qFatal("Cannot remove own test decal");command("bts.project.close()\nbts.project.open(root/'on.blend')");break;
  case 17:capture("opened");get<QToolButton>("warpToGeometry")->setChecked(false);break;
  case 18:capture("off-again");command("bts.project.save_as(root/'off.blend')");break;
  case 19:capture("off-saved");command("bts.project.close()\nbts.project.open(root/'off.blend')");break;
  case 20:capture("off-opened");get<QToolButton>("warpToGeometry")->setChecked(true);break;
  case 21:capture("reopened-resume");command("bts.project.close(discard=True)");timer.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
  }
 }
 public:Demo(QMainWindow *parent,std::function<bool()> pending,std::function<QJsonObject()> snapshot):QObject(parent),window(parent),directory(qEnvironmentVariable("BTS_WARP_GEOMETRY_DEMO")),busy(std::move(pending)),state(std::move(snapshot)){QDir().mkpath(directory);timer.setInterval(350);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_warp_geometry_demo(QMainWindow *window,std::function<bool()> busy,std::function<QJsonObject()> snapshot){new Demo(window,std::move(busy),std::move(snapshot));}
}
