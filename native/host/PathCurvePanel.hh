#include "NumericLocale.hh"
#pragma once
#include "SurfacePath.hh"
#include <QCheckBox>
#include <QComboBox>
#include "ColorPicker.hh"
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <functional>
namespace bts {
enum class PathCurveAction {Pressure,Smooth,Custom,Reverse,Closed,DeleteVertex};
struct PathDisplayOptions {double handle=6,width=1;QColor selected{"#edb763"},other{"#8195a5"};bool normals=false,tangents=true,direction=true;};
class PathCurvePanel final:public QFrame {
 public:
  std::function<void(PathCurveAction,double)> changed;
  std::function<void()> ribbon_requested;
  std::function<void(int)> mode_changed;
  std::function<void(const PathDisplayOptions &)> display_changed;
  explicit PathCurvePanel(QWidget *parent):QFrame(parent){setObjectName("pathCurvePanel");setStyleSheet("#pathCurvePanel{background:#292929;border:1px solid #505050;}");auto *layout=new QGridLayout(this);layout->setContentsMargins(5,4,5,4);layout->setSpacing(3);
    vertex_=new QLabel("No vertex selected",this);vertex_->setObjectName("surfaceVertexLabel");layout->addWidget(vertex_,0,0);auto *line=new QWidget(this);auto *pressure_layout=new QHBoxLayout(line);pressure_layout->setContentsMargins(0,0,0,0);pressure_layout->addWidget(new QLabel("Pressure",line));pressure_=new bts::NumericSpinBox(line);pressure_->setObjectName("surfaceVertexPressure");pressure_->setRange(0,100);pressure_->setDecimals(1);pressure_->setKeyboardTracking(false);pressure_layout->addWidget(pressure_);layout->addWidget(line,0,1,1,2);
    const auto button=[&](const char *text,const char *name,int row,int column,bool check,PathCurveAction action){auto *control=new QToolButton(this);control->setText(text);control->setObjectName(name);control->setCheckable(check);control->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);layout->addWidget(control,row,column);connect(control,&QToolButton::clicked,this,[this,control,action](bool checked){if(changed)changed(action,control->isCheckable()?double(checked):0);});return control;};
    smooth_=button("Smooth","surfaceVertexSmooth",1,0,true,PathCurveAction::Smooth);custom_=button("Custom tangents","surfaceCustomTangents",1,1,true,PathCurveAction::Custom);closed_=button("Close path","closeSurfacePath",1,2,true,PathCurveAction::Closed);reverse_=button("Reverse","reverseSurfacePath",2,0,false,PathCurveAction::Reverse);remove_=button("Delete vertex","deleteSurfaceVertex",2,1,false,PathCurveAction::DeleteVertex);
    auto *ribbon=new QPushButton("Ribbon settings…",this);ribbon->setObjectName("surfaceRibbonSettings");layout->addWidget(ribbon,3,2);mode_=new QComboBox(this);mode_->setObjectName("surfacePathMode");mode_->addItems({"Brush path","Ribbon path","Filled path"});layout->addWidget(mode_,3,0,1,2);connect(mode_,&QComboBox::currentIndexChanged,this,[this](int mode){if(mode_changed)mode_changed(mode);});connect(ribbon,&QPushButton::clicked,this,[this]{if(ribbon_requested)ribbon_requested();});
    auto *display=new QToolButton(this);display->setObjectName("surfacePathDisplay");display->setText("Display…");display->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);layout->addWidget(display,2,2);connect(display,&QToolButton::clicked,this,[this]{edit_display();});connect(pressure_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(changed)changed(PathCurveAction::Pressure,value);});hide();
  }
  void synchronize(const SurfacePath *path,int vertex){const bool valid=path&&vertex>=0&&vertex<int(path->points.size());const QSignalBlocker a(pressure_),b(smooth_),c(custom_),d(closed_),e(mode_);if(path)mode_->setCurrentIndex(path->filled?2:path->ribbon.enabled?1:0);vertex_->setText(valid?QString("Vertex %1").arg(vertex+1):"No vertex selected");pressure_->setEnabled(valid);smooth_->setEnabled(valid);custom_->setEnabled(valid);remove_->setEnabled(valid);reverse_->setEnabled(path);closed_->setEnabled(path);closed_->setChecked(path&&path->closed);pressure_->setValue(valid?path->points[vertex].pressure*100:100);smooth_->setChecked(valid&&path->points[vertex].smooth);custom_->setChecked(valid&&path->points[vertex].custom);}
 private:
  QComboBox *mode_;
  QLabel *vertex_;QDoubleSpinBox *pressure_;QToolButton *smooth_,*custom_,*closed_,*reverse_,*remove_;PathDisplayOptions options_;
  void edit_display(){QDialog dialog(this);dialog.setObjectName("pathDisplayDialog");dialog.setWindowTitle("Path display");auto *form=new QFormLayout(&dialog);QDoubleSpinBox handle,width;handle.setObjectName("pathDisplayHandleSize");width.setObjectName("pathDisplayLineWidth");handle.setRange(2,20);width.setRange(.5,4);handle.setValue(options_.handle);width.setValue(options_.width);form->addRow("Handle size",&handle);form->addRow("Path width",&width);QCheckBox normals("Normals"),tangents("Tangents"),direction("Direction");normals.setObjectName("pathDisplayNormals");tangents.setObjectName("pathDisplayTangents");direction.setObjectName("pathDisplayDirection");normals.setChecked(options_.normals);tangents.setChecked(options_.tangents);direction.setChecked(options_.direction);form->addRow(&normals);form->addRow(&tangents);form->addRow(&direction);ColorPicker selected,other;selected.set_color(picker_from_srgb(options_.selected));other.set_color(picker_from_srgb(options_.other));QPushButton done("Done");selected.setObjectName("pathDisplaySelectedColor");other.setObjectName("pathDisplayOtherColor");done.setObjectName("closePathDisplay");form->addRow("Selected path color",&selected);form->addRow("Other paths color",&other);form->addRow(&done);
    const auto update=[&]{options_.handle=handle.value();options_.width=width.value();options_.normals=normals.isChecked();options_.tangents=tangents.isChecked();options_.direction=direction.isChecked();if(display_changed)display_changed(options_);};connect(&handle,&QDoubleSpinBox::valueChanged,&dialog,update);connect(&width,&QDoubleSpinBox::valueChanged,&dialog,update);for(auto *check:{&normals,&tangents,&direction})connect(check,&QCheckBox::toggled,&dialog,update);connect(&done,&QPushButton::clicked,&dialog,&QDialog::accept);selected.changed=[&](const PickerColor &value){options_.selected=picker_to_srgb(value);update();};other.changed=[&](const PickerColor &value){options_.other=picker_to_srgb(value);update();};dialog.exec();}
};
}
