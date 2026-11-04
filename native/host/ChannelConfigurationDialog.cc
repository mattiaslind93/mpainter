#include "ChannelConfiguration.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
namespace bts {
std::optional<ChannelConfigurationEdit> choose_channel_configuration(QWidget *parent,const TextureDocument &document,const TextureSet &source)
{
  QDialog dialog(parent);dialog.setObjectName("channelConfigurationDialog");dialog.setWindowTitle("Add or remove channels");dialog.resize(540,640);
  auto *layout=new QVBoxLayout(&dialog);
  auto *explanation=new QLabel("Start from "+QString::fromStdString(source.name)+". Apply configures channel presence and precision together. Custom slot names and existing layer data stay local to each Texture Set.",&dialog);explanation->setWordWrap(true);layout->addWidget(explanation);
  auto *scroll=new QScrollArea(&dialog);scroll->setWidgetResizable(true);auto *body=new QWidget(scroll);auto *grid=new QGridLayout(body);
  grid->addWidget(new QLabel("Channel",body),0,0);grid->addWidget(new QLabel("Precision",body),0,1);grid->addWidget(new QLabel("Present in sets",body),0,2);
  std::array<QCheckBox *,15> enabled{};std::array<QComboBox *,15> precision{};
  const auto initial=channel_configuration(source);
  const QStringList names{"Base Color","Metallic","Roughness","Normal","Bump","Ambient occlusion"};
  int eligible=0,skipped=0;for(const auto &set:document.texture_sets())if(!set.disabled&&set.uniform_editable)++eligible;else ++skipped;
  for(int c=0;c<15;++c){
    const auto name=c<6?names[c]:c==14?QString("Displacement"):QString("User%1 — %2").arg(c-6).arg(QString::fromStdString(source.users[c-6].name));
    enabled[c]=new QCheckBox(name,body);enabled[c]->setObjectName(QString("batchChannelEnabled%1").arg(c));enabled[c]->setChecked(initial.channels[c].enabled);if(c==4)enabled[c]->setToolTip("Bump. One grayscale channel. Affects shading through bump-to-normal and does not move the surface.");if(c==14)enabled[c]->setToolTip("Displacement. Moves the surface. Separate from grayscale Bump.");if(c!=3)grid->addWidget(enabled[c],c+1,0);else enabled[c]->hide();
    precision[c]=new QComboBox(body);precision[c]->setObjectName(QString("batchChannelPrecision%1").arg(c));for(auto label:channel_precision_names)precision[c]->addItem(label);precision[c]->setCurrentIndex(int(initial.channels[c].precision));precision[c]->setEnabled(enabled[c]->isChecked());if(c!=3)grid->addWidget(precision[c],c+1,1);else precision[c]->hide();QObject::connect(enabled[c],&QCheckBox::toggled,precision[c],&QWidget::setEnabled);
    int count=0;for(const auto &set:document.texture_sets())if(!set.disabled&&set.uniform_editable&&channel_configuration(set).channels[c].enabled)++count;
    auto *count_label=new QLabel(QString("%1 / %2").arg(count).arg(eligible),body);count_label->setObjectName(QString("batchChannelCount%1").arg(c));if(c!=3)grid->addWidget(count_label,c+1,2);else count_label->hide();
  }
  scroll->setWidget(body);layout->addWidget(scroll,1);
  auto *all=new QCheckBox(QString("Apply to all Texture Sets (%1 editable)").arg(eligible),&dialog);all->setObjectName("batchChannelsAllSets");all->setEnabled(eligible>1);layout->addWidget(all);
  if(skipped){auto *note=new QLabel(QString("%1 inactive or unsupported Texture Sets will be skipped.").arg(skipped),&dialog);note->setWordWrap(true);layout->addWidget(note);}
  auto *help=new QLabel("Removing a channel hides its output. Paint, masks and fill inputs are retained for Undo or re-adding the channel.",&dialog);help->setWordWrap(true);layout->addWidget(help);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText("Apply");buttons->button(QDialogButtonBox::Ok)->setObjectName("applyChannelConfiguration");buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelChannelConfiguration");layout->addWidget(buttons);
  QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
    bool removing=false;for(const auto &set:document.texture_sets())if(!set.disabled&&set.uniform_editable&&(all->isChecked()||set.id==source.id)){const auto before=channel_configuration(set);for(int c=0;c<15;++c)removing|=before.channels[c].enabled&&!enabled[c]->isChecked();}
    if(removing){QMessageBox warning(QMessageBox::Warning,"Remove channel outputs?","Removed channels stop contributing to the preview and export. Existing layer data is retained.",QMessageBox::Ok|QMessageBox::Cancel,&dialog);warning.setObjectName("confirmChannelRemoval");warning.setDefaultButton(QMessageBox::Cancel);if(warning.exec()!=QMessageBox::Ok)return;}
    dialog.accept();
  });
  if(dialog.exec()!=QDialog::Accepted)return {};
  ChannelConfigurationEdit result;result.all_sets=all->isChecked();for(int c=0;c<15;++c)result.configuration.channels[c]={enabled[c]->isChecked(),ChannelPrecision(precision[c]->currentIndex())};return result;
}
}
