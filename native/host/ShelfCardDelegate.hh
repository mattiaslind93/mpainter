#pragma once
#include "InlineNameDelegate.hh"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QTextLayout>
namespace bts {
// Keep rename delivery and drag roles in InlineNameDelegate. Painting only
// consumes the existing GPU/embedded icon; it never evaluates a material.
class ShelfCardDelegate final:public InlineNameDelegate {
 public:using InlineNameDelegate::InlineNameDelegate;
 void paint(QPainter *painter,const QStyleOptionViewItem &option,const QModelIndex &index)const override{
  QStyleOptionViewItem style(option);initStyleOption(&style,index);const auto text=style.text;const auto icon=style.icon;style.text.clear();style.icon={};style.features&=~QStyleOptionViewItem::HasDecoration;
  const auto *widget=style.widget;(widget?widget->style():QApplication::style())->drawControl(QStyle::CE_ItemViewItem,&style,painter,widget);
  painter->save();const int icon_size=qMin(style.rect.width()-6,qMax(16,style.rect.height()-32));const QRect image(style.rect.center().x()-icon_size/2,style.rect.top()+2,icon_size,icon_size);icon.paint(painter,image,Qt::AlignCenter,style.state&QStyle::State_Enabled?QIcon::Normal:QIcon::Disabled);
  painter->setFont(style.font);painter->setPen(style.palette.color(style.state&QStyle::State_Selected?QPalette::HighlightedText:QPalette::Text));
  const qreal width=qMax(0,style.rect.width()-4);QTextLayout layout(text,style.font);QTextOption text_option;text_option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);layout.setTextOption(text_option);layout.beginLayout();auto first=layout.createLine();if(first.isValid())first.setLineWidth(width);const int split=first.isValid()?first.textLength():0;layout.endLayout();
  const int line_height=style.fontMetrics.height();const QRect first_rect(style.rect.left()+2,image.bottom()+2,int(width),line_height);
  painter->drawText(first_rect,Qt::AlignHCenter|Qt::AlignTop,text.left(split).trimmed());
  if(split<text.size())painter->drawText(first_rect.translated(0,line_height),Qt::AlignHCenter|Qt::AlignTop,style.fontMetrics.elidedText(text.mid(split).trimmed(),Qt::ElideRight,int(width)));
  painter->restore();
 }
};
}
