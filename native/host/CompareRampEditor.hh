#include "Theme.hh"
#pragma once
#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <array>
#include <cmath>
#include <algorithm>
#include <functional>
namespace bts {
// Five fixed abscissae avoid duplicate/crossing knots while permitting arbitrary ramps.
class CompareRampEditor final : public QWidget {
 public:
  explicit CompareRampEditor(QWidget *parent=nullptr):QWidget(parent){setMinimumHeight(100);setAccessibleName("Compare Mask curve ramp");setToolTip("Drag a curve point vertically. Double click to reset the linear ramp.");}
  std::array<float,5> points{0,.25f,.5f,.75f,1};
  std::function<void(const std::array<float,5> &)> changed;
 protected:
  void paintEvent(QPaintEvent *)override{
    QPainter p(this);p.fillRect(rect(),QColor(bts::theme::Canvas));p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(8,0,width()-8,0);for(int i=0;i<5;++i)gradient.setColorAt(i/4.,QColor::fromRgbF(points[i],points[i],points[i]));
    p.fillRect(QRectF(8,height()-17,width()-16,10),gradient);
    p.setPen(QPen(QColor(bts::theme::Accent),2));for(int i=1;i<5;++i)p.drawLine(point(i-1),point(i));
    p.setBrush(QColor(bts::theme::Heading));for(int i=0;i<5;++i)p.drawEllipse(point(i),4,4);
  }
  void mousePressEvent(QMouseEvent *e)override{if(e->button()!=Qt::LeftButton)return;active_=std::clamp(int(std::lround((e->position().x()-8)*4/std::max(1,width()-16))),0,4);move(e);}
  void mouseMoveEvent(QMouseEvent *e)override{if(active_>=0&&(e->buttons()&Qt::LeftButton))move(e);}
  void mouseReleaseEvent(QMouseEvent *)override{active_=-1;}
  void mouseDoubleClickEvent(QMouseEvent *)override{points={0,.25f,.5f,.75f,1};if(changed)changed(points);update();}
 private:
  int active_=-1;
  QPointF point(int i)const{return {8+i*(width()-16)/4.,8+(1-points[i])*(height()-36)};}
  void move(QMouseEvent *e){points[active_]=std::clamp(float(1-(e->position().y()-8)/std::max(1,height()-36)),0.f,1.f);if(changed)changed(points);update();}
};
}
