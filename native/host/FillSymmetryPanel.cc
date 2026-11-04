#include "NumericLocale.hh"
#include "FillSymmetryPanel.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>
namespace bts {
FillSymmetryPanel::FillSymmetryPanel(QWidget *parent):QWidget(parent)
{
 setObjectName("fillSymmetryPanel");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,6,0,6);
 layout->addWidget(new QLabel("SYMMETRY",this));enabled_=new QCheckBox("Enable symmetry",this);enabled_->setObjectName("fillSymmetryEnabled");layout->addWidget(enabled_);
 options_=new QWidget(this);layout->addWidget(options_);auto *form=new QFormLayout(options_);
 mode_=new QComboBox(options_);mode_->setObjectName("fillSymmetryMode");mode_->addItems({"Mirror","Radial"});form->addRow("Mode",mode_);
 axis_=new QComboBox(options_);axis_->setObjectName("fillSymmetryAxis");axis_->addItems({"X","Y","Z"});form->addRow("Axis",axis_);
 const auto field=[&](const QString &name){auto *value=new bts::NumericSpinBox(options_);value->setObjectName(name);value->setLocale(QLocale::c());value->setRange(-1e6,1e6);value->setDecimals(4);value->setSingleStep(.01);value->setKeyboardTracking(false);connect(value,&QDoubleSpinBox::valueChanged,this,[this]{publish();});return value;};
 offset_=field("fillSymmetryOffset");form->addRow("Axis position (scene units)",offset_);
 for(int i=0;i<3;++i){center_[i]=field(QString("fillSymmetryCenter%1").arg(i));form->addRow(QString("Center %1").arg(QStringList{"X","Y","Z"}[i]),center_[i]);}
 copies_=new QSpinBox(options_);copies_->setObjectName("fillSymmetryCopies");copies_->setRange(2,32);form->addRow("Radial copies",copies_);
 flip_u_=new QCheckBox("Flip input U on alternating copies",options_);flip_u_->setObjectName("fillSymmetryFlipU");form->addRow(flip_u_);
 flip_v_=new QCheckBox("Flip input V on alternating copies",options_);flip_v_->setObjectName("fillSymmetryFlipV");form->addRow(flip_v_);
 connect(enabled_,&QCheckBox::toggled,this,[this]{publish();});connect(mode_,&QComboBox::currentIndexChanged,this,[this]{publish();});connect(axis_,&QComboBox::currentIndexChanged,this,[this]{publish();});connect(copies_,&QSpinBox::valueChanged,this,[this]{publish();});connect(flip_u_,&QCheckBox::toggled,this,[this]{publish();});connect(flip_v_,&QCheckBox::toggled,this,[this]{publish();});synchronize({},false);
}
void FillSymmetryPanel::synchronize(const std::shared_ptr<const FillSymmetry> &value,bool supported)
{
 state_=value;const auto options=value?*value:FillSymmetry{};const auto &s=options.constraint;
 const QSignalBlocker a(enabled_),b(mode_),c(axis_),d(copies_),e(offset_),f(flip_u_),g(flip_v_);enabled_->setChecked(s.enabled);mode_->setCurrentIndex(int(s.mode));axis_->setCurrentIndex(s.axis);copies_->setValue(s.copies);offset_->setValue(s.offset);flip_u_->setChecked(options.flip_u);flip_v_->setChecked(options.flip_v);
 for(int i=0;i<3;++i){const QSignalBlocker block(center_[i]);center_[i]->setValue(s.center[i]);center_[i]->setEnabled(s.mode==SymmetryMode::Radial);}
 offset_->setEnabled(s.mode==SymmetryMode::Mirror);copies_->setEnabled(s.mode==SymmetryMode::Radial);options_->setVisible(s.enabled);setVisible(supported);
}
void FillSymmetryPanel::publish()
{
 auto value=std::make_shared<FillSymmetry>(state_?*state_:FillSymmetry{});auto &s=value->constraint;s.enabled=enabled_->isChecked();s.mode=SymmetryMode(mode_->currentIndex());s.axis=axis_->currentIndex();s.copies=copies_->value();s.offset=offset_->value();for(int i=0;i<3;++i)s.center[i]=center_[i]->value();value->flip_u=flip_u_->isChecked();value->flip_v=flip_v_->isChecked();value->validate();synchronize(value,true);if(changed)changed(value);
}
}
