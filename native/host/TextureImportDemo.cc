#include "TextureImportDemo.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QGuiApplication>
#include <QUrl>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QDropEvent>
#include <QMimeData>
#include <cmath>
namespace bts {namespace {
class ImportDemo final:public QObject {
 QWidget *w,*v;QString dir,mesh,fixtures;int resolution,stage=0,extra_channel=1;bool extra=qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_EXTRA_CHANNELS");QTimer timer,ping;QElapsedTimer ping_clock,loading_clock,deadline;double max_ping=0,first_visible_ms=-1;bool measuring=false;int undo_before=0;QJsonArray proofs;std::function<QJsonObject()> state;
 void require(bool ok,const char *message){if(!ok)qFatal("W import GUI: %s (stage=%d)",message,stage);}
 QAction *action(const char *id){auto *a=w->findChild<QAction *>(id);require(a,id);return a;}
 QComboBox *channel(int c){auto *box=w->findChild<QComboBox *>(c?QString("fillChannelResource%1").arg(c):QString("fillBaseColorResource"));require(box,"channel resource widget");return box;}
 void capture(const char *name){auto data=state();data["stage"]=name;QJsonArray rows;for(int c=0;c<15;++c){int count=0;for(auto *row:w->findChildren<QWidget *>(QString("fillChannelRow%1").arg(c)))count+=row->isVisible();rows.append(count);}data["visible_rows"]=rows;require(rows[3].toInt()==0,"new project must not expose Normal fill row");for(int c:{0,1,2,4})require(rows[c].toInt()==1,"fill channel missing or duplicated");if(extra)for(int c=0;c<15;++c)if(c!=3)require(rows[c].toInt()==1,"active channel missing or duplicated");proofs.append(data);w->setProperty("guiCapturePath",dir+"/"+name+"-viewport.png");action("guiCaptureViewport")->trigger();w->grab().save(dir+"/"+name+".png");}
 void load(int c,const QString &file){timer.stop();QTimer::singleShot(200,this,[this,file]{auto *d=w->findChild<QFileDialog *>("fillTextureFileDialog");require(d,"file selector");d->setDirectory(QFileInfo(file).absolutePath());d->selectFile(file);if(auto *edit=d->findChild<QLineEdit *>("fileNameEdit"))edit->setText(QFileInfo(file).fileName());qInfo("BTS_TEXTURE_DIALOG_TEST file=%s selection=%s",qPrintable(file),qPrintable(d->selectedFiles().join(";")));loading_clock.start();ping_clock.start();max_ping=0;first_visible_ms=-1;measuring=true;ping.start(10);QMetaObject::invokeMethod(d,"accept",Qt::DirectConnection);});channel(c)->setCurrentIndex(c?1:2);timer.start(20);}
 void import_shelf(){timer.stop();QTimer::singleShot(100,this,[this]{auto *dialog=w->findChild<QDialog *>("resourceImportDialog");require(dialog,"resource Import dialog");dialog->findChild<QLineEdit *>("resourceImportPath")->setText(fixtures+"/color.1001.exr");require(dialog->findChild<QComboBox *>("resourceImportUsage")->currentIndex()==1,"EXR import must default to Texture");dialog->findChild<QLineEdit *>("resourceImportName")->setText("W Shelf raw sequence");dialog->findChild<QComboBox *>("resourceImportScope")->setCurrentIndex(1);dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();});w->findChild<QToolButton *>("importResources")->click();timer.start(20);}
 void choose_shelf(int c){channel(c)->setCurrentIndex(c?2:3);auto *list=w->findChild<QListWidget *>("shelfAssets");for(int row=0;row<list->count();++row)if(list->item(row)->text()=="W Shelf raw sequence"){loading_clock.start();ping_clock.start();first_visible_ms=-1;max_ping=0;measuring=true;ping.start(10);QMetaObject::invokeMethod(list,"itemDoubleClicked",Qt::DirectConnection,Q_ARG(QListWidgetItem *,list->item(row)));return;}require(false,"Shelf texture missing");}
 void stop_measure(const char *name){ping.stop();measuring=false;proofs.append(QJsonObject{{"stage",name},{"visible_ms",first_visible_ms<0?double(loading_clock.elapsed()):first_visible_ms},{"finalized_ms",double(loading_clock.elapsed())},{"max_ui_ping_ms",max_ping}});}
 bool ready(){const auto s=state();return !s["texture_loading"].toBool()&&!s["composite_pending"].toBool();}
 void drop(const QString &name){auto *layers=w->findChild<QListWidget *>("layerStack");require(layers,"layers");QMimeData mime;mime.setUrls({QUrl::fromLocalFile(fixtures+"/"+name)});QDragEnterEvent enter(QPoint(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(layers->viewport(),&enter);QDropEvent event(QPointF(20,20),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(layers->viewport(),&event);require(event.isAccepted(),"image drop not accepted");}
 void advance(){if(deadline.elapsed()>180000)qFatal("W import GUI timed out stage=%d",stage);if(stage>2){const auto current=state();if(measuring&&first_visible_ms<0&&(stage==4||stage==5||stage==10||stage==14)){const auto tiles=current["sequence_tiles"].toArray();if(stage==10&&!current["texture_loading"].toBool()&&channel(2)->currentText()=="W Shelf raw sequence")first_visible_ms=loading_clock.elapsed();else if(stage==14&&!current["texture_loading"].toBool()&&current["layers"].toInt()==undo_before+1){const auto actual=current["native_color"].toArray(),expected=current["native_color_expected"].toArray();bool equal=actual.size()==4&&expected.size()==4;for(int c=0;equal&&c<4;++c)equal=std::abs(actual[c].toDouble()-expected[c].toDouble())<.011;if(equal)first_visible_ms=loading_clock.elapsed();}else if(!tiles.isEmpty()&&(stage==4||stage==5)){const auto values=tiles[0].toObject()[stage==4?"color":"roughness"].toArray();if(!values.isEmpty()&&std::abs(values[0].toDouble()-.25)<.011)first_visible_ms=loading_clock.elapsed();}}if(current["texture_loading"].toBool()||current["composite_pending"].toBool())return;if((stage==6||stage==12)&&!w->findChild<QMessageBox *>("textureLoadErrorDialog"))return;}switch(stage++){
 case 0:action("newProject")->trigger();break;
 case 1:{QDialog *dialog=nullptr;for(auto *d:w->findChildren<QDialog *>())if(d->findChild<QLineEdit *>("meshPath"))dialog=d;require(dialog,"New dialog");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=dialog->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));if(auto *tiles=dialog->findChild<QCheckBox *>("preserveUvTiles"))tiles->setChecked(true);dialog->findChild<QPushButton *>("createProject")->click();break;}
 case 2:action("guiTextureImportPrepare")->trigger();if(extra){action("addChannel5")->trigger();for(int slot=0;slot<8;++slot)action(qPrintable(QString("addUserChannel%1").arg(slot)))->trigger();w->findChild<QCheckBox *>("displacementChannel")->setChecked(true);}w->findChild<QToolButton *>("addFillLayer")->click();break;
 case 3:capture("uniform");undo_before=state()["undo_count"].toInt();load(0,fixtures+"/color_1001.exr");break;
 case 4:stop_measure("color-load-latency");capture("color-loaded");require(state()["fill_mode"].toInt()==6,"UDIM must select Match per UV Tile");require(state()["undo_count"].toInt()==undo_before+1,"one image assignment must be one undo");load(2,fixtures+"/color-1001.exr");break;
 case 5:stop_measure("roughness-load-latency");capture("roughness-loaded");action("checkGuiComposite")->trigger();require(state()["composite_differences"].toArray().isEmpty(),"native composite mismatch");undo_before=state()["undo_count"].toInt();load(0,fixtures+"/corrupt.exr");break;
 case 6:{stop_measure("corrupt-load-latency");auto *error=w->findChild<QMessageBox *>("textureLoadErrorDialog");require(error,"corrupt EXR must be visible error");error->close();require(state()["undo_count"].toInt()==undo_before,"failed decode modified Undo");capture("corrupt-unchanged");channel(2)->setCurrentIndex(0);break;}
 case 7:capture("roughness-uniform");action("undoDocument")->trigger();break;
 case 8:capture("roughness-restored");import_shelf();break;
 case 9:capture("shelf-imported");choose_shelf(2);break;
 case 10:stop_measure("shelf-load-latency");capture("shelf-bound");undo_before=state()["undo_count"].toInt();load(0,fixtures+"/image16.png");w->findChild<QPushButton *>("fillTextureCancel0")->click();break;
 case 11:stop_measure("cancelled-load-latency");require(state()["undo_count"].toInt()==undo_before,"cancelled load changed Undo");capture("cancelled-unchanged");load(0,fixtures+"/missing_1001.exr");break;
 case 12:{stop_measure("missing-load-latency");auto *error=w->findChild<QMessageBox *>("textureLoadErrorDialog");require(error&&error->text().contains("1002"),"missing tile error must identify tile");error->close();require(state()["undo_count"].toInt()==undo_before,"missing tile changed Undo");capture("missing-unchanged");w->findChild<QToolButton *>("addPaintLayer")->click();break;}
 case 13:undo_before=state()["layers"].toInt();load(0,fixtures+"/image8.png");break;
 case 14:stop_measure("paint-import-latency");require(state()["layers"].toInt()==undo_before+1,"paint assignment must create fill");capture("paint-imported-as-fill");drop("image16.png");break;
 case 15:if(extra&&extra_channel<15){if(extra_channel==3)++extra_channel;const int c=extra_channel++;capture(qPrintable(QString("before-channel-%1").arg(c)));load(c,fixtures+"/image16.png");--stage;break;}capture("drop-created-fill");action("guiTextureImportSave")->trigger();capture("saved-packed");action("guiTextureImportHideSources")->trigger();action("guiTextureImportOpen")->trigger();break;
 case 16:{capture("opened-packed");QFile file(dir+"/texture-import-gui.json");require(file.open(QIODevice::WriteOnly),"proof output");file.write(QJsonDocument(QJsonObject{{"captures",proofs},{"platform",QGuiApplication::platformName()},{"locale",qEnvironmentVariable("LC_ALL")},{"resolution",resolution},{"sources_absent",!QFile::exists(fixtures+"/color_1001.exr")}}).toJson());file.close();timer.stop();QTimer::singleShot(50,qApp,&QCoreApplication::quit);break;}
 }}
 public:ImportDemo(QWidget *workspace,QWidget *viewport,QString output,QString m,int r,std::function<QJsonObject()> s):QObject(workspace),w(workspace),v(viewport),dir(output),mesh(m),fixtures(qEnvironmentVariable("BTS_TEXTURE_IMPORT_FIXTURES")),resolution(r),state(std::move(s)){QDir().mkpath(dir);deadline.start();QTimer::singleShot(180000,this,[this]{qFatal("W import GUI watchdog stage=%d",stage);});timer.setInterval(200);QObject::connect(&timer,&QTimer::timeout,this,[this]{advance();});QObject::connect(&ping,&QTimer::timeout,this,[this]{if(measuring){max_ping=std::max(max_ping,double(ping_clock.restart()));}});timer.start();}
};}
void start_texture_import_demo(QWidget *w,QWidget *v,const QString &dir,const QString &mesh,int res,std::function<QJsonObject()> state){new ImportDemo(w,v,dir,mesh,res,std::move(state));}
}
