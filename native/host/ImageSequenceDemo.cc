#include "ViewportDemoCapture.hh"
#include "ImageSequenceDemo.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
class SequenceDemo final:public QObject {
 QWidget *w,*v;QString dir,mesh;int resolution,stage=0;QTimer timer;QJsonArray captures;std::function<QJsonObject()> state;
 void action(const char *name){auto *a=w->findChild<QAction *>(name);if(!a)qFatal("Sequence action missing: %s",name);a->trigger();}
 void combo(const char *name,int value){auto *c=w->findChild<QComboBox *>(name);if(!c)qFatal("Sequence combo missing: %s",name);c->setCurrentIndex(value);}
 void number(const char *name,double value){auto *c=w->findChild<QDoubleSpinBox *>(name);if(!c)qFatal("Sequence number missing: %s",name);c->setValue(value);QMetaObject::invokeMethod(c,"editingFinished",Qt::DirectConnection);}
 void capture(const char *name){auto data=state();data["stage"]=name;data["transforms_visible"]=w->findChild<QWidget *>("fillTransforms")->isVisible();data["filter_visible"]=w->findChild<QWidget *>("fillFiltering")->isVisible();data["frame_visible"]=w->findChild<QWidget *>("fillToolbarManipulator")->isVisible();data["panel_frame_visible"]=w->findChild<QWidget *>("fillFrameControls")->isVisible();data["spatial_disclosure_visible"]=w->findChild<QWidget *>("fill3dDisclosure")->isVisible();const auto thumbnail=w->findChild<QListWidget *>("layerStack")->currentItem()->icon().pixmap(48).toImage();data["thumbnail_hash"]=QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromRawData(reinterpret_cast<const char *>(thumbnail.constBits()),thumbnail.sizeInBytes()),QCryptographicHash::Sha256).toHex());QJsonArray shelf;auto *list=w->findChild<QListWidget *>("shelfAssets");for(int i=0;i<list->count();++i){const auto *item=list->item(i);if(item->text().startsWith("PD103"))shelf.append(QJsonObject{{"name",item->text()},{"count",item->data(Qt::UserRole+20).toInt()}});}data["sequence_shelf"]=shelf;captures.append(data);capture_demo_window(w,v).save(dir+"/"+name+".png");if(auto *gl=qobject_cast<QOpenGLWidget *>(v))capture_viewport_framebuffer(gl).save(dir+"/"+name+"-viewport.png");}
 void shelf(){auto *list=w->findChild<QListWidget *>("shelfAssets");for(int i=0;i<list->count();++i)if(list->item(i)->text()=="PD103-color"){QMetaObject::invokeMethod(list,"itemDoubleClicked",Qt::DirectConnection,Q_ARG(QListWidgetItem *,list->item(i)));return;}qFatal("Grouped sequence Shelf item missing");}
 void advance(){if(stage>2){const auto data=state();if(data["composite_pending"].toBool()||data["property_pending"].toBool())return;}switch(stage++){
 case 0:action("newProject");break;
 case 1:{auto *dialog=w->findChild<QDialog *>("newProjectDialog");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);dialog->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);auto *size=dialog->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));dialog->findChild<QPushButton *>("createProject")->click();break;}
 case 2:action("guiPlanarPrepare");capture("created");timer.stop();QTimer::singleShot(100,this,[this]{auto *dialog=w->findChild<QDialog *>("resourceImportDialog");if(!dialog)qFatal("Sequence import dialog missing");dialog->findChild<QLineEdit *>("resourceImportPath")->setText(dir+"/PD103-color_1001.png");dialog->findChild<QComboBox *>("resourceImportUsage")->setCurrentIndex(1);dialog->findChild<QComboBox *>("resourceImportScope")->setCurrentIndex(1);dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});w->findChild<QToolButton *>("importResources")->click();timer.start();break;
 case 3:capture("imported-only");w->findChild<QToolButton *>("addFillLayer")->click();break;
 case 4:combo("fillProjectionMode",6);break;
 case 5:capture("match-uniform");w->findChild<QLineEdit *>("shelfSearch")->setText("u:texture");shelf();break;
 case 6:capture("matched-color");timer.stop();QTimer::singleShot(100,this,[this]{auto *picker=w->findChild<QFileDialog *>("fillTextureFileDialog");if(!picker)qFatal("Sequence texture picker missing");picker->selectFile(dir+"/PD103-data_1001.png");QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);});combo("fillChannelResource2",1);timer.start();break;
 case 7:capture("matched-data");combo("fillProjectionMode",0);break;
 case 8:capture("ordinary-uv");number("fillScaleU",2);number("fillOffset0",.2);break;
 case 9:capture("uv-transformed");combo("fillProjectionMode",6);break;
 case 10:capture("matching-ignores-transform");action("undoDocument");break;
 case 11:capture("undo-mode");action("redoDocument");break;
 case 12:capture("redo-mode");combo("fillChannelResource2",0);break;
 case 13:capture("roughness-uniform");action("undoDocument");break;
 case 14:capture("undo-uniform");action("checkGuiComposite");break;
 case 15:capture("reference");action("guiPlanarSave");break;
 case 16:capture("saved");for(const auto &name:{"PD103-color_1001.png","PD103-color_1002.png","PD103-data_1001.png","PD103-data_1002.png"}){const auto path=dir+"/"+name;if(!QFile::rename(path,path+".removed"))qFatal("Sequence source removal failed");}action("guiPlanarOpen");break;
 case 17:capture("opened-packed");combo("fillBaseColorResource",0);break;
 case 18:capture("color-uniform-open");action("undoDocument");break;
 case 19:capture("undo-open");action("checkGuiComposite");break;
 case 20:combo("fillBaseColorResource",1);break;
 case 21:combo("fillScaleMode",0);break;
 case 22:capture("atom-matched");combo("fillProjectionMode",0);break;
 case 23:capture("atom-uv");action("undoDocument");break;
 case 24:capture("atom-restored");action("checkGuiComposite");break;
 case 25:{capture("final");timer.stop();QFile file(dir+"/image-sequence.json");if(!file.open(QIODevice::WriteOnly))qFatal("Sequence proof unavailable");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());file.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
 }}
 public:SequenceDemo(QWidget *workspace,QWidget *viewport,QString d,QString m,int r,std::function<QJsonObject()> s):QObject(workspace),w(workspace),v(viewport),dir(d),mesh(m),resolution(r),state(std::move(s)){QDir().mkpath(dir);for(int tile=1001;tile<=1002;++tile){QImage color(tile==1001?64:32,tile==1001?32:64,QImage::Format_RGBA8888);color.fill(tile==1001?Qt::red:Qt::green);if(!color.save(dir+QString("/PD103-color_%1.png").arg(tile)))qFatal("Sequence color source write failed");QImage data(32,32,QImage::Format_RGBA8888);data.fill(tile==1001?QColor(64,224,16):QColor(192,16,224));if(!data.save(dir+QString("/PD103-data_%1.png").arg(tile)))qFatal("Sequence data source write failed");}timer.setInterval(700);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_image_sequence_demo(QWidget *w,QWidget *v,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new SequenceDemo(w,v,d,m,r,std::move(state));}
}
