#include "NumericLocale.hh"
#include "DisplacementPanel.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
namespace bts {
DisplacementPanel::DisplacementPanel(QWidget *parent):QWidget(parent){
  setObjectName("displacementPanel");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,6,0,6);
  channel_=new QCheckBox("Displacement channel",this);channel_->setObjectName("displacementChannel");layout->addWidget(channel_);
  enabled_=new QCheckBox("Displacement & Tessellation — Enable",this);enabled_->setObjectName("displacementEnabled");layout->addWidget(enabled_);
  fields_=new QWidget(this);auto *form=new QFormLayout(fields_);form->setContentsMargins(0,0,0,0);
  auto *source=new QComboBox(fields_);source->setObjectName("displacementSource");source->addItem("Displacement");source->setToolTip("Bump changes shading only; geometry uses the independent Displacement channel.");form->addRow("Source Channel",source);
  const auto spin=[&](const char *name,double low,double high,int decimals,double step){auto *control=new bts::NumericSpinBox(fields_);control->setObjectName(name);control->setLocale(QLocale::c());control->setRange(low,high);control->setDecimals(decimals);control->setSingleStep(step);control->setKeyboardTracking(false);connect(control,&QDoubleSpinBox::valueChanged,this,[this]{publish();});return control;};
  strength_=spin("displacementStrength",0,1e6,5,.01);form->addRow("Scale (scene units)",strength_);
  midlevel_=spin("displacementMidlevel",0,1,4,.5);midlevel_->setToolTip("Neutral height: 0 for signed/black-neutral maps (default), 0.5 for middle-gray maps. Explicit legacy project values are preserved.");form->addRow("Neutral / Midlevel",midlevel_);
  mode_=new QComboBox(fields_);mode_->setObjectName("displacementSubdivisionMode");mode_->addItem("Uniform",0);mode_->addItem("Catmull-Clark (OpenSubdiv)",2);mode_->addItem("Edge Length",1);auto *item=static_cast<QStandardItemModel *>(mode_->model())->item(2);item->setToolTip("Object-space approximation: choose a uniform linear subdivision level from the longest original edge / Max Length. EEVEE has no adaptive tessellation. Allocation is checked against available RAM and VRAM.");form->addRow("Subdivision Mode",mode_);connect(mode_,&QComboBox::currentIndexChanged,this,[this]{publish();});
  subdivision_=new QSpinBox(fields_);subdivision_->setObjectName("displacementSubdivisionCount");subdivision_->setRange(0,11);subdivision_->setKeyboardTracking(false);subdivision_->setToolTip("Preview and optional displaced mesh export. Allocation is checked against available RAM and VRAM.");form->addRow("Subdivision Count",subdivision_);connect(subdivision_,&QSpinBox::valueChanged,this,[this]{publish();});
  max_length_=spin("displacementMaxLength",.000001,1e6,6,.001);max_length_->setEnabled(false);max_length_->setToolTip("Target edge length in object-space scene units. The longest original edge determines a uniform level; the native modifier level range and available RAM/VRAM determine feasibility.");form->addRow("Max Length",max_length_);
  auto *hint=new QLabel("Displacement needs a dense mesh. Preview and optional displaced mesh export use this subdivision; UVs interpolate linearly without smoothing. Original geometry, picking, baking and base mesh export are retained.",fields_);hint->setWordWrap(true);form->addRow(hint);layout->addWidget(fields_);
  auto *restore=new QPushButton("Restore defaults",this);restore->setObjectName("displacementRestoreDefaults");layout->addWidget(restore);connect(restore,&QPushButton::clicked,this,[this]{auto defaults=DisplacementSettings{};defaults.channel=state_.channel;defaults.allocated=state_.allocated;defaults.precision=state_.precision;if(changed)changed(defaults);});
  connect(channel_,&QCheckBox::toggled,this,[this](bool on){if(!syncing_&&channel_changed)channel_changed(on);});
  connect(enabled_,&QCheckBox::toggled,this,[this]{publish();});synchronize({},false);
}
void DisplacementPanel::synchronize(const DisplacementSettings &state,bool editable,std::optional<bool> layer_channel){syncing_=true;state_=state;setEnabled(editable);channel_->setText(layer_channel?"Displacement in active layer":"Displacement channel");channel_->setChecked(layer_channel.value_or(state.channel));enabled_->setChecked(state.enabled);enabled_->setEnabled(editable&&state.channel);fields_->setEnabled(editable&&state.channel&&state.enabled);strength_->setValue(state.strength);midlevel_->setValue(state.midlevel);subdivision_->setValue(state.subdivision);mode_->setCurrentIndex(mode_->findData(state.mode));subdivision_->setEnabled(state.mode!=1);max_length_->setValue(state.max_length);max_length_->setEnabled(state.mode==1);syncing_=false;}
void DisplacementPanel::publish(){if(syncing_)return;auto next=state_;next.enabled=enabled_->isChecked();next.strength=float(strength_->value());next.midlevel=float(midlevel_->value());next.subdivision=subdivision_->value();next.mode=mode_->currentData().toInt();next.max_length=max_length_->value();next.validate();if(next!=state_&&changed)changed(next);}
}
