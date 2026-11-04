#include "PythonConsole.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QOpenGLWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
QString quote(const QString &v){const auto bytes=QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);return QString::fromUtf8(bytes.mid(1,bytes.size()-2));}
class Demo final:public QObject {
 QMainWindow *window;QString directory;std::function<bool()> busy;std::function<QJsonObject()> state,stats;QTimer timer;QElapsedTimer elapsed;int phase=0,ticks=0,set_index=0;bool coarse=false;QJsonArray captures;
 template<class T>T *get(const char *name){auto *v=window->findChild<T *>(name);if(!v)qFatal("Residency control missing: %s",name);return v;}
 void command(const QString &code){get<QLineEdit>("pythonCommand")->setText("exec("+quote(code)+")");get<QPushButton>("executePythonCommand")->click();if(!get<QDockWidget>("pythonConsoleDock")->property("lastExecutionSucceeded").toBool())qFatal("Residency script failed: %s",qPrintable(get<QPlainTextEdit>("pythonOutput")->toPlainText()));}
 void capture(const char *name,bool light=false){const qint64 measured_ms=elapsed.isValid()?elapsed.elapsed():0;auto value=light?QJsonObject{{"residency",stats()}}:state();value["stage"]=name;value["elapsed_ms"]=measured_ms;captures.append(value);QFile out(directory+"/residency.json");if(!out.open(QIODevice::WriteOnly))qFatal("Residency proof unavailable");out.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());window->findChild<QOpenGLWidget *>()->grabFramebuffer().save(directory+"/"+name+".png");}

 void camera(int index){auto *c=get<QComboBox>("sceneCameraSelector");const int found=c->findText(QString("Camera%1").arg(index));if(found<0)qFatal("Residency imported camera missing");get<QListWidget>("textureSets")->setCurrentRow(index);c->setCurrentIndex(found);elapsed.restart();coarse=false;}
 void finish(){command("bts.project.close(discard=True)");timer.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);}
 void advance(){if(++ticks>18000)qFatal("Residency demo timed out phase=%d",phase);auto *v=window->findChild<QOpenGLWidget *>();if(!v||!v->isValid()||(phase!=1&&busy()))return;
  if(qEnvironmentVariableIsSet("BTS_RESIDENCY_RESTART")){QFile out(directory+"/restart.json");out.open(QIODevice::WriteOnly);out.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"full_resolution",true}}).toJson());timer.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);return;}
  const auto settled=[&]{return !stats().isEmpty()&&!stats()["refining"].toBool();};
  switch(phase){
  case 0:get<QAction>("showPythonConsole")->trigger();command("import bts,pathlib,json,hashlib\nroot=pathlib.Path("+quote(directory)+")\nC=bts.textureset.ChannelType");if(qEnvironmentVariableIsSet("BTS_RESIDENCY_OPEN_ONLY")){command("bts.project.open(root/'resident.blend')");phase=4;}else{get<QAction>("newProject")->trigger();++phase;}break;
  case 1:{auto *d=get<QDialog>("newProjectDialog");auto *preset=d->findChild<QComboBox *>("projectTemplate");preset->setCurrentIndex(preset->findData(":pbr"));d->findChild<QLineEdit *>("meshPath")->setText(directory+"/residency.dae");auto *r=d->findChild<QComboBox *>("documentResolution");r->setCurrentIndex(r->findData(2048));d->findChild<QPushButton *>("createProject")->click();++phase;break;}
  case 2:command("asset=bts.resource.import_image(root/'pattern.png',scope='project',usage='texture')");++phase;break;
  case 3:if(set_index<8){get<QListWidget>("textureSets")->setCurrentRow(set_index++);get<QToolButton>("addFillLayer")->click();command("fill=bts.layerstack.get_root_layer_nodes(bts.textureset.get_active_stack())[0]\nfill.active_channels={C.BaseColor,C.Metallic,C.Roughness,C.Bump}\nfor channel in (C.BaseColor,C.Metallic,C.Roughness,C.Bump): fill.set_source(channel,asset['id'])");}else ++phase;break;
  case 4:if(!settled())return;capture("wide");camera(0);++phase;break;
  case 5:if(stats()["refining"].toBool()&&!coarse){capture("front0-coarse",true);coarse=true;}if(!settled())return;capture("front0");if(qEnvironmentVariableIsSet("BTS_RESIDENCY_OPEN_ONLY")){camera(7);++phase;break;}command("sets=bts.textureset.all_texture_sets()\nexport_config={'directory':str(root/'before'),'texture_sets':[sets[0]['id']],'format':'png16','preset':'document','size':128}\nresult=bts.export.export_project_textures(export_config)\nassert result['status']=='success',result\nbefore={pathlib.Path(p).name:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()for p in result['files']}");camera(7);++phase;break;
  case 6:if(stats()["refining"].toBool()&&!coarse){capture("front7-coarse",true);coarse=true;}if(!settled())return;capture("front7");camera(0);++phase;break;
  case 7:if(!settled())return;capture("revisited");if(qEnvironmentVariableIsSet("BTS_RESIDENCY_OPEN_ONLY")){finish();++phase;break;}elapsed.restart();++phase;break;
  case 8:if(!settled())return;capture("full-resolution");command("export_config['directory']=str(root/'after')\nresult=bts.export.export_project_textures(export_config)\nassert result['status']=='success',result\nafter={pathlib.Path(p).name:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()for p in result['files']}\nassert before==after\n(root/'export-exact.json').write_text(json.dumps({'files':before,'byte_exact':True},indent=2))\nbts.project.save_as(root/'resident.blend')");++phase;break;
  case 9:if(!settled())return;capture("saved");command("bts.project.close()\nbts.project.open(root/'resident.blend')");++phase;break;
  case 10:if(!settled())return;capture("opened");finish();++phase;break;
  }
 }
 public:Demo(QMainWindow *parent,std::function<bool()> pending,std::function<QJsonObject()> snapshot,std::function<QJsonObject()> statistics):QObject(parent),window(parent),directory(qEnvironmentVariable("BTS_RESIDENCY_DEMO")),busy(std::move(pending)),state(std::move(snapshot)),stats(std::move(statistics)){QDir().mkpath(directory);timer.setInterval(16);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_residency_demo(QMainWindow *window,std::function<bool()> busy,std::function<QJsonObject()> state,std::function<QJsonObject()> stats){new Demo(window,std::move(busy),std::move(state),std::move(stats));}
}
