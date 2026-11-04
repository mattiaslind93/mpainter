#include "PerformanceDemo.hh"
#include "Performance.hh"
#include "GpuPerformance.hh"
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <cmath>
namespace bts {namespace {
class Demo final:public QObject {
 QWidget *w,*v;QString dir,mesh;int resolution,stage=0,index=0,case_index=0;QTimer timer,pulse,ping;QElapsedTimer clock,heartbeat,settle;double max_ping=0,call_ms=0,process_cpu=0;QJsonObject transfers,before;QJsonArray cases;std::function<QJsonObject()> state;bool finished=true;QSlider *slider=nullptr;QWidget *color_wheel=nullptr;QJsonArray gpu_frames;QPointF point;const char *labels[10]={"import","stroke","fill","property_drag","layer_select","displacement","channel_switch","orbit_zoom","undo","export"};
 template<class T>T *get(const char *name){auto *value=w->findChild<T *>(name);if(!value)qFatal("Performance control missing: %s",name);return value;}
 void action(const char *name){get<QAction>(name)->trigger();}
 void mouse(QEvent::Type type,QPointF p,Qt::MouseButton button=Qt::LeftButton,Qt::KeyboardModifiers modifiers=Qt::NoModifier){QMouseEvent event(type,p,v->mapToGlobal(p.toPoint()),type==QEvent::MouseMove?Qt::NoButton:button,type==QEvent::MouseButtonRelease?Qt::NoButton:button,modifiers);QCoreApplication::sendEvent(v,&event);}
 void color_mouse(QEvent::Type type,QPointF p){QMouseEvent event(type,p,color_wheel->mapToGlobal(p.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(color_wheel,&event);}
 void begin(){before=state();index=0;finished=false;max_ping=0;call_ms=0;transfers=gpu_transfer_snapshot();performance::begin(labels[case_index]);process_cpu=1000.*std::clock()/CLOCKS_PER_SEC;heartbeat.start();clock.start();ping.start();}
 void number(const char *name,double value){auto *n=get<QDoubleSpinBox>(name);n->setValue(value);QMetaObject::invokeMethod(n,"editingFinished",Qt::DirectConnection);}
 void export_maps(){timer.stop();auto *poll=new QTimer(this);connect(poll,&QTimer::timeout,this,[this,poll]{auto *dialog=w->findChild<QDialog *>("textureExportDialog");if(!dialog)return;if(!dialog->property("profileStarted").toBool()){dialog->setProperty("profileStarted",true);dialog->findChild<QLineEdit *>("exportDirectory")->setText(dir+"/maps");dialog->findChild<QPushButton *>("runTextureExport")->click();}if(dialog->property("exportFinished").toBool()){if(!dialog->property("exportError").toString().isEmpty())qFatal("Profile export error");poll->stop();dialog->accept();poll->deleteLater();}});poll->start(20);action("exportTextures");timer.start();}
 void start_case(){begin();const auto start=performance::Clock::now();{
 performance::Scope scope("action");switch(case_index){
 case 0:{action("newProject");auto *d=get<QDialog>("newProjectDialog");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *r=d->findChild<QComboBox *>("documentResolution");r->setCurrentIndex(r->findData(resolution));d->findChild<QPushButton *>("createProject")->click();break;}
 case 1:point=QPointF(v->width()*.43,v->height()*.48);mouse(QEvent::MouseButtonPress,point);pulse.start();break;
 case 2:get<QToolButton>("addFillLayer")->click();break;
 case 3:if(std::getenv("BTS_GPU_UNIFORM_DEMO")){QKeyEvent inspect(QEvent::KeyPress,Qt::Key_C,Qt::NoModifier);QCoreApplication::sendEvent(v,&inspect);get<QWidget>("materialColorPicker")->findChild<QPushButton *>("colorSwatch")->click();color_wheel=get<QDialog>("colorPickerDialog")->findChild<QWidget *>("colorWheel");color_mouse(QEvent::MouseButtonPress,{65,100});}else{slider=get<QSlider>("roughnessSlider");slider->setSliderDown(true);}pulse.start();break;
 case 4:pulse.start();break;
 case 5:get<QToolButton>("shaderSettingsButton")->click();get<QCheckBox>("displacementChannel")->setChecked(true);get<QToolButton>("brushDisplacement")->setChecked(true);number("materialDisplacement",.1);pulse.start();break;
 case 6:pulse.start();break;
 case 7:mouse(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::AltModifier);pulse.start();break;
 case 8:action("undoDocument");break;
 case 9:export_maps();break;
 }}call_ms=performance::ms(start);if(!pulse.isActive()){finished=true;settle.start();}}
 void tick(){performance::Scope scope("input");++index;switch(case_index){
 case 1:{const auto p=point+QPointF(30*std::sin(index*.13),25*(1-std::cos(index*.13)));mouse(index==60?QEvent::MouseButtonRelease:QEvent::MouseMove,p);break;}
 case 3:if(color_wheel){color_mouse(index==100?QEvent::MouseButtonRelease:QEvent::MouseMove,{65+index*.9,100-index*.35});if(index==20||index==80){const auto name=QString("gpu-color-%1.png").arg(index);capture_viewport_framebuffer(v).save(dir+"/"+name);gpu_frames.append(QJsonObject{{"index",index},{"file",name},{"state",state()}});}if(index==100)get<QDialog>("colorPickerDialog")->hide();}else{slider->setValue(200+index*6);if(index==100)slider->setSliderDown(false);}break;
 case 4:get<QListWidget>("layerStack")->setCurrentRow(index%2);break;
 case 5:number("displacementStrength",.5+.01*index);break;
 case 6:{QKeyEvent event(QEvent::KeyPress,Qt::Key_C,Qt::NoModifier);QCoreApplication::sendEvent(v,&event);break;}
 case 7:{mouse(index==60?QEvent::MouseButtonRelease:QEvent::MouseMove,point+QPointF(index*2,10*std::sin(index*.1)),Qt::LeftButton,Qt::AltModifier);if(index%10==0){QWheelEvent e(point,v->mapToGlobal(point.toPoint()),{},QPoint(0,index%20?120:-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(v,&e);}break;}
 }const int count=case_index==3?100:(case_index==4||case_index==5||case_index==6)?20:60;if(index>=count){pulse.stop();finished=true;settle.start();if(case_index==5)get<QDialog>("shaderSettings")->hide();}}
 void complete(){ping.stop();const auto duration=clock.nsecsElapsed()/1e6;const auto cpu=1000.*std::clock()/CLOCKS_PER_SEC-process_cpu;performance::begin("");auto data=gpu_transfer_snapshot();for(auto it=data.begin();it!=data.end();++it){auto now=it.value().toObject(),old=transfers[it.key()].toObject();for(auto n=now.begin();n!=now.end();++n)n.value()=n.value().toDouble()-old[n.key()].toDouble();it.value()=now;}auto after=state();cases.append(QJsonObject{{"flow",labels[case_index]},{"wall_ms",duration},{"process_cpu_ms",cpu},{"action_return_ms",call_ms},{"max_ping_ms",max_ping},{"settled_after_release_ms",settle.elapsed()},{"transfers",data},{"before",before},{"after",after}});std::printf("BTS_PROFILE flow=%s wall_ms=%.3f cpu_ms=%.3f ping_ms=%.3f\n",labels[case_index],duration,cpu,max_ping);std::fflush(stdout);++case_index;stage=2;}
 void advance(){if(stage==0){stage=1;start_case();return;}if(stage==1){const auto s=state();if(!finished||s["composite_pending"].toBool()||s["property_pending"].toBool()||settle.elapsed()<150)return;complete();return;}if(stage==2){if(case_index==1){get<QToolButton>("addPaintLayer")->click();stage=3;return;}if(case_index==10){finish();return;}stage=1;start_case();return;}if(stage==3){if(state()["composite_pending"].toBool())return;stage=1;start_case();}}
 void finish(){timer.stop();action("checkGuiComposite");auto final=state();auto *gl=qobject_cast<QOpenGLWidget *>(v);capture_viewport_framebuffer(gl).save(dir+"/final.png");QFile out(dir+"/performance.json");if(!out.open(QIODevice::WriteOnly))qFatal("Performance output unavailable");out.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"resolution",resolution},{"cases",cases},{"gpu_frames",gpu_frames},{"stages",performance::snapshot()},{"final",final}}).toJson());out.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);}
 public:Demo(QWidget *workspace,QWidget *viewport,QString d,QString m,int r,std::function<QJsonObject()> s):QObject(workspace),w(workspace),v(viewport),dir(d),mesh(m),resolution(r),state(std::move(s)){QDir().mkpath(dir+"/maps");timer.setInterval(20);connect(&timer,&QTimer::timeout,this,[this]{advance();});pulse.setInterval(20);pulse.setTimerType(Qt::PreciseTimer);connect(&pulse,&QTimer::timeout,this,[this]{tick();});ping.setInterval(10);ping.setTimerType(Qt::PreciseTimer);connect(&ping,&QTimer::timeout,this,[this]{max_ping=std::max(max_ping,heartbeat.nsecsElapsed()/1e6);heartbeat.restart();});QTimer::singleShot(1000,this,[this]{timer.start();});}
};}
void start_performance_demo(QWidget *w,QWidget *v,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new Demo(w,v,d,m,r,std::move(state));}
}
