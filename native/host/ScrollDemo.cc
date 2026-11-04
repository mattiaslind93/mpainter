#include "ScrollDemo.hh"
#include <QAbstractScrollArea>
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
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
namespace bts {namespace {
class ScrollDemo final:public QObject {
 QWidget *w;QString dir,mesh;int resolution,stage=0;QTimer timer;QJsonArray checks;std::function<QJsonObject()> state;
 void check(const char *name,bool passed,int before,int after){checks.append(QJsonObject{{"test",name},{"passed",passed},{"before",before},{"after",after}});if(!passed)qFatal("Panel scroll check failed: %s (%d -> %d)",name,before,after);}
 void wheel(QWidget *widget,QAbstractScrollArea *area,const char *name){
   if(auto *scroll=qobject_cast<QScrollArea *>(area))scroll->ensureWidgetVisible(widget);
   const int before=area?area->verticalScrollBar()->value():0;
   const int direction=before>0?120:-120;const QPointF p=widget->rect().center();
   QWheelEvent event(p,widget->mapToGlobal(p),{},{0,direction},Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
   widget->setFocus();QCoreApplication::sendEvent(widget,&event);
   const int after=area?area->verticalScrollBar()->value():0;
   check(name,!area||before!=after,before,after);
 }
 void pan(QAbstractScrollArea *area,const char *name){
   auto *bar=area->verticalScrollBar();if(bar->maximum()<=bar->minimum())qFatal("Native pan range missing: %s",name);
   bar->setValue(bar->minimum());const int before=bar->value();const QPointF point=area->viewport()->rect().center();
   for(auto type:{QEvent::MouseButtonPress,QEvent::MouseMove,QEvent::MouseButtonRelease}){const auto p=point+(type==QEvent::MouseButtonPress?QPointF{}:QPointF(0,-60));QMouseEvent e(type,p,area->viewport()->mapToGlobal(p),type==QEvent::MouseMove?Qt::NoButton:Qt::MiddleButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::MiddleButton,Qt::NoModifier);QCoreApplication::sendEvent(area->viewport(),&e);}
   check(name,bar->value()>before&&QWidget::mouseGrabber()!=area->viewport(),before,bar->value());
 }
 void advance(){if(stage>2&&state()["composite_pending"].toBool())return;switch(stage++){
 case 0:w->findChild<QAction *>("newProject")->trigger();break;
 case 1:{auto *d=w->findChild<QDialog *>("newProjectDialog");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=d->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));d->findChild<QPushButton *>("createProject")->click();break;}
 case 2:w->resize(1000,680);w->findChild<QToolButton *>("addFillLayer")->click();break;
 case 3:{auto *slider=w->findChild<QSlider *>("roughnessSlider");const int value=slider->value(),undo=state()["undo_count"].toInt();auto *area=w->findChild<QScrollArea *>("propertiesScroll");wheel(slider,area,"roughness-wheel-scrolls-properties");check("roughness-value-preserved",slider->value()==value,value,slider->value());pan(area,"middle-pan-properties");auto *size=w->findChild<QDoubleSpinBox *>("brushSize");const double diameter=size->value();wheel(size,nullptr,"toolbar-wheel-consumed");check("toolbar-value-preserved",size->value()==diameter,int(diameter),int(size->value()));check("scroll-has-no-undo",state()["undo_count"].toInt()==undo,undo,state()["undo_count"].toInt());w->grab().save(dir+"/properties.png");w->findChild<QTabWidget *>("layerSettingsTabs")->setCurrentIndex(1);w->findChild<QCheckBox *>("displacementChannel")->setChecked(true);break;}
 case 4:{auto *scale=w->findChild<QDoubleSpinBox *>("displacementStrength");auto *area=w->findChild<QScrollArea *>("textureSetSettingsScroll");const double value=scale->value();const int undo=state()["undo_count"].toInt();wheel(scale,area,"scale-wheel-scrolls-settings");check("scale-value-preserved",scale->value()==value,int(value),int(scale->value()));pan(area,"middle-pan-settings");check("settings-scroll-has-no-undo",state()["undo_count"].toInt()==undo,undo,state()["undo_count"].toInt());w->grab().save(dir+"/settings.png");w->findChild<QTabWidget *>("layerSettingsTabs")->setCurrentIndex(0);for(int i=0;i<20;++i)w->findChild<QToolButton *>("addPaintLayer")->click();break;}
 case 5:{auto *layers=w->findChild<QListWidget *>("layerStack");const int selected=layers->currentRow();pan(layers,"middle-pan-layers");check("layer-selection-preserved",layers->currentRow()==selected,selected,layers->currentRow());auto *shelf=w->findChild<QListWidget *>("shelfAssets");if(shelf->verticalScrollBar()->maximum()>0)pan(shelf,"middle-pan-shelf");w->grab().save(dir+"/layers.png");timer.stop();QFile f(dir+"/panel-scroll.json");if(!f.open(QIODevice::WriteOnly))qFatal("Scroll proof write failed");f.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"checks",checks},{"state",state()}}).toJson());f.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
 }}
 public:ScrollDemo(QWidget *workspace,QString d,QString m,int r,std::function<QJsonObject()> s):QObject(workspace),w(workspace),dir(d),mesh(m),resolution(r),state(std::move(s)){QDir().mkpath(dir);timer.setInterval(900);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_scroll_demo(QWidget *w,QWidget *,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new ScrollDemo(w,d,m,r,std::move(state));}
}
