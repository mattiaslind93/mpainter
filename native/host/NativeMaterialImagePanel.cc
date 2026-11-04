#include "NativeMaterialImagePanel.hh"
#include "NativeMaterialImageRefresh.hh"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTimer>
namespace bts {
NativeMaterialImagePanel::NativeMaterialImagePanel(QWidget *parent):QWidget(parent){
 setObjectName("nativeImageReplacements");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,6,0,6);
 auto *heading=new QLabel("IMAGE REPLACEMENTS",this);layout->addWidget(heading);
 slot_=new QComboBox(this);slot_->setObjectName("nativeImageSlot");slot_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);slot_->setMinimumContentsLength(12);layout->addWidget(slot_);
 source_=new QLabel(this);source_->setObjectName("nativeImageSource");source_->setTextFormat(Qt::PlainText);source_->setWordWrap(true);auto label_policy=source_->sizePolicy();label_policy.setHorizontalPolicy(QSizePolicy::Ignored);source_->setSizePolicy(label_policy);layout->addWidget(source_);
 space_=new QComboBox(this);space_->setObjectName("nativeImageSourceSpace");layout->addWidget(space_);
 auto *buttons=new QHBoxLayout;file_=new QPushButton("File…",this);file_->setObjectName("nativeImageFile");shelf_=new QPushButton("Shelf…",this);shelf_->setObjectName("nativeImageShelf");buttons->addWidget(file_);buttons->addWidget(shelf_);layout->addLayout(buttons);
 status_=new QLabel(this);status_->setObjectName("nativeImageStatus");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);status_->setSizePolicy(label_policy);layout->addWidget(status_);
 reset_=new QPushButton("Reset all image replacements",this);reset_->setObjectName("nativeImageResetAll");reset_->setToolTip("Restore the first original source's images; retain current material inputs and Mapping edits");layout->addWidget(reset_);
 cancel_=new QPushButton("Cancel image replacement",this);cancel_->setObjectName("nativeImageCancel");layout->addWidget(cancel_);
 connect(slot_,&QComboBox::currentIndexChanged,this,[this]{refresh();});connect(space_,&QComboBox::currentIndexChanged,this,[this]{refresh();});
 connect(file_,&QPushButton::clicked,this,[this]{choose(true);});connect(shelf_,&QPushButton::clicked,this,[this]{choose(false);});
 connect(reset_,&QPushButton::clicked,this,[this]{if(reset_requested)reset_requested();});connect(cancel_,&QPushButton::clicked,this,[this]{if(cancel_requested)cancel_requested();});
 set_color_spaces({});refresh();
}
void NativeMaterialImagePanel::loading(const QString &source){setProperty("nativeImageInspecting",true);availability_.reset();can_reset_=false;slot_->clear();source_->setText(source);set_busy(true,"Inspecting image links…");}
void NativeMaterialImagePanel::synchronize(std::shared_ptr<const NativeImageBindingAvailability> value,bool can_reset){
 setProperty("nativeImageInspecting",false);availability_=std::move(value);can_reset_=can_reset;busy_=false;const QSignalBlocker block(slot_);slot_->clear();
 if(availability_)for(const auto &row:availability_->bindings){QStringList path;for(const auto &group:row.binding.groups)path.append(QString("%1 [%2]").arg(group.group).arg(group.node_identifier));path.append(QString("%1 · %2 → %3.%4 [%5]").arg(row.binding.image,row.binding.output,row.binding.consumer_type,row.binding.input).arg(row.binding.consumer_identifier));auto type=row.binding.consumer_type;type.remove("ShaderNode");const auto occurrence=row.binding.groups.empty()?QString():QString(" [%1]").arg(row.binding.groups.front().node_identifier);slot_->addItem(row.binding.image+" → "+type+"."+row.binding.input+occurrence);slot_->setItemData(slot_->count()-1,path.join(" / "),Qt::ToolTipRole);slot_->setItemData(slot_->count()-1,native_image_binding_json(row.binding),Qt::UserRole);}
 refresh();
}
void NativeMaterialImagePanel::set_color_spaces(const QStringList &spaces){spaces_=spaces;const QSignalBlocker block(space_);space_->clear();space_->addItem("Choose source color interpretation…");space_->addItem("Original declared color space");for(const auto &space:spaces_)space_->addItem(space);refresh();}
void NativeMaterialImagePanel::set_busy(bool busy,const QString &message){busy_=busy;refresh();if(!message.isEmpty())status_->setText(message);}
void NativeMaterialImagePanel::error(const QString &message){setProperty("nativeImageInspecting",false);busy_=false;refresh();status_->setText(message);}
void NativeMaterialImagePanel::source_context(const QString &resource,bool native,NativeHeightPolicy policy){
 setVisible(native);const auto previous=property("nativeImageResource").toString();const auto next=native?resource:QString();const auto next_policy=native?policy:NativeHeightPolicy::None;
 const auto previous_policy=property("nativeImageHeightPolicy").toInt();if(previous==next&&previous_policy==int(next_policy))return;
 setProperty("nativeImageResource",next);setProperty("nativeImageHeightPolicy",int(next_policy));
 if(native)restrict_native_image_refresh(*this,next_policy);
 QTimer::singleShot(0,this,[this]{if(source_changed)source_changed();});
}
void NativeMaterialImagePanel::target_context(const QString &token){if(property("nativeImageTarget").toString()==token)return;setProperty("nativeImageTarget",token);if(target_changed)target_changed();}
void NativeMaterialImagePanel::refresh(){
 const int index=slot_->currentIndex();const auto *row=availability_&&index>=0&&size_t(index)<availability_->bindings.size()?&availability_->bindings[size_t(index)]:nullptr;
 slot_->setEnabled(!busy_&&row);space_->setEnabled(!busy_&&row&&row->available());
 const bool explicit_space=space_->currentIndex()>0;const bool enabled=!busy_&&row&&row->available()&&explicit_space;
 file_->setEnabled(enabled);shelf_->setEnabled(enabled);reset_->setVisible(can_reset_);reset_->setEnabled(can_reset_&&!busy_);cancel_->setVisible(busy_);
 if(row){source_->setText("Source: "+row->binding.image+"\nDeclared: "+row->binding.source_settings["source_space"].toString());if(!busy_){if(!row->reason.isEmpty())status_->setText(row->reason);else if(!explicit_space)status_->setText("Choose the replacement image's color interpretation first.");else{QStringList channels;for(const auto &channel:row->active_channels)channels.append(QString::number(channel.toObject()["channel"].toInt()));status_->setText("Supported native channels: "+channels.join(", ")+". Replacement uses Float32 precision.");}}}
 else if(!busy_){source_->clear();status_->setText("No reachable image links in this native source.");}
}
void NativeMaterialImagePanel::choose(bool file){
 const int index=slot_->currentIndex();if(busy_||!availability_||index<0||size_t(index)>=availability_->bindings.size()||!availability_->bindings[size_t(index)].available())return;
 NativeImageReplacementPolicy policy;if(space_->currentIndex()==1)policy.source_space=NativeImageSourceSpace::OriginalDeclared;else if(space_->currentIndex()>1){policy.source_space=NativeImageSourceSpace::Named;policy.named_space=space_->currentText();}else return;
 if(requested)requested(availability_->bindings[size_t(index)].binding,file,policy);
}
}
