#include "NumericLocale.hh"
#pragma once
#include "TextureDocument.hh"
#include "HistogramMetadata.hh"
#include "LevelsRampEditor.hh"
#include <QTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <functional>
namespace bts {
class LevelsPanel final:public QWidget {
 public:
  explicit LevelsPanel(QWidget *parent):QWidget(parent){
    histogram_timer_=new QTimer(this);histogram_timer_->setInterval(16);connect(histogram_timer_,&QTimer::timeout,this,[this]{poll_histogram();});
    setObjectName("levelsProperties");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(7);
    title_=new QLabel(this);layout->addWidget(title_);channel_=new QComboBox(this);channel_->setObjectName("levelsChannel");channel_->setAccessibleName("Affected channel");layout->addWidget(channel_);
    auto *inputs=new QHBoxLayout;inputs->setSpacing(4);
    const std::array<const char *,5> names{"levelsInputLow","levelsGamma","levelsInputHigh","levelsOutputLow","levelsOutputHigh"};
    const std::array<const char *,5> labels{"Input black","Gamma","Input white","Output black","Output white"};
    for(int i=0;i<5;++i){fields_[i]=new bts::NumericSpinBox(this);fields_[i]->setObjectName(names[i]);fields_[i]->setLocale(QLocale::c());fields_[i]->setAccessibleName(labels[i]);fields_[i]->setToolTip(labels[i]);fields_[i]->setRange(i==1?.01:-65504,i==1?100:65504);fields_[i]->setDecimals(4);fields_[i]->setSingleStep(.05);fields_[i]->setKeyboardTracking(false);fields_[i]->setMinimumWidth(0);fields_[i]->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);if(i<3)inputs->addWidget(fields_[i]);connect(fields_[i],&QDoubleSpinBox::valueChanged,this,[this,i](double value){auto e=effect_;auto &curve=edited_levels_curve(e);if(i==0)curve.input_low=std::min(float(value),std::min(curve.input_high-1e-4f,std::nextafter(curve.input_high,-INFINITY)));if(i==1)curve.gamma=value;if(i==2)curve.input_high=std::max(float(value),std::max(curve.input_low+1e-4f,std::nextafter(curve.input_low,INFINITY)));if(i==3)curve.output_low=value;if(i==4)curve.output_high=value;publish(e);});}
    graph_=new LevelsRampEditor(this);graph_->setObjectName("levelsHistogram");layout->addWidget(graph_);layout->addWidget(new QLabel("Precision",this));layout->addLayout(inputs);auto *outputs=new QHBoxLayout;outputs->addWidget(fields_[3]);outputs->addWidget(fields_[4]);layout->addLayout(outputs);
    auto *buttons=new QHBoxLayout;buttons->setSpacing(3);component_=new QComboBox(this);component_->setObjectName("levelsComponent");component_->setAccessibleName("Levels component");component_->setFixedWidth(62);buttons->addWidget(component_);connect(component_,&QComboBox::currentIndexChanged,this,[this](int index){if(index>=0){effect_.component=component_->itemData(index).toInt();if(component_selected)component_selected(effect_.id,effect_.component);}});buttons->addStretch();
    for(const auto *text:{"Reset","Auto","Invert"}){auto *button=new QPushButton(text,this);button->setObjectName(QString("levels%1").arg(text));if(QString(text)=="Auto")auto_button_=button;button->setFixedWidth(44);button->setStyleSheet("QPushButton {padding:2px 4px;min-width:0;}");button->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Fixed);buttons->addWidget(button);connect(button,&QPushButton::clicked,this,[this,text]{auto e=effect_;if(QString(text)=="Reset"){const auto id=e.id;const auto channel=e.channel,component=e.component;const bool enabled=e.enabled;e={};e.id=id;e.channel=channel;e.component=component;e.enabled=enabled;}else if(QString(text)=="Invert"){auto &curve=edited_levels_curve(e);std::swap(curve.output_low,curve.output_high);}else if(maximum_-minimum_>1e-6f){auto &curve=edited_levels_curve(e);curve.input_low=minimum_;curve.input_high=maximum_;}publish(e);});}
    clamp_=new QCheckBox("Clamp",this);clamp_->setObjectName("levelsClamp");buttons->addWidget(clamp_);layout->addLayout(buttons);
    connect(clamp_,&QCheckBox::toggled,this,[this](bool value){auto e=effect_;e.clamp=value;publish(e);});
    connect(channel_,&QComboBox::currentIndexChanged,this,[this](int index){if(index>=0){auto e=effect_;e.channel=channel_->itemData(index).toInt();if(!rgb_levels_channel(e.channel))e.component=0;publish(e);}});
    graph_->changed=[this](const LevelCurve &curve){auto e=effect_;edited_levels_curve(e)=curve;publish(e);};
    graph_->edit_started=[this]{if(edit_started)edit_started();};graph_->edit_finished=[this]{if(edit_finished)edit_finished();};
  }
  std::function<void(const LevelsEffect &)> changed;
  std::function<void()> edit_started,edit_finished;
  std::function<void(QUuid,int)> component_selected;
  std::function<std::uint64_t(const TextureSet &,const HistogramRequest &)> histogram_requested;
  std::function<std::optional<HistogramMetadata>(const QUuid &)> histogram_result;
  void synchronize(const TextureSet *set,const LevelsEffect *effect,const LayerMask *mask_input=nullptr){
    setVisible(effect&&set);if(!effect||!set){histogram_timer_->stop();return;}effect_=*effect;channel_->setVisible(!mask_input);const QSignalBlocker a(channel_),b(clamp_),component_block(component_);channel_->clear();const std::array<const char *,6> names{"Base Color","Metallic","Roughness","Normal","Bump","Ambient occlusion"};for(int c=0;c<6;++c)if(c!=3&&(c==5?set->ao_channel:set->channels[c]))channel_->addItem(names[c],c);for(int slot=0;slot<custom_channel_count;++slot)if(set->users[slot].present)channel_->addItem(QString::fromStdString(set->users[slot].name),6+slot);if(set->displacement.channel)channel_->addItem("Displacement",14);channel_->setCurrentIndex(channel_->findData(effect->channel));title_->setText(mask_input?QString("LEVELS"):"LEVELS — "+channel_->currentText().toUpper());component_->clear();const bool color=!mask_input&&rgb_levels_channel(effect->channel);component_->addItem(color?"Color":"Lum",0);if(color){component_->addItem("Red",1);component_->addItem("Green",2);component_->addItem("Blue",3);}component_->setCurrentIndex(component_->findData(color?effect->component:0));clamp_->setChecked(effect->clamp);
    const auto &curve=edited_levels_curve(*effect);const std::array<float,5> values{curve.input_low,curve.gamma,curve.input_high,curve.output_low,curve.output_high};for(int i=0;i<5;++i){const QSignalBlocker block(fields_[i]);fields_[i]->setValue(values[i]);}
    graph_->set_curve(curve);if(histogram_requested){HistogramRequest request;request.layer=set->selected_layer;request.effect=effect->id;request.mask=mask_input;request.channel=effect->channel;request.component=!mask_input&&rgb_levels_channel(effect->channel)?effect->component:0;request.curve=*effect;histogram_ticket_=histogram_requested(*set,request);graph_->setProperty("gpuHistogramReady",false);auto_button_->setEnabled(false);histogram_timer_->start();poll_histogram();}graph_->update();

  }
 protected:
  void showEvent(QShowEvent *event)override{QWidget::showEvent(event);if(histogram_ticket_)histogram_timer_->start();}
 private:
  QPushButton *auto_button_=nullptr;
  QTimer *histogram_timer_=nullptr;std::uint64_t histogram_ticket_=0;
  void poll_histogram(){if(!isVisible()){histogram_timer_->stop();return;}if(!histogram_result)return;const auto result=histogram_result(effect_.id);if(!result||result->ticket!=histogram_ticket_)return;if(!result->valid){graph_->setToolTip("Histogram unavailable — see Console");histogram_timer_->stop();return;}graph_->setToolTip({});graph_->set_histogram(result->bins[0]);minimum_=result->minimum;maximum_=result->maximum;QVariantList series;for(const auto &bins:result->bins){QVariantList values;for(unsigned value:bins)values.append(value);series.append(QVariant(values));}graph_->setProperty("gpuHistogramBins",series);graph_->setProperty("gpuHistogramReady",true);auto_button_->setEnabled(true);graph_->update();histogram_timer_->stop();}
  LevelsEffect effect_;QLabel *title_;QComboBox *channel_,*component_;QCheckBox *clamp_;LevelsRampEditor *graph_;std::array<QDoubleSpinBox *,5> fields_{};float minimum_=0,maximum_=1;
  void publish(const LevelsEffect &e){effect_=e;if(changed)changed(e);}
};
}
