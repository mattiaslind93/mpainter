#include "Theme.hh"
#pragma once
#include "LayerRowLayout.hh"
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QAbstractItemDelegate>
#include <QStyleOptionViewItem>
#include <QMimeData>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QKeyEvent>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>
namespace bts {
// The document owns ordering. Qt previews a gap but never mutates its item model on drop.
class LayerStackList final : public QListWidget {
 public:
  explicit LayerStackList(QWidget *parent):QListWidget(parent) {
    setFrameShape(QFrame::NoFrame);setDragEnabled(true);setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(false);
    scroll_.setInterval(40);
    connect(&scroll_,&QTimer::timeout,this,[this] {
      const int step=last_position_.y()<24?-16:last_position_.y()>viewport()->height()-24?16:0;
      if(step){verticalScrollBar()->setValue(verticalScrollBar()->value()+step);update_gap(last_position_);}
    });
  }
  std::function<void(QUuid,int)> layer_dropped;
  std::function<void(const std::vector<QUuid> &,QUuid,bool,bool)> hierarchy_dropped;
  std::function<void()> delete_requested;
  std::function<void()> copy_reference,paste_instance;
  std::function<bool(const QString &)> asset_accepted;
  std::function<void(const QString &,int,bool,int,bool)> asset_dropped;
  int insertion_row() const {return gap_;}
  std::unique_ptr<QMimeData> drag_data(const QUuid &id) const {
    auto mime=std::make_unique<QMimeData>();mime->setData(format,token_+'|'+id.toByteArray());QJsonArray selected;bool includes=false;for(auto *item:selectedItems())if(item->data(Qt::UserRole+10).toString().isEmpty()){const QUuid selected_id(item->data(Qt::UserRole).toString());selected.append(selected_id.toString());includes|=selected_id==id;}if(!includes)selected=QJsonArray{id.toString()};mime->setData("application/x-bts-layer-selection",QJsonDocument(selected).toJson(QJsonDocument::Compact));return mime;
  }
 protected:
  void keyPressEvent(QKeyEvent *event) override{if(event->key()==Qt::Key_Delete&&delete_requested){delete_requested();event->accept();return;}if(event->matches(QKeySequence::Copy)&&copy_reference){copy_reference();event->accept();return;}if(event->key()==Qt::Key_V&&event->modifiers()==(Qt::ControlModifier|Qt::ShiftModifier)&&paste_instance){paste_instance();event->accept();return;}QListWidget::keyPressEvent(event);}
  void mousePressEvent(QMouseEvent *event) override {
    const auto index=indexAt(event->position().toPoint());
    const auto point=event->position().toPoint();
    const auto row=visualRect(index);
    const bool effect=!index.data(Qt::UserRole+10).toString().isEmpty();
    const bool control=index.isValid()&&(
      layer_eye_rect(row,effect?index.data(Qt::UserRole+2).toInt()*20:0).contains(point)||
      (index.data(Qt::UserRole+20).toBool()&&layer_instance_rect(index,row).contains(point))||
      (effect?layer_effect_controls_rect(row):layer_controls_rect(row)).contains(point)||
      (!effect&&layer_geometry_rect(index,row).contains(point))||
      (!effect&&QRect(layer_thumbnail_x(index,row),row.center().y()-12,index.data(Qt::UserRole+6).toBool()?54:24,24).contains(point)));
    if(event->button()==Qt::LeftButton&&control) {
      control_index_=index;event->accept();return;
    }
    if(event->button()==Qt::LeftButton&&index.isValid()&&(event->modifiers()&Qt::ShiftModifier)) {
      const QModelIndex anchor=selection_anchor_.isValid()?QModelIndex(selection_anchor_):currentIndex().isValid()?currentIndex():index;
      selectionModel()->setCurrentIndex(index,QItemSelectionModel::NoUpdate);
      const QItemSelection range(model()->index(std::min(anchor.row(),index.row()),0),model()->index(std::max(anchor.row(),index.row()),0));
      selectionModel()->select(range,QItemSelectionModel::ClearAndSelect);scrollTo(index);
      range_click_=true;event->accept();return;
    }
    QListWidget::mousePressEvent(event);
    if(event->button()==Qt::LeftButton&&index.isValid())selection_anchor_=index;
  }
  void mouseReleaseEvent(QMouseEvent *event) override {
    if(control_index_.isValid()&&event->button()==Qt::LeftButton) {
      const QModelIndex index=control_index_;control_index_=QModelIndex{};QStyleOptionViewItem option;initViewItemOption(&option);option.rect=visualRect(index);
      itemDelegateForIndex(index)->editorEvent(event,model(),option,index);event->accept();return;
    }
    if(range_click_&&event->button()==Qt::LeftButton){range_click_=false;event->accept();return;}
    QListWidget::mouseReleaseEvent(event);
  }
  void startDrag(Qt::DropActions) override {
    if(!currentItem()||!currentItem()->data(Qt::UserRole+10).toString().isEmpty())return;
    QDrag drag(this);drag.setMimeData(drag_data(QUuid(currentItem()->data(Qt::UserRole).toString())).release());
    drag.setPixmap(currentItem()->icon().pixmap(32,32));drag.exec(Qt::MoveAction);clear_gap();
  }
  void dragEnterEvent(QDragEnterEvent *event) override {
    if(accepted_asset(event->mimeData())){asset_drag_=true;update_gap(event->position().toPoint());scroll_.start();event->setDropAction(Qt::CopyAction);event->accept();return;}
    if(source_row(event->mimeData())<0){event->ignore();return;}
    update_gap(event->position().toPoint());scroll_.start();event->setDropAction(Qt::MoveAction);event->accept();
  }
  void dragMoveEvent(QDragMoveEvent *event) override {
    if(accepted_asset(event->mimeData())){asset_drag_=true;update_gap(event->position().toPoint());event->setDropAction(Qt::CopyAction);event->accept();return;}
    if(source_row(event->mimeData())<0){clear_gap();event->ignore();return;}
    update_gap(event->position().toPoint());event->setDropAction(Qt::MoveAction);event->accept();
  }
  void dragLeaveEvent(QDragLeaveEvent *event) override {clear_gap();event->accept();}
  void dropEvent(QDropEvent *event) override {
    if(accepted_asset(event->mimeData())){const auto id=QString::fromUtf8(event->mimeData()->data("application/x-bts-asset"));update_gap(event->position().toPoint());const int gap=gap_,target=indexAt(event->position().toPoint()).row();const bool replace=event->modifiers()&Qt::ControlModifier;const auto index=indexAt(event->position().toPoint());const bool mask=index.isValid()&&index.data(Qt::UserRole+6).toBool()&&layer_mask_rect(index,visualRect(index)).contains(event->position().toPoint());clear_gap();if(asset_dropped)asset_dropped(id,gap,replace,target,mask);event->setDropAction(Qt::CopyAction);event->accept();return;}
    const int source=source_row(event->mimeData());
    if(source<0){clear_gap();event->ignore();return;}
    update_gap(event->position().toPoint());const int target_row=gap_-(gap_>source?1:0);
    const QUuid id(item(source)->data(Qt::UserRole).toString());const auto target=drop_target_;const bool into=into_folder_,above=drop_above_;std::vector<QUuid> ids;for(const auto &entry:QJsonDocument::fromJson(event->mimeData()->data("application/x-bts-layer-selection")).array()){const QUuid selected(entry.toString());if(!selected.isNull())ids.push_back(selected);}if(ids.empty())ids.push_back(id);clear_gap();
    if(hierarchy_dropped)hierarchy_dropped(ids,target,into,above);else if(target_row!=source&&layer_dropped)layer_dropped(id,target_row);
    event->setDropAction(Qt::MoveAction);event->accept();
  }
  void paintEvent(QPaintEvent *event) override {
    QListWidget::paintEvent(event);QPainter painter(viewport());
    if(count()==0){painter.setPen(QColor(bts::theme::Muted));painter.drawText(viewport()->rect(),Qt::AlignCenter,"No layers");}
    if(into_folder_){for(int row=0;row<count();++row)if(QUuid(item(row)->data(Qt::UserRole).toString())==drop_target_&&item(row)->data(Qt::UserRole+10).toString().isEmpty()){const auto rect=visualItemRect(item(row)).adjusted(1,1,-2,-2);painter.fillRect(rect,bts::theme::alpha(bts::theme::Accent, 75));painter.setPen(QPen(QColor(bts::theme::Accent),2));painter.drawRect(rect);return;}}
    if(gap_<0)return;
    int y=gap_<count()?visualItemRect(item(gap_)).top():count()?visualItemRect(item(count()-1)).bottom()+1:0;
    y=std::clamp(y,1,std::max(1,viewport()->height()-2));painter.setPen(QPen(QColor(bts::theme::Accent),2));
    painter.drawLine(4,y,viewport()->width()-5,y);
  }
 private:
  static constexpr auto format="application/x-bts-layer";
  const QByteArray token_=QUuid::createUuid().toByteArray();
  bool asset_drag_=false;
  QTimer scroll_;QPoint last_position_;int gap_=-1;QUuid drop_target_;bool into_folder_=false,drop_above_=false;
  QPersistentModelIndex selection_anchor_,control_index_;bool range_click_=false;
  bool accepted_asset(const QMimeData *mime) const {return mime&&mime->hasFormat("application/x-bts-asset")&&asset_accepted&&asset_accepted(QString::fromUtf8(mime->data("application/x-bts-asset")));}
  int source_row(const QMimeData *mime) const {
    if(!mime||!mime->hasFormat(format))return -1;
    const auto data=mime->data(format);if(!data.startsWith(token_+'|'))return -1;
    const QUuid id(QString::fromUtf8(data.mid(token_.size()+1)));if(id.isNull())return -1;
    for(int row=0;row<count();++row)if(QUuid(item(row)->data(Qt::UserRole).toString())==id)return row;
    return -1;
  }
  void update_gap(QPoint point) {
    last_position_=point;const auto index=indexAt({5,point.y()});
    gap_=index.isValid()?index.row()+(point.y()>visualRect(index).center().y()?1:0):point.y()<0?0:count();into_folder_=false;drop_target_={};drop_above_=point.y()<0;
    if(asset_drag_){while(gap_<count()&&!item(gap_)->data(Qt::UserRole+10).toString().isEmpty())++gap_;viewport()->update();return;}
    if(index.isValid()){const auto rect=visualRect(index);into_folder_=point.x()>=20+index.data(Qt::UserRole+2).toInt()*20&&index.data(Qt::UserRole+3).toBool()&&index.data(Qt::UserRole+10).toString().isEmpty()&&point.y()>rect.top()+rect.height()/4&&point.y()<rect.bottom()-rect.height()/4;int target_row=index.row();if(!into_folder_&&index.data(Qt::UserRole+2).toInt()>0&&point.x()<20+index.data(Qt::UserRole+2).toInt()*20){while(target_row>0&&item(target_row)->data(Qt::UserRole+2).toInt()>0)--target_row;}drop_target_=QUuid(item(target_row)->data(Qt::UserRole).toString());drop_above_=gap_<=index.row();}
    viewport()->update();
  }
  void clear_gap(){asset_drag_=false;scroll_.stop();gap_=-1;into_folder_=false;drop_target_={};viewport()->update();}
};
}
