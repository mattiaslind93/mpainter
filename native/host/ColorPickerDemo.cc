#include "ColorPickerDemo.hh"
#include "ColorPicker.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QPushButton>
#include <QSettings>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>

namespace bts {
namespace {
class ColorDemo final:public QObject {
  QWidget *workspace_,*viewport_;QString directory_,mesh_;int resolution_,stage_=0,move_=0;
  std::function<QJsonObject()> state_;QTimer timer_,ping_;QElapsedTimer ping_clock_;double max_ping_=0;
  QJsonArray captures_,live_;qint64 before_drag_=0;ColorPicker *picker_=nullptr;QWidget *wheel_=nullptr;
  QDialog *popup(){return workspace_->findChild<QDialog *>("colorPickerDialog");}
  void mouse(QWidget *widget,QEvent::Type type,QPointF point){QMouseEvent event(type,point,widget->mapToGlobal(point.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(widget,&event);}
  void click(QWidget *widget,QPointF point){mouse(widget,QEvent::MouseButtonPress,point);mouse(widget,QEvent::MouseButtonRelease,point);}
  void hex(const char *text){auto *field=popup()->findChild<QLineEdit *>("colorHex");field->setText(text);QMetaObject::invokeMethod(field,"editingFinished",Qt::DirectConnection);}
  void show(){picker_->findChild<QPushButton *>("colorSwatch")->click();wheel_=popup()->findChild<QWidget *>("colorWheel");}
  void capture(const char *name){auto value=state_();value["stage"]=name;value["brush_size"]=workspace_->findChild<QDoubleSpinBox *>("brushSize")->value();QJsonArray picker;for(float v:picker_->color())picker.append(v);value["picker_color"]=picker;captures_.append(value);}
  void advance(){switch(stage_++){
    case 0:{QAction *action=nullptr;for(auto *a:workspace_->findChildren<QAction *>())if(a->text()=="&New…")action=a;if(!action)qFatal("Color GUI New missing");action->trigger();break;}
    case 1:{QDialog *dialog=nullptr;for(auto *d:workspace_->findChildren<QDialog *>())if(d->findChild<QLineEdit *>("meshPath"))dialog=d;if(!dialog)qFatal("Color New dialog missing");dialog->findChild<QLineEdit *>("meshPath")->setText(mesh_);auto *size=dialog->findChild<QComboBox *>("documentResolution");size->setCurrentIndex(size->findData(resolution_));dialog->findChild<QPushButton *>("createProject")->click();break;}
    case 2:{picker_=static_cast<ColorPicker *>(workspace_->findChild<QWidget *>("materialColorPicker"));capture("created");show();click(wheel_,{208,110});click(wheel_,{170,50});capture("wheel-red");hex("#0000FF");hex("#FF0000");capture("hex-red");auto *size=workspace_->findChild<QDoubleSpinBox *>("brushSize");size->setValue(12);workspace_->findChild<QAction *>("undoDocument")->trigger();capture("undo-brush-color");workspace_->findChild<QAction *>("redoDocument")->trigger();capture("redo-brush-color");size->setValue(10);
      // Exercise the same SV interaction through real pressure-bearing tablet events.
      static const QPointingDevice device("Color wheel pen",0x434f4c,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
      for(const auto type:{QEvent::TabletPress,QEvent::TabletMove,QEvent::TabletRelease}){QTabletEvent event(type,&device,{125,80},wheel_->mapToGlobal(QPoint(125,80)),.5,0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,type==QEvent::TabletRelease?Qt::NoButton:Qt::LeftButton);QCoreApplication::sendEvent(wheel_,&event);}capture("tablet-square");hex("#FF0000");popup()->hide();workspace_->findChild<QToolButton *>("addFillLayer")->click();capture("fill-white");show();hex("#FF0000");before_drag_=state_()["undo_count"].toInteger();mouse(wheel_,QEvent::MouseButtonPress,{70,80});ping_clock_.start();ping_.start();timer_.setInterval(20);break;}
    case 3:{if(move_<100){viewport_->repaint();live_.append(state_());++move_;mouse(wheel_,QEvent::MouseMove,{70+move_*.8,80-move_*.15});--stage_;return;}mouse(wheel_,QEvent::MouseButtonRelease,{150,65});ping_.stop();capture("fill-drag");popup()->grab().save(directory_+"/picker.png");popup()->hide();workspace_->findChild<QAction *>("undoDocument")->trigger();capture("undo-drag");workspace_->findChild<QToolButton *>("addPaintLayer")->click();const QPointF point(viewport_->width()*.43,viewport_->height()*.48);click(viewport_,point);show();hex("#0000FF");capture("before-pick");popup()->findChild<QPushButton *>("colorEyedropper")->click();click(viewport_,point);timer_.setInterval(20);break;}
    case 4:{if(viewport_->property("gpuMaterialPickPending").toBool()){--stage_;return;}capture("picked-albedo");show();popup()->findChild<QCheckBox *>("colorAdvanced")->setChecked(true);picker_->linear_fields()[0]->setValue(2.5);capture("hdr");popup()->hide();show();capture("hdr-reopened");popup()->hide();timer_.setInterval(100);break;}
    case 5:{if(state_()["composite_pending"].toBool()){--stage_;return;}timer_.stop();QJsonArray recent;QSettings settings("mPainter","ColorPicker");for(const auto &entry:settings.value("recentACESCg").toStringList())recent.append(entry);bool dot=true;for(auto *spin:popup()->findChildren<QDoubleSpinBox *>())dot&=spin->locale().decimalPoint()==".";QFile file(directory_+"/color-picker.json");if(!file.open(QIODevice::WriteOnly))qFatal("Color proof write failed");file.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"captures",captures_},{"recent",recent},{"point_decimal",dot},{"before_drag_undo",before_drag_},{"max_ping_ms",max_ping_},{"drag_moves",move_},{"live_previews",live_}}).toJson());file.close();workspace_->grab().save(directory_+"/workspace.png");QTimer::singleShot(100,qApp,&QCoreApplication::quit);break;}
  }}
 public:
  ColorDemo(QWidget *w,QWidget *v,QString d,QString m,int r,std::function<QJsonObject()> state):QObject(w),workspace_(w),viewport_(v),directory_(d),mesh_(m),resolution_(r),state_(std::move(state)){QDir().mkpath(directory_);timer_.setInterval(1000);connect(&timer_,&QTimer::timeout,this,[this]{advance();});ping_.setTimerType(Qt::PreciseTimer);ping_.setInterval(10);connect(&ping_,&QTimer::timeout,this,[this]{max_ping_=std::max(max_ping_,ping_clock_.nsecsElapsed()/1e6);ping_clock_.restart();});timer_.start();}
};
}
void start_color_picker_demo(QWidget *w,QWidget *v,const QString &d,const QString &m,int r,std::function<QJsonObject()> state){new ColorDemo(w,v,d,m,r,std::move(state));}
}
