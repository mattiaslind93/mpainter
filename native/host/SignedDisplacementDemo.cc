#include "ViewportDemoCapture.hh"
#include "SignedDisplacementDemo.hh"
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
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {namespace {
class SignedDemo final:public QObject {
 QWidget *w,*v;QString dir,mesh;int resolution,stage=0;QTimer timer;QJsonArray captures;std::function<QJsonObject()> state;
 void action(const char *name){auto *a=w->findChild<QAction *>(name);if(!a)qFatal("Signed action missing: %s",name);a->trigger();}
 void number(const char *name,double value){auto *c=w->findChild<QDoubleSpinBox *>(name);if(!c)qFatal("Signed number missing: %s",name);c->setValue(value);}
 void capture(const char *name){auto data=state();data["stage"]=name;auto *value=w->findChild<QDoubleSpinBox *>("materialDisplacement");data["brush_min"]=value->minimum();data["brush_max"]=value->maximum();data["brush_value"]=value->value();data["point_decimal"]=value->locale().decimalPoint()==".";captures.append(data);capture_viewport_framebuffer(v).save(dir+"/"+name+".png");}
 void stroke(){auto point=state()["stroke_point"].toArray();if(point.size()!=2)qFatal("Signed original-surface probe missing");QPointF p(point[0].toDouble(),point[1].toDouble());for(auto type:{QEvent::MouseButtonPress,QEvent::MouseMove,QEvent::MouseButtonRelease}){QMouseEvent event(type,p,v->mapToGlobal(p.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(v,&event);p+=QPointF(3,0);}}
 void advance(){if(state()["composite_pending"].toBool())return;switch(stage++){
 case 0:for(auto *a:w->findChildren<QAction *>())if(a->text()=="&New…"){a->trigger();return;}qFatal("Signed New missing");
 case 1:{QDialog *d=nullptr;for(auto *candidate:w->findChildren<QDialog *>())if(candidate->findChild<QLineEdit *>("meshPath"))d=candidate;if(!d)qFatal("Signed New dialog missing");d->findChild<QLineEdit *>("meshPath")->setText(mesh);auto *size=d->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution));d->findChild<QPushButton *>("createProject")->click();break;}
 case 2:capture("created");w->findChild<QToolButton *>("addPaintLayer")->click();w->findChild<QToolButton *>("shaderSettingsButton")->click();w->findChild<QCheckBox *>("displacementChannel")->setChecked(true);number("displacementMidlevel",0);for(int c=0;c<5;++c)w->findChild<QToolButton *>(QString("brushChannel%1").arg(c))->setChecked(false);number("brushSize",25);number("materialDisplacement",-.25);break;
 case 3:capture("negative-empty");stroke();break;
 case 4:capture("negative");number("displacementStrength",0);break;
 case 5:capture("negative-zero");number("displacementStrength",1);w->findChild<QToolButton *>("addPaintLayer")->click();number("materialDisplacement",.25);break;
 case 6:capture("positive-empty");stroke();break;
 case 7:capture("positive");number("displacementStrength",0);break;
 case 8:capture("positive-zero");number("displacementStrength",1);action("guiDisplacementSave");break;
 case 9:capture("saved");action("guiDisplacementOpen");break;
 case 10:capture("opened");number("displacementStrength",0);action("guiDisplacementSave");number("displacementStrength",1);action("guiDisplacementOpen");break;
 case 11:capture("explicit-zero-opened");w->findChild<QPushButton *>("displacementRestoreDefaults")->click();break;
 case 12:{capture("restored-defaults");timer.stop();QFile file(dir+"/signed-displacement.json");if(!file.open(QIODevice::WriteOnly))qFatal("Signed proof unavailable");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures}}).toJson());file.close();QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
 }}
 public:SignedDemo(QWidget *workspace,QWidget *viewport,QString d,QString m,int r,std::function<QJsonObject()> s):QObject(workspace),w(workspace),v(viewport),dir(d),mesh(m),resolution(r),state(std::move(s)){QDir().mkpath(dir);timer.setInterval(900);connect(&timer,&QTimer::timeout,this,[this]{advance();});timer.start();}
};}
void start_signed_displacement_demo(QWidget *w,QWidget *v,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new SignedDemo(w,v,d,m,r,std::move(state));}
}
