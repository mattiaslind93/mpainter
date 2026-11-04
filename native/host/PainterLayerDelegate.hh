#include "Theme.hh"
#pragma once
#include "InlineNameDelegate.hh"
#include "LayerRowLayout.hh"
#include "PainterWorkflow.hh"
#include <QApplication>
#include <QPalette>
#include <QAbstractItemModel>
#include <QUuid>
namespace bts {
class PainterLayerDelegate final:public InlineNameDelegate {
 public:
 using InlineNameDelegate::InlineNameDelegate;
 std::function<void(const QModelIndex &)> opacity_requested,blend_requested;
 std::function<void(QUuid)> folder_toggled,geometry_selected,instance_selected,mask_bypassed,mask_isolated;
 std::function<void(QUuid,QUuid)> effect_removed;
 std::function<void(QUuid,bool)> target_selected;
 void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index)const override{
  QStyleOptionViewItem style(option);initStyleOption(&style,index);const QString name=style.text.trimmed();const auto thumbnail=style.icon;const bool effect=!index.data(Qt::UserRole+10).toString().isEmpty();
  style.text.clear();style.icon={};style.features&=~(QStyleOptionViewItem::HasDecoration|QStyleOptionViewItem::HasCheckIndicator);const auto *widget=style.widget;(widget?widget->style():QApplication::style())->drawControl(QStyle::CE_ItemViewItem,&style,painter,widget);
  painter->save();if(index.data(Qt::UserRole+27).toBool())painter->setOpacity(.43);painter->setFont(style.font);const int y=style.rect.center().y();const int indent=index.data(Qt::UserRole+2).toInt()*20;const bool enabled=index.data(Qt::CheckStateRole).toInt()==Qt::Checked;const auto text_color=style.palette.color(style.state&QStyle::State_Selected?QPalette::HighlightedText:QPalette::Text);painter->setPen(text_color);
  const auto eye=layer_eye_rect(style.rect,effect?indent:0);QStyleOption check;check.rect=QRect(eye.center()-QPoint(8,8),QSize(16,16));check.state=style.state;check.state&=~(QStyle::State_On|QStyle::State_Off);check.state|=enabled?QStyle::State_On:QStyle::State_Off;(widget?widget->style():QApplication::style())->drawPrimitive(QStyle::PE_IndicatorCheckBox,&check,painter,widget);
  if(effect){
   const QRect glyph(eye.right()+7,y-8,16,16);paint_workflow_glyph(*painter,glyph,PainterGlyph(index.data(Qt::UserRole+23).toInt()));
   const auto controls=layer_effect_controls_rect(style.rect);const QRect text_rect(glyph.right()+6,style.rect.top(),qMax(0,controls.left()-glyph.right()-10),style.rect.height());painter->drawText(text_rect,Qt::AlignVCenter|Qt::AlignLeft,style.fontMetrics.elidedText(name,Qt::ElideRight,text_rect.width()));
   if(index.data(Qt::UserRole+24).isValid()&&!index.data(Qt::UserRole+24).toBool())painter->setPen(QColor(bts::theme::Muted));
   painter->drawText(layer_effect_blend_rect(style.rect),Qt::AlignCenter,compact_layer_blend(index.data(Qt::UserRole+5).toInt()));painter->drawText(layer_effect_opacity_rect(style.rect),Qt::AlignCenter,compact_layer_opacity(index.data(Qt::UserRole+1).toDouble()));painter->setPen(text_color);painter->drawText(layer_effect_delete_rect(style.rect),Qt::AlignCenter,"×");painter->restore();return;
  }
  int x=style.rect.left()+26+indent;
  if(index.data(Qt::UserRole+3).toBool()){painter->drawText(QRect(x-5,y-10,14,20),Qt::AlignCenter,index.data(Qt::UserRole+4).toBool()?"▸":"▾");paint_workflow_glyph(*painter,QRect(x+10,y-8,16,16),PainterGlyph::Folder);x+=30;}
  thumbnail.paint(painter,QRect(x,y-12,24,24));const int thumbnail_x=x;x+=30;
  if(index.data(Qt::UserRole+6).toBool()){
   const auto mask=layer_mask_rect(index,style.rect);qvariant_cast<QIcon>(index.data(Qt::UserRole+7)).paint(painter,mask);
   if(!index.data(Qt::UserRole+9).toBool()){painter->setPen(QPen(QColor(bts::theme::Error),1));painter->drawLine(mask.topLeft(),mask.bottomRight());painter->drawLine(mask.topRight(),mask.bottomLeft());}
   if(index.data(Qt::UserRole+8).toBool()){painter->setPen(QPen(QColor(bts::theme::Accent),1));painter->drawRect(mask.adjusted(-1,-1,1,1));}
   if(index.data(Qt::UserRole+12).toBool())painter->fillRect(mask.left(),mask.bottom()+2,24,2,QColor(bts::theme::Warning));
   x+=30;
  }
  if((style.state&QStyle::State_Selected)&&!index.data(Qt::UserRole+8).toBool()){painter->setPen(QPen(QColor(bts::theme::Accent),1));painter->drawRect(QRect(thumbnail_x-1,y-13,26,26));}
  const unsigned channels=index.data(Qt::UserRole+22).toUInt();int marker=0;for(int channel:{0,1,2,4,5,14}){painter->fillRect(thumbnail_x+marker*4,y+14,3,2,channels&(1u<<channel)?QColor(bts::theme::Warning):QColor(bts::theme::Border));++marker;}
  const auto geometry=layer_geometry_rect(index,style.rect);paint_workflow_glyph(*painter,geometry.adjusted(4,4,-4,-4),PainterGlyph::Geometry,index.data(Qt::UserRole+17).toBool()?QColor(bts::theme::Accent):index.data(Qt::UserRole+18).toBool()?QColor(bts::theme::Heading):QColor(bts::theme::Muted));x=geometry.right()+6;
  if(index.data(Qt::UserRole+20).toBool()){const auto box=layer_instance_rect(index,style.rect);painter->setPen(QPen(text_color,1));painter->drawRect(box.adjusted(2,2,-8,-8));painter->drawRect(box.adjusted(7,7,-3,-3));if(index.data(Qt::UserRole+19).toBool())painter->drawText(box.adjusted(8,5,3,5),Qt::AlignCenter,"→");x=box.right()+6;}
  if(!index.data(Qt::UserRole+21).toString().isEmpty())painter->fillRect(style.rect.right()-2,style.rect.top()+2,2,style.rect.height()-4,QColor(bts::theme::Warning));
  painter->setPen(text_color);const auto controls=layer_controls_rect(style.rect);const QRect text_rect(x,style.rect.top(),qMax(0,controls.left()-x-4),style.rect.height());painter->drawText(text_rect,Qt::AlignVCenter|Qt::AlignLeft,style.fontMetrics.elidedText(name,Qt::ElideRight,text_rect.width()));painter->drawText(controls.adjusted(0,3,-5,-3),Qt::AlignTop|Qt::AlignRight,compact_layer_blend(index.data(Qt::UserRole+5).toInt())+" ▾");painter->drawText(controls.adjusted(0,3,-5,-3),Qt::AlignBottom|Qt::AlignRight,compact_layer_opacity(index.data(Qt::UserRole+1).toDouble())+" ▾");painter->restore();
 }
 bool editorEvent(QEvent *event,QAbstractItemModel *model,const QStyleOptionViewItem &option,const QModelIndex &index)override{
  if(index.data(Qt::UserRole+27).toBool())return true;
  if(event->type()!=QEvent::MouseButtonRelease)return InlineNameDelegate::editorEvent(event,model,option,index);
  const auto *mouse=static_cast<QMouseEvent *>(event);if(mouse->button()!=Qt::LeftButton)return InlineNameDelegate::editorEvent(event,model,option,index);const auto point=mouse->position().toPoint();const auto id=QUuid(index.data(Qt::UserRole).toString());const bool effect=!index.data(Qt::UserRole+10).toString().isEmpty();
  if(layer_eye_rect(option.rect,effect?index.data(Qt::UserRole+2).toInt()*20:0).contains(point)){model->setData(index,index.data(Qt::CheckStateRole).toInt()==Qt::Checked?Qt::Unchecked:Qt::Checked,Qt::CheckStateRole);return true;}
  if(effect){if(layer_effect_delete_rect(option.rect).contains(point)){if(effect_removed)effect_removed(id,QUuid(index.data(Qt::UserRole+10).toString()));return true;}if(index.data(Qt::UserRole+24).isValid()&&!index.data(Qt::UserRole+24).toBool()&&layer_effect_controls_rect(option.rect).contains(point))return true;if(layer_effect_blend_rect(option.rect).contains(point)){if(blend_requested)blend_requested(index);return true;}if(layer_effect_opacity_rect(option.rect).contains(point)){if(opacity_requested)opacity_requested(index);return true;}return InlineNameDelegate::editorEvent(event,model,option,index);}
  if(index.data(Qt::UserRole+20).toBool()&&layer_instance_rect(index,option.rect).contains(point)){if(instance_selected)instance_selected(id);return true;}
  if(!index.data(Qt::UserRole+19).toBool()&&layer_geometry_rect(index,option.rect).contains(point)){if(geometry_selected)geometry_selected(id);return true;}
  if(QRect(layer_thumbnail_x(index,option.rect),option.rect.center().y()-12,24,24).contains(point)){if(target_selected)target_selected(id,false);return true;}
  if(index.data(Qt::UserRole+6).toBool()&&layer_mask_rect(index,option.rect).contains(point)){if(mouse->modifiers()&Qt::ShiftModifier){if(mask_bypassed)mask_bypassed(id);}else if(mouse->modifiers()&Qt::AltModifier){if(mask_isolated)mask_isolated(id);}else if(target_selected)target_selected(id,true);return true;}
  if(index.data(Qt::UserRole+3).toBool()&&point.x()>option.rect.left()+22&&point.x()<std::min(option.rect.right()-layer_controls_width,option.rect.left()+76+index.data(Qt::UserRole+2).toInt()*20)){if(folder_toggled)folder_toggled(id);return true;}
  if(layer_controls_rect(option.rect).contains(point)){if(point.y()<=option.rect.center().y()){if(blend_requested)blend_requested(index);}else if(opacity_requested)opacity_requested(index);return true;}
  return InlineNameDelegate::editorEvent(event,model,option,index);
 }
};
}
