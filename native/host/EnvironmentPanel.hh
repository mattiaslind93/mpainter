#include "NumericLocale.hh"
#include "DisplayPostPanel.hh"
#pragma once
#include <functional>
#include <map>
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QToolButton>
#include <QLocale>
namespace bts {
class EnvironmentPanel final : public QDialog {
 public:
  explicit EnvironmentPanel(QWidget *parent):QDialog(parent,Qt::Tool) {
    setWindowTitle("Display Settings");setObjectName("displaySettings");resize(400,700);
    auto *outer=new QVBoxLayout(this);auto *scroll=new QScrollArea(this);scroll->setObjectName("displaySettingsScroll");scroll->setWidgetResizable(true);auto *content=new QWidget(scroll);scroll->setWidget(content);outer->addWidget(scroll);auto *layout=new QVBoxLayout(content);layout->setContentsMargins(18,18,18,18);layout->setSpacing(14);
    auto *title=new QLabel("Environment Settings",this);auto font=title->font();font.setBold(true);title->setFont(font);layout->addWidget(title);
    layout->addWidget(new QLabel("Environment Map",this));resource_=new QPushButton(this);resource_->setObjectName("environmentResource");layout->addWidget(resource_);
    connect(resource_,&QPushButton::clicked,this,[this]{if(resource_requested)resource_requested();});
    const auto control=[&](const QString &key,const QString &name,const QString &label,double low,double high,const QString &suffix) {
      auto *row=new QHBoxLayout;row->addWidget(new QLabel(label,this));row->addStretch();
      auto *number=new bts::NumericSpinBox(this);number->setObjectName(name);number->setLocale(QLocale::c());number->setAccessibleName(label);number->setRange(low,high);
      number->setDecimals(1);number->setSuffix(suffix);number->setKeyboardTracking(false);row->addWidget(number);layout->addLayout(row);
      auto *slider=new QSlider(Qt::Horizontal,this);slider->setObjectName(name+"Slider");slider->setAccessibleName(label);slider->setRange(qRound(low*10),qRound(high*10));layout->addWidget(slider);
      controls_[key]={number,slider};
      connect(slider,&QSlider::valueChanged,this,[number](int value){number->setValue(value/10.0);});
      connect(number,&QDoubleSpinBox::valueChanged,this,[this,key,slider](double value){
        const QSignalBlocker blocker(slider);slider->setValue(qRound(value*10));if(changed)changed(key,value);
      });
    };
    auto *background=new QLabel("Environment lights the scene and reflections. Background uses the UI theme.",this);
    background->setWordWrap(true);layout->addWidget(background);
    control("exposure","environmentExposure","Environment Exposure",-10,10," EV");
    control("rotation","environmentRotation","Environment Rotation",-180,180,"°");
    controls_["rotation"].first->setToolTip("Shift + right-drag horizontally in the 3D viewport to rotate lighting and reflections.");
    controls_["rotation"].second->setToolTip(controls_["rotation"].first->toolTip());
    auto *alignment_row=new QHBoxLayout;follows_view_=new QCheckBox("Lighting follows view",this);follows_view_->setObjectName("lightingFollowsView");follows_view_->setToolTip("Enabled: camera-relative Studio Light. Disabled: light remains fixed in world space. Rotation works in both modes.");alignment_row->addWidget(follows_view_);
    alignment_=new QComboBox(this);alignment_->setObjectName("environmentAlignment");alignment_->setAccessibleName("Environment alignment");
    alignment_->addItems({"World","Local"});alignment_->hide();layout->addLayout(alignment_row);
    connect(follows_view_,&QCheckBox::toggled,this,[this](bool follows){alignment_->setCurrentIndex(follows?1:0);});
    connect(alignment_,&QComboBox::currentIndexChanged,this,[this](int index){const QSignalBlocker block(follows_view_);follows_view_->setChecked(index==1);if(changed)changed("alignment",index);});

    post_=new DisplayPostPanel(content);layout->addWidget(post_);post_->changed=[this](const QString &key,double value){if(changed)changed("post."+key,value);};
    sss_enabled_=new QCheckBox("Activate Subsurface Scattering",content);sss_enabled_->setObjectName("subsurfacePreviewEnabled");layout->addWidget(sss_enabled_);auto *sss_row=new QHBoxLayout;sss_row->addWidget(new QLabel("Sample Count",content));sss_samples_=new QSpinBox(content);sss_samples_->setObjectName("subsurfacePreviewSamples");sss_samples_->setRange(1,64);sss_samples_->setKeyboardTracking(false);sss_samples_->setToolTip("EEVEE diffusion kernel samples. Cycles uses its rendering sample limit.");sss_row->addWidget(sss_samples_);layout->addLayout(sss_row);auto *sss_reset=new QPushButton("Restore subsurface defaults",content);sss_reset->setObjectName("subsurfacePreviewRestore");layout->addWidget(sss_reset);connect(sss_enabled_,&QCheckBox::toggled,this,[this](bool on){sss_samples_->setEnabled(on);if(changed)changed("subsurface.enabled",on);});connect(sss_samples_,&QSpinBox::valueChanged,this,[this](int value){if(changed)changed("subsurface.samples",value);});connect(sss_reset,&QPushButton::clicked,this,[this]{if(changed)changed("subsurface.reset",1);});
    layout->addStretch();
  }
  void set_state(const QJsonObject &state) {
    const auto subsurface=state["subsurface"].toObject();{const QSignalBlocker a(sss_enabled_),b(sss_samples_);sss_enabled_->setChecked(subsurface["enabled"].toBool());sss_samples_->setValue(subsurface["samples"].toInt(16));sss_samples_->setEnabled(sss_enabled_->isChecked()&&!sss_samples_->property("renderUnavailable").toBool());}
    resource_->setText(state["resource"].toString()=="default-studio.hdr"?"Studio":state["resource"].toString());
    post_->synchronize(state["post_effects"].toObject());
    const QSignalBlocker alignment_blocker(alignment_);alignment_->setCurrentIndex(state["alignment"].toString()=="Local"?1:0);
    const QSignalBlocker follow_blocker(follows_view_);follows_view_->setChecked(state.contains("lighting_follows_view")?state["lighting_follows_view"].toBool():state["alignment"]=="Local");
    for(const auto &[key,widgets]:controls_) {
      const auto [number,slider]=widgets;const QSignalBlocker a(number),b(slider);
      const double value=state[key].toDouble();number->setValue(value);slider->setValue(qRound(value*10));
    }
  }
  std::function<void(const QString &,double)> changed;
  std::function<void()> resource_requested;
 private:
  DisplayPostPanel *post_;
  QCheckBox *sss_enabled_;QSpinBox *sss_samples_;
  QPushButton *resource_;
  QComboBox *alignment_;
  QCheckBox *follows_view_;
  std::map<QString,std::pair<QDoubleSpinBox *,QSlider *>> controls_;
};
}
