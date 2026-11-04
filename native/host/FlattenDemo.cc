#include "FlattenDemo.hh"
#include "ViewportDemoCapture.hh"
#include "EvaluationScope.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QCheckBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <QToolButton>
namespace bts {namespace {
class Demo final:public QObject {
 QWidget *w,*v;QString dir,mesh,group;int size,phase=0,ticks=0,frame=0;QTimer timer,ping;QElapsedTimer clock;qint64 last=0,max_ping=0;bool was_flattening=false;QJsonArray captures,exports,source_plan;std::function<QJsonObject()> state;
 template<class T>T *get(const char *name){auto *p=w->findChild<T *>(name);if(!p)qFatal("Flatten control missing: %s",name);return p;}
 void action(const char *name){const bool modal=qstrcmp(name,"flattenLayer")==0;if(modal)timer.stop();get<QAction>(name)->trigger();if(modal)timer.start();}
 QJsonObject source_proof(){timer.stop();if(source_plan.isEmpty()){action("guiFlattenProbePlan");source_plan=w->property("flattenProbePlan").toJsonArray();}QJsonObject hashes;for(const auto &value:source_plan){const auto request=value.toObject();w->setProperty("flattenProbeRequest",request);action("guiFlattenProbeStart");QEventLoop loop;QTimer wait;QElapsedTimer elapsed;elapsed.start();wait.setInterval(5);connect(&wait,&QTimer::timeout,&loop,[&]{action("guiFlattenProbePoll");if(w->property("flattenProbeReady").toBool())loop.quit();if(elapsed.elapsed()>10000)qFatal("Flatten source proof timed out");});wait.start();loop.exec();const auto key=request["layer"].toString()+"/"+QString::number(request["tile"].toInt())+"/"+QString::number(request["channel"].toInt())+"/"+QString::number(request["mask"].toBool())+"/"+request["page"].toString();hashes[key]=w->property("flattenProbeHash").toString();}timer.start();return hashes;}
 void capture(const char *name){if(qstrcmp(name,"before")==0||qstrcmp(name,"flattened")==0||qstrcmp(name,"opened")==0)action("guiFlattenMaterialProbes");QJsonObject pixels;if(qstrcmp(name,"saved")==0||qstrcmp(name,"opened")==0)pixels=source_proof();auto s=state();if(!pixels.isEmpty())s["gpu_source_pages"]=pixels;s["stage"]=name;s["flattened_assets"]=get<QListWidget>("shelfAssets")->count();captures.append(s);QFile f(dir+"/flatten.json");if(!f.open(QIODevice::WriteOnly))qFatal("Flatten proof write failed");f.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"flatten_ping_ms",max_ping},{"captures",captures},{"exports",exports}}).toJson());f.close();capture_viewport_framebuffer(v).save(dir+"/"+name+"-viewport.png");capture_demo_window(w,v).save(dir+"/"+name+"-window.png");}
 void paint(){auto p=state()["skew_uv_point"].toArray();QPointF from(p[0].toDouble(),p[1].toDouble());const auto send=[&](QEvent::Type t,QPointF point,Qt::MouseButton b,Qt::MouseButtons bs){QMouseEvent e(t,point,v->mapToGlobal(point.toPoint()),b,bs,Qt::NoModifier);QCoreApplication::sendEvent(v,&e);};send(QEvent::MouseButtonPress,from,Qt::LeftButton,Qt::LeftButton);for(int i=1;i<=12;++i)send(QEvent::MouseMove,from+QPointF(i*2.,i*.6),Qt::NoButton,Qt::LeftButton);send(QEvent::MouseButtonRelease,from+QPointF(24,7.2),Qt::LeftButton,Qt::NoButton);}
 void frame_views(){action("guiSkewPrepare");const auto panes=state()["view"].toObject()["panes"].toArray();for(const auto &item:panes){const auto pane=item.toObject();if(pane["view"].toString()!="3D")continue;const auto r=pane["rect"].toArray();const double scale=v->devicePixelRatioF();const QPointF point((r[0].toDouble()+r[2].toDouble()*.5)/scale,(r[1].toDouble()+r[3].toDouble()*.5)/scale);QMouseEvent press(QEvent::MouseButtonPress,point,v->mapToGlobal(point.toPoint()),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier),release(QEvent::MouseButtonRelease,point,v->mapToGlobal(point.toPoint()),Qt::MiddleButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(v,&press);QApplication::sendEvent(v,&release);v->setFocus();QKeyEvent frame(QEvent::KeyPress,Qt::Key_F,Qt::NoModifier);QApplication::sendEvent(v,&frame);break;}}
 void select_group(){auto *list=get<QListWidget>("layerStack");for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toUuid()!=QUuid(group)){list->setCurrentRow(i);break;}for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toUuid()==QUuid(group)){list->setCurrentRow(i);return;}qFatal("Flatten group row missing");}
 void export_maps(bool mask){timer.stop();if(!mask){auto *list=get<QListWidget>("layerStack");const auto layers=state()["document"].toObject()["texture_sets"].toArray()[0].toObject()["layers"].toArray();const QUuid fill(layers[1].toObject()["id"].toString());for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toUuid()==fill)list->item(i)->setSelected(true);}
  QTimer::singleShot(100,this,[this,mask]{auto *d=get<QDialog>("flattenExportDialog");const auto path=dir+(mask?"/mask":"/maps");QDir().mkpath(path);d->findChild<QLineEdit *>("flattenExportDirectory")->setText(path);if(mask)d->findChild<QComboBox *>("flattenMaskFormat")->setCurrentIndex(1);d->findChild<QPushButton *>("runFlattenExport")->click();if(!d->property("flattenExportError").toString().isEmpty()||!d->property("flattenExportFinished").toBool())qFatal("Flatten export failed: %s",qPrintable(d->property("flattenExportError").toString()));exports.append(QJsonObject{{"mask",mask},{"files",QJsonArray::fromStringList(d->property("flattenExportFiles").toStringList())},{"probes",d->property("flattenExportProbes").toJsonArray()}});d->grab().save(dir+(mask?"/mask-export.png":"/batch-export.png"));d->accept();});action(mask?"exportFlattenedMask":"exportFlattenedGroups");select_group();timer.start();}
 void advance(){if(++ticks>1000)qFatal("Flatten demo timeout");auto s=state();if(phase>1&&(s["property_pending"].toBool()||s["composite_pending"].toBool()||s["render_frames"].toInt()<frame))return;
 switch(phase++){
 case 0:action("newProject");break;
 case 1:{auto *d=get<QDialog>("newProjectDialog");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *r=d->findChild<QComboBox *>("documentResolution");r->setCurrentIndex(r->findData(size));if(qEnvironmentVariableIsSet("BTS_FLATTEN_UDIM"))d->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);d->findChild<QPushButton *>("createProject")->click();action("guiSkewPrepare");if(qEnvironmentVariableIsSet("BTS_FLATTEN_RECT")){get<QToolButton>("textureSetSizeLock")->setChecked(false);auto *height=get<QComboBox>("textureSetHeight");height->setCurrentIndex(height->findData(1024));}break;}
 case 2:capture("empty-before");action("flattenLayer");break;
 case 3:capture("empty-after");get<QToolButton>("addFillLayer")->click();get<QDoubleSpinBox>("roughnessControl")->setValue(.23);get<QDoubleSpinBox>("metallicControl")->setValue(.35);if(qEnvironmentVariableIsSet("BTS_FLATTEN_LEGACY_NORMAL")||qEnvironmentVariableIsSet("BTS_FLATTEN_LARGE"))action("guiFlattenSetup");break;
 case 4:get<QToolButton>("addPaintLayer")->click();get<QDoubleSpinBox>("materialBump")->setValue(qEnvironmentVariableIsSet("BTS_FLATTEN_LARGE")?-.25:.2);action("guiFlattenPaintSetup");paint();break;
 case 5:{auto *list=get<QListWidget>("layerStack");list->clearSelection();int count=0;for(int i=0;i<list->count()&&count<2;++i)if(list->item(i)->data(Qt::UserRole+10).toString().isEmpty()){list->item(i)->setSelected(true);++count;}action("groupLayers");group=state()["document"].toObject()["texture_sets"].toArray()[0].toObject()["selected_layer"].toString();action("guiFlattenGroupSetup");action("addWhiteMask");get<QDoubleSpinBox>("maskGrayscale")->setValue(.3);paint();break;}
 case 6:select_group();frame_views();break;
 case 7:capture("before");if(!qEnvironmentVariableIsSet("BTS_FLATTEN_SKIP_EXPORT"))export_maps(false);break;
 case 8:if(!qEnvironmentVariableIsSet("BTS_FLATTEN_SKIP_EXPORT"))export_maps(true);break;
 case 9:capture("after-exports");action("flattenLayer");break;
 case 10:if(!qEnvironmentVariableIsSet("BTS_FLATTEN_LARGE"))action("checkGuiComposite");{auto *categories=get<QListWidget>("shelfCategories");const auto matches=categories->findItems("Flattened layers",Qt::MatchExactly);if(matches.empty())qFatal("Flattened category missing");categories->setCurrentItem(matches.front());}capture("flattened");action("undoDocument");break;
 case 11:capture("undo");action("redoDocument");break;
 case 12:capture("redo");action("guiSkewSave");break;
 case 13:capture("saved");action("guiSkewOpen");frame_views();break;
 case 14:if(!qEnvironmentVariableIsSet("BTS_FLATTEN_LARGE"))action("checkGuiComposite");capture("opened");timer.stop();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;
 }frame=state()["render_frames"].toInt()+12;
 }
 public:Demo(QWidget *window,QWidget *viewport,QString directory,QString fixture,int resolution,std::function<QJsonObject()> snapshot):QObject(window),w(window),v(viewport),dir(directory),mesh(fixture),size(resolution),state([snapshot=std::move(snapshot)]{EditStateScope metadata;return snapshot();}){QDir().mkpath(dir);clock.start();ping.setInterval(10);connect(&ping,&QTimer::timeout,this,[this]{const auto now=clock.elapsed();const auto *modal=QApplication::activeModalWidget();const bool flattening=modal&&modal->objectName()=="flattenProgressDialog";if(flattening&&was_flattening)max_ping=std::max(max_ping,now-last);was_flattening=flattening;last=now;});ping.start();timer.setInterval(150);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_flatten_demo(QWidget *w,QWidget *v,const QString &dir,const QString &mesh,int size,std::function<QJsonObject()> state){new Demo(w,v,dir,mesh,size,std::move(state));}
}
