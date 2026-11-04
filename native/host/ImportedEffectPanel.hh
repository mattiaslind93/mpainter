#pragma once
#include "NumericLocale.hh"
#include "TextureDocument.hh"
#include "MaskEffectPath.hh"
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QStandardItemModel>
#include <functional>
namespace bts {
// Native editors for the ordered Fill and isolated mask Group effect kinds.
class ImportedEffectPanel final:public QWidget {
 LevelsEffect fill_;MaskEffect group_;bool is_group_=false;
 std::array<QDoubleSpinBox *,3> values_;std::array<QLabel *,3> labels_;
 QDoubleSpinBox *opacity_;QComboBox *blend_;QTreeWidget *children_;
 void publish(){if(is_group_){group_.opacity=float(opacity_->value()/100);group_.blending=LayerBlend(blend_->currentData().toInt());if(mask_changed)mask_changed(group_);}else{for(int c=0;c<3;++c)fill_.fill_value[c]=float(values_[fill_.channel==0?c:0]->value());fill_.opacity=float(opacity_->value()/100);fill_.blending=LayerBlend(blend_->currentData().toInt());if(content_changed)content_changed(fill_);}}
 public:
 std::function<void(const LevelsEffect &)> content_changed;
 std::function<void(const MaskEffect &)> mask_changed;
 std::function<void(const QUuid &)> child_selected;
 explicit ImportedEffectPanel(QWidget *parent):QWidget(parent){setObjectName("orderedImportedEffectProperties");auto *form=new QFormLayout(this);const std::array<const char *,3> names{"Red","Green","Blue"};for(int c=0;c<3;++c){labels_[c]=new QLabel(names[c],this);values_[c]=new NumericSpinBox(this);values_[c]->setObjectName(QString("orderedFillValue%1").arg(c));values_[c]->setRange(-65504,65504);values_[c]->setDecimals(4);values_[c]->setKeyboardTracking(false);form->addRow(labels_[c],values_[c]);connect(values_[c],&QDoubleSpinBox::valueChanged,this,[this]{publish();});}
  opacity_=new NumericSpinBox(this);opacity_->setObjectName("orderedEffectOpacity");opacity_->setRange(0,100);opacity_->setSuffix(" %");opacity_->setKeyboardTracking(false);form->addRow("Opacity",opacity_);blend_=new QComboBox(this);blend_->setObjectName("orderedEffectBlend");for(int i=0;i<int(layer_blend_names.size());++i)blend_->addItem(layer_blend_names[i],i);form->addRow("Blending",blend_);connect(opacity_,&QDoubleSpinBox::valueChanged,this,[this]{publish();});connect(blend_,&QComboBox::currentIndexChanged,this,[this]{publish();});
  children_=new QTreeWidget(this);children_->setObjectName("maskGroupChildren");children_->setHeaderLabel("Group effects");form->addRow(children_);connect(children_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem *item){if(!is_group_||!group_.group_effects)return;const QUuid id(item->data(0,Qt::UserRole).toString());auto effects=std::make_shared<std::vector<MaskEffect>>(*group_.group_effects);auto *effect=edit_mask_effect(*effects,id);if(!effect)return;effect->enabled=item->checkState(0)==Qt::Checked;group_.group_effects=std::move(effects);publish();});connect(children_,&QTreeWidget::itemActivated,this,[this](QTreeWidgetItem *item){if(child_selected)child_selected(QUuid(item->data(0,Qt::UserRole).toString()));});
 }
 void synchronize(const LevelsEffect *fill,const MaskEffect *group){setVisible(fill||group);if(!fill&&!group)return;is_group_=group;const QSignalBlocker opacity_block(opacity_),blend_block(blend_),children_block(children_);if(fill)fill_=*fill;if(group)group_=*group;
  for(int c=0;c<3;++c){const QSignalBlocker block(values_[c]);values_[c]->setVisible(fill&&!fill->source_coverage&&(c==0||fill->channel==0));labels_[c]->setVisible(fill&&!fill->source_coverage&&(c==0||fill->channel==0));if(fill)values_[c]->setValue(fill->fill_value[c]);}labels_[0]->setText(fill&&fill->channel==0?"Red":"Value");opacity_->setValue((group?group->opacity:fill->opacity)*100);blend_->setCurrentIndex(blend_->findData(int(group?group->blending:fill->blending)));if(auto *model=qobject_cast<QStandardItemModel *>(blend_->model())){auto *item=model->item(blend_->findData(int(LayerBlend::Passthrough)));item->setEnabled(!group);item->setToolTip(group?"Mask Groups isolate their children over zero. Passthrough applies only to layer folders.":QString{});}children_->setVisible(group);children_->clear();if(group&&group->group_effects){std::vector<QTreeWidgetItem *> parents;for(const auto &entry:mask_effect_entries(*group->group_effects,true)){const auto &effect=*entry.effect;auto *item=entry.depth?new QTreeWidgetItem(parents.at(entry.depth-1)):new QTreeWidgetItem(children_);if(parents.size()<=entry.depth)parents.resize(entry.depth+1);parents[entry.depth]=item;const char *kind=effect.kind==MaskEffectKind::Group?"Group":effect.kind==MaskEffectKind::Generator?"Generator":effect.kind==MaskEffectKind::Filter?"Filter":effect.kind==MaskEffectKind::Fill?"Fill":effect.kind==MaskEffectKind::Levels?"Levels":effect.kind==MaskEffectKind::Paint?"Paint":"Mask effect";item->setText(0,effect.name.empty()?QString::fromLatin1(kind):QString::fromStdString(effect.name));item->setToolTip(0,"Activate to edit this effect in Properties");item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(0,effect.enabled?Qt::Checked:Qt::Unchecked);item->setData(0,Qt::UserRole,effect.id.toString());}children_->expandAll();}
 }
};
}
