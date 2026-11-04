#include "BakeFixDemo.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
namespace bts {namespace {
class BakeDemo final:public QObject {
 QWidget *w;QString dir,mesh;int stage=0,resolution;QTimer timer,poll;std::function<QJsonObject()> state;QJsonArray captures,dialog_states;
 QAction *action(const char *name){auto *a=w->findChild<QAction *>(name);if(!a)qFatal("Bake GUI action missing: %s",name);return a;}
 void capture(const char *name){auto s=state();s["stage"]=name;captures.append(s);}
 void inspect(QDialog *d,const char *name){auto *button=d->findChild<QPushButton *>("startMeshBake");auto *source=d->findChild<QCheckBox *>("bakeUseProjectMesh");dialog_states.append(QJsonObject{{"stage",name},{"enabled",button->isEnabled()},{"tooltip",button->toolTip()},{"help",d->findChild<QLabel *>("bakeDisabledReason")->text()},{"project_mesh",source->isChecked()},{"source_visible",source->isVisible()&&d->rect().contains(source->mapTo(d,QPoint{}))}});}
 void bake(bool first){timer.stop();QTimer::singleShot(100,this,[this,first]{auto *d=w->findChild<QDialog *>("meshBakeDialog");if(!d)qFatal("Bake dialog missing");auto *source=d->findChild<QCheckBox *>("bakeUseProjectMesh");auto *maps=d->findChild<QListWidget *>("bakeMapTypes");auto *sets=d->findChild<QTreeWidget *>("bakeTextureSets");auto *cage=d->findChild<QCheckBox *>("bakeUseCage");
   if(first){inspect(d,"default");source->setChecked(false);inspect(d,"no-source");source->setChecked(true);for(int i=0;i<7;++i)maps->item(i)->setCheckState(Qt::Unchecked);inspect(d,"no-maps");maps->item(1)->setCheckState(Qt::Checked);for(int i=0;i<sets->topLevelItemCount();++i)sets->topLevelItem(i)->setCheckState(0,Qt::Unchecked);inspect(d,"no-tiles");for(int i=0;i<sets->topLevelItemCount();++i)sets->topLevelItem(i)->setCheckState(0,Qt::Checked);cage->setChecked(true);inspect(d,"no-cage");cage->setChecked(false);source->setChecked(false);d->grab().save(dir+"/disabled.png");d->findChild<QPushButton *>("saveBakeSettings")->click();return;}
   inspect(d,"saved-explicit-source-off");source->setChecked(true);for(int i=0;i<7;++i)maps->item(i)->setCheckState(i==3||i==4?Qt::Checked:Qt::Unchecked);auto *size=d->findChild<QComboBox *>("bakeResolution");size->setCurrentIndex(size->findData(512));d->findChild<QSpinBox *>("bakeSecondaryRays")->setValue(8);inspect(d,"ready-ao-curvature");d->grab().save(dir+"/ready.png");poll.start();d->findChild<QPushButton *>("startMeshBake")->click();
 });action("bakeMeshMapsAction")->trigger();timer.start(500);}
 void advance(){switch(stage++){
 case 0:for(auto *a:w->findChildren<QAction *>())if(a->text()=="&New…"){a->trigger();return;}qFatal("Bake New action missing");
 case 1:{QDialog *d=nullptr;for(auto *c:w->findChildren<QDialog *>())if(c->findChild<QLineEdit *>("meshPath"))d=c;if(!d)qFatal("Bake New dialog missing");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=d->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));d->findChild<QPushButton *>("createProject")->click();break;}
 case 2:capture("created");bake(true);capture("settings-saved");action("guiBakeFixSave")->trigger();action("guiBakeFixOpen")->trigger();capture("preferences-opened");break;
 case 3:bake(false);capture("baked");action("undoDocument")->trigger();break;
 case 4:if(state()["composite_pending"].toBool()){--stage;return;}capture("undo");action("redoDocument")->trigger();break;
 case 5:if(state()["composite_pending"].toBool()){--stage;return;}capture("redo");action("guiBakeFixSave")->trigger();action("guiBakeFixOpen")->trigger();break;
 case 6:{capture("maps-opened");timer.stop();w->grab().save(dir+"/workspace.png");QFile file(dir+"/bake-fix.json");if(!file.open(QIODevice::WriteOnly))qFatal("Bake proof unavailable");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures},{"dialogs",dialog_states}}).toJson());file.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
 }}
 public:BakeDemo(QWidget *workspace,QString d,QString m,int r,std::function<QJsonObject()> s):QObject(workspace),w(workspace),dir(d),mesh(m),resolution(r),state(std::move(s)){QDir().mkpath(dir);timer.setInterval(1000);connect(&timer,&QTimer::timeout,this,[this]{advance();});poll.setInterval(50);connect(&poll,&QTimer::timeout,this,[this]{auto *d=w->findChild<QDialog *>("meshBakeProgress");if(!d)return;auto *finish=d->findChild<QPushButton *>("finishMeshBake");if(finish->text()=="Close")qFatal("Bake failed: %s",qPrintable(d->findChild<QLabel *>("meshBakeProgressStatus")->text()));if(finish->text()=="OK"){poll.stop();finish->click();}});timer.start();}
};}
void start_bake_fix_demo(QWidget *w,QWidget *,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new BakeDemo(w,d,m,r,std::move(state));}
}
