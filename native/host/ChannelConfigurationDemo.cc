#include "ChannelConfiguration.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QOpenGLWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
QString quote(const QString &value){const auto bytes=QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);return QString::fromUtf8(bytes.mid(1,bytes.size()-2));}
class Demo final:public QObject {
  QMainWindow *window;QString directory,mesh;std::function<bool()> busy;std::function<QJsonObject()> state;QTimer timer;int phase=0,ticks=0;QJsonArray captures;
  template<class T>T *get(const char *name){auto *value=window->findChild<T *>(name);if(!value)qFatal("Batch channel GUI control missing: %s",name);return value;}
  void command(const QString &code){get<QLineEdit>("pythonCommand")->setText("exec("+quote(code)+")");get<QPushButton>("executePythonCommand")->click();if(!get<QDockWidget>("pythonConsoleDock")->property("lastExecutionSucceeded").toBool())qFatal("Batch script failed: %s",qPrintable(get<QPlainTextEdit>("pythonOutput")->toPlainText()));}
  void capture(const char *name){auto data=state();data["stage"]=name;captures.append(data);QFile proof(directory+"/channels.json");if(!proof.open(QIODevice::WriteOnly))qFatal("Cannot write batch proof");proof.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());window->findChild<QOpenGLWidget *>()->grabFramebuffer().save(directory+"/"+name+"-viewport.png");window->grab().save(directory+"/"+name+"-workspace.png");}
  void configure(bool cancel,bool all){
    QTimer::singleShot(30,this,[this,cancel,all]{
      auto *dialog=get<QDialog>("channelConfigurationDialog");
      dialog->findChild<QCheckBox *>("batchChannelEnabled1")->setChecked(false);
      dialog->findChild<QCheckBox *>("batchChannelEnabled5")->setChecked(true);
      dialog->findChild<QCheckBox *>("batchChannelEnabled14")->setChecked(true);
      dialog->findChild<QComboBox *>("batchChannelPrecision2")->setCurrentIndex(int(ChannelPrecision::L16F));
      dialog->findChild<QCheckBox *>("batchChannelsAllSets")->setChecked(all);
      if(all)dialog->findChild<QCheckBox *>("batchChannelEnabled4")->setChecked(false);
      dialog->grab().save(directory+(cancel?"/cancel-dialog.png":all?"/all-dialog.png":"/local-dialog.png"));
      if(cancel){dialog->findChild<QPushButton *>("cancelChannelConfiguration")->click();return;}
      QTimer::singleShot(30,this,[this]{auto *warning=get<QMessageBox>("confirmChannelRemoval");warning->button(QMessageBox::Ok)->click();});
      dialog->findChild<QPushButton *>("applyChannelConfiguration")->click();
    });
    get<QAction>("configureChannels")->trigger();
  }
  void advance(){if(++ticks>1000)qFatal("Batch channels demo timed out");auto *view=window->findChild<QOpenGLWidget *>();if(!view||!view->isValid()||busy())return;
    switch(phase++){
    case 0:get<QAction>("showPythonConsole")->trigger();command("import bts");break;
    case 1:command("bts.project.create("+quote(mesh)+",settings=bts.project.Settings(resolution=128,import_cameras=False))");break;
    case 2:{auto *list=get<QListWidget>("textureSets");if(list->count()!=2)qFatal("Batch fixture needs two Texture Sets");list->setCurrentRow(1);get<QToolButton>("removeChannel1")->click();list->setCurrentRow(0);get<QAction>("addChannel5")->trigger();break;}
    case 3:capture("before");configure(true,false);break;
    case 4:capture("cancel");configure(false,false);break;
    case 5:capture("local");get<QListWidget>("textureSets")->setCurrentRow(1);configure(false,true);break;
    case 6:capture("all");get<QAction>("undoDocument")->trigger();break;
    case 7:capture("undo");get<QAction>("redoDocument")->trigger();break;
    case 8:capture("redo");command("bts.project.save_as("+quote(directory+"/channels.blend")+")");break;
    case 9:capture("saved");command("bts.project.close()\nbts.project.open("+quote(directory+"/channels.blend")+")");break;
    case 10:capture("opened");command("bts.project.close()");timer.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
    }
    if(phase>3){view->grabFramebuffer().save(directory+QString("/phase-%1-viewport.png").arg(phase));window->grab().save(directory+"/workspace.png");}
  }
 public:Demo(QMainWindow *parent,std::function<bool()> pending,std::function<QJsonObject()> snapshot):QObject(parent),window(parent),directory(qEnvironmentVariable("BTS_BATCH_CHANNEL_DEMO")),mesh(qEnvironmentVariable("BTS_BATCH_CHANNEL_MESH")),busy(std::move(pending)),state(std::move(snapshot)){QDir().mkpath(directory);timer.setInterval(300);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_channel_configuration_demo(QMainWindow *window,std::function<bool()> busy,std::function<QJsonObject()> snapshot){new Demo(window,std::move(busy),std::move(snapshot));}
}
