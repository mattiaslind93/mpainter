// SPDX-License-Identifier: GPL-3.0-or-later
#include "EnvironmentRotationGesture.hh"
#include <cassert>
#include <cstdio>

static QMouseEvent event(QEvent::Type type, QPointF point, Qt::MouseButton button,
                         Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers=Qt::ShiftModifier)
{
  return QMouseEvent(type,point,point,button,buttons,modifiers);
}

int main()
{
  using bts::EnvironmentRotationGesture;
  EnvironmentRotationGesture drag;
  auto press=event(QEvent::MouseButtonPress,{100,100},Qt::RightButton,Qt::RightButton);
  assert(!drag.begin(press,false,30));
  for(auto modifiers:{Qt::NoModifier,Qt::AltModifier,Qt::ControlModifier}){
    auto other=event(QEvent::MouseButtonPress,{100,100},Qt::RightButton,Qt::RightButton,modifiers);
    assert(!drag.begin(other,true,30));
  }
  auto chord=event(QEvent::MouseButtonPress,{100,100},Qt::RightButton,Qt::RightButton|Qt::LeftButton);
  assert(!drag.begin(chord,true,30));
  assert(drag.begin(press,true,30));
  auto vertical=event(QEvent::MouseMove,{100,250},Qt::NoButton,Qt::RightButton);
  assert(drag.move(vertical)==30);
  auto horizontal=event(QEvent::MouseMove,{180,250},Qt::NoButton,Qt::RightButton);
  assert(drag.move(horizontal)==70);
  auto backwards=event(QEvent::MouseMove,{80,0},Qt::NoButton,Qt::RightButton,Qt::NoModifier);
  assert(drag.move(backwards)==20);
  auto wrap=event(QEvent::MouseMove,{500,100},Qt::NoButton,Qt::RightButton);
  assert(drag.move(wrap)==-130);
  auto left_wrap=event(QEvent::MouseMove,{-500,100},Qt::NoButton,Qt::RightButton);
  assert(drag.move(left_wrap)==90);
  auto other_release=event(QEvent::MouseButtonRelease,{180,100},Qt::LeftButton,Qt::RightButton);
  assert(!drag.release(other_release)&&drag.active());
  auto release=event(QEvent::MouseButtonRelease,{190,100},Qt::RightButton,Qt::NoButton);
  assert(drag.release(release)==75&&!drag.active());
  assert(!drag.move(horizontal));
  assert(drag.begin(press,true,-175));
  assert(drag.move(backwards)==175);
  drag.finish();
  assert(!drag.move(horizontal)&&!drag.release(release));
  assert(drag.begin(press,true,60));
  auto lost_button=event(QEvent::MouseMove,{180,100},Qt::NoButton,Qt::NoButton);
  assert(!drag.move(lost_button)&&!drag.active());
  assert(!drag.begin(press,true,NAN));
  std::puts("Environment Shift+RMB: horizontal rotation, wrap, release, grab loss and navigation isolation PASS");
}
