#include "RibbonDemo.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
class RibbonDemo final:public QObject {
 QWidget *w,*v;QString dir,mesh,image;int resolution,stage=0;QTimer timer;QJsonArray captures;std::function<QJsonObject()> state;
 void action(const char *name){auto *a=w->findChild<QAction *>(name);if(!a)qFatal("Ribbon action missing: %s",name);a->trigger();}
 void mouse(QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,p,v->mapToGlobal(p),button,buttons,Qt::NoModifier);QCoreApplication::sendEvent(v,&e);}
 QPointF point(int index){return w->property("ribbonPoints").toList().at(index).toPointF();}
 void click(int index){const auto p=point(index);mouse(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);}
 void settings(std::function<void(QDialog *)> change){timer.stop();QTimer::singleShot(100,this,[this,change]{auto *d=w->findChild<QDialog *>("ribbonSettingsDialog");if(!d)qFatal("Ribbon settings missing");change(d);d->grab().save(dir+"/settings.png");d->findChild<QPushButton *>("commitRibbonSettings")->click();});w->findChild<QPushButton *>("surfaceRibbonSettings")->click();timer.start();}
 void capture(const char *name){auto data=state();data["stage"]=name;captures.append(data);w->grab().save(dir+"/"+name+".png");}
 void advance(){if(stage>2&&state()["composite_pending"].toBool())return;qInfo("BTS_RIBBON_DEMO_STAGE %d",stage);switch(stage++){
 case 0:action("newProject");break;
 case 1:{auto *d=w->findChild<QDialog *>("newProjectDialog");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=d->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));d->findChild<QPushButton *>("createProject")->click();break;}
 case 2:action("guiRibbonPrepare");w->findChild<QToolButton *>("addPaintLayer")->click();break;
 case 3:action("selectSurfacePath");w->findChild<QDoubleSpinBox *>("brushSize")->setValue(18);if(w->findChild<QToolButton *>("brushPressureSize")->isChecked())w->findChild<QToolButton *>("brushPressureSize")->click();break;
 case 4:settings([this](QDialog *d){d->findChild<QCheckBox *>("ribbonEnabled")->setChecked(true);d->findChild<QComboBox *>("ribbonTiling")->setCurrentIndex(0);QTimer::singleShot(100,this,[this]{auto *picker=w->findChild<QFileDialog *>("ribbonFilePicker");if(!picker)qFatal("Ribbon file picker missing");picker->setDirectory(QFileInfo(image).absolutePath());picker->selectFile(image);if(auto *field=picker->findChild<QLineEdit *>("fileNameEdit"))field->setText(QFileInfo(image).fileName());qInfo("BTS_RIBBON_FILE_TEST selected=%s",qPrintable(picker->selectedFiles().join(";")));QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);});d->findChild<QPushButton *>("ribbonChooseImage")->click();});break;
 case 5:click(0);break;
 case 6:click(1);break;
 case 7:click(2);break;
 case 8:capture("stretched");settings([](QDialog *d){d->findChild<QComboBox *>("ribbonTiling")->setCurrentIndex(2);d->findChild<QSpinBox *>("ribbonCount")->setValue(4);d->findChild<QCheckBox *>("ribbonMirror")->setChecked(true);});break;
 case 9:capture("repeated");action("undoDocument");break;
 case 10:capture("undo-repeat");action("redoDocument");break;
 case 11:capture("redo-repeat");settings([](QDialog *d){d->findChild<QComboBox *>("ribbonStretch")->setCurrentIndex(1);d->findChild<QComboBox *>("ribbonAlignment")->setCurrentIndex(1);d->findChild<QCheckBox *>("ribbonRemoveClipped")->setChecked(true);});break;
 case 12:capture("aspect");settings([](QDialog *d){d->findChild<QComboBox *>("ribbonOrientation")->setCurrentIndex(1);d->findChild<QCheckBox *>("ribbonFlip")->setChecked(true);d->findChild<QComboBox *>("ribbonCorner")->setCurrentIndex(1);});break;
 case 13:capture("oriented");click(1);w->findChild<QToolButton *>("surfaceVertexSmooth")->click();break;
 case 14:capture("curved");w->findChild<QDoubleSpinBox *>("brushSize")->setValue(24);break;
 case 15:capture("wider");action("guiRibbonSave");break;
 case 16:capture("saved");if(!QFile::rename(image,image+".removed"))qFatal("Cannot move ribbon source");action("guiRibbonOpen");break;
 case 17:capture("opened-packed");action("selectSurfacePath");w->findChild<QListWidget *>("pathsList")->setCurrentRow(0);settings([](QDialog *d){d->findChild<QComboBox *>("ribbonOrientation")->setCurrentIndex(2);});break;
 case 18:{capture("edited-after-open");timer.stop();QFile f(dir+"/ribbon.json");if(!f.open(QIODevice::WriteOnly))qFatal("Ribbon proof write failed");f.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());f.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
 }}
 public:RibbonDemo(QWidget *workspace,QWidget *viewport,QString d,QString m,int r,std::function<QJsonObject()> s):QObject(workspace),w(workspace),v(viewport),dir(d),mesh(m),resolution(r),state(std::move(s)){QDir().mkpath(dir);image=dir+"/ribbon-source.png";QImage source(128,32,QImage::Format_RGBA8888);for(int y=0;y<32;++y)for(int x=0;x<128;++x)source.setPixelColor(x,y,y<4||y>27?QColor(Qt::white):x<64?QColor(Qt::red):QColor(Qt::blue));if(!source.save(image))qFatal("Ribbon source write failed");timer.setInterval(800);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_ribbon_demo(QWidget *w,QWidget *v,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new RibbonDemo(w,v,d,m,r,std::move(state));}
}
