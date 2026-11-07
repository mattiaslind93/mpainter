#include "NumericLocale.hh"
#pragma once
#include "ProjectionMaterial.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
namespace bts {
class ProjectionPanel final:public QWidget {
 public:
  explicit ProjectionPanel(QWidget *parent):QWidget(parent){
    setObjectName("projectionProperties");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);/* The heading belongs to the PropertySection around this panel. */auto *form=new QFormLayout;layout->addLayout(form);
    tiling_=new QComboBox(this);tiling_->setObjectName("projectionTiling");tiling_->addItems({"No Tiling","Horizontal Tiling","Vertical Tiling","H and V Tiling"});form->addRow("Tiling mode",tiling_);connect(tiling_,&QComboBox::currentIndexChanged,this,[this](int value){source_.transform.tiling=value;publish();});
    resource_=new QToolButton(this);resource_->setObjectName("projectionResource");resource_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);resource_->setPopupMode(QToolButton::InstantPopup);auto *menu=new QMenu(resource_);resource_->setMenu(menu);layout->addWidget(resource_);auto *uniform=menu->addAction("uniform color");uniform->setObjectName("projectionUniform");connect(uniform,&QAction::triggered,this,[this]{if(resource_selected)resource_selected(false);});auto *weave=menu->addAction("Blue Weave");weave->setObjectName("projectionBlueWeave");connect(weave,&QAction::triggered,this,[this]{if(resource_selected)resource_selected(true);});
    auto *parameters=new QFormLayout;layout->addLayout(parameters);
    for(int i=0;i<2;++i){offset_[i]=new bts::NumericSpinBox(this);offset_[i]->setObjectName(i?"projectionOffsetY":"projectionOffsetX");offset_[i]->setRange(-10,10);offset_[i]->setDecimals(3);offset_[i]->setKeyboardTracking(false);parameters->addRow(i?"Offset Y":"Offset X",offset_[i]);connect(offset_[i],&QDoubleSpinBox::valueChanged,this,[this,i](double value){source_.transform.offset[i]=value;publish();});}
    scale_=new bts::NumericSpinBox(this);scale_->setObjectName("projectionScale");scale_->setRange(.01,100);scale_->setDecimals(3);scale_->setSingleStep(.05);scale_->setKeyboardTracking(false);parameters->addRow("Scale",scale_);connect(scale_,&QDoubleSpinBox::valueChanged,this,[this](double value){source_.transform.scale=value;publish();});
    angle_=new bts::NumericSpinBox(this);angle_->setObjectName("projectionAngle");angle_->setRange(-180,180);angle_->setDecimals(1);angle_->setKeyboardTracking(false);parameters->addRow("Rotation",angle_);connect(angle_,&QDoubleSpinBox::valueChanged,this,[this](double value){source_.transform.angle=value;publish();});
    density_=new bts::NumericSpinBox(this);density_->setObjectName("projectionWeaveDensity");density_->setRange(4,512);density_->setDecimals(1);density_->setKeyboardTracking(false);density_label_=new QLabel("Weave density",this);parameters->addRow(density_label_,density_);connect(density_,&QDoubleSpinBox::valueChanged,this,[this](double value){source_.density=value;publish();});
    auto *reset=new QToolButton(this);reset->setObjectName("projectionReset");reset->setText("Reset transformation");parameters->addRow(reset);connect(reset,&QToolButton::clicked,this,[this]{source_.transform.offset={};source_.transform.scale=1;source_.transform.angle=0;publish();});
  }
  std::function<void(const ProjectionMaterial &)> changed;
  std::function<void(bool)> resource_selected;
  void synchronize(const ProjectionMaterial *source){
    setVisible(source);if(!source)return;source_=*source;const QSignalBlocker a(tiling_),b(scale_),c(angle_),d(density_),e(offset_[0]),f(offset_[1]);tiling_->setCurrentIndex(source->transform.tiling);scale_->setValue(source->transform.scale);angle_->setValue(source->transform.angle);density_->setValue(source->density);density_->setVisible(source->weave);density_label_->setVisible(source->weave);for(int i=0;i<2;++i)offset_[i]->setValue(source->transform.offset[i]);resource_->setText(QString("Material\n%1").arg(QString::fromStdString(source->name)));
  }
 private:
  ProjectionMaterial source_;QComboBox *tiling_;QToolButton *resource_;QDoubleSpinBox *scale_,*angle_,*density_;QLabel *density_label_;std::array<QDoubleSpinBox *,2> offset_;
  void publish(){source_.validate();if(changed)changed(source_);}
};
}
