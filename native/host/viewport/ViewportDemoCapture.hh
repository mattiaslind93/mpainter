// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportCapture.hh"
#include <QPainter>
#include <QPixmap>
namespace bts {
// Offscreen QWidget composition does not contain the private GL framebuffer.
// This is an explicit demo composite, not a grab of the user's display.
inline QImage capture_demo_window(QWidget *window, QWidget *viewport)
{
  QImage image = window->grab().toImage();
  const auto frame = capture_viewport_framebuffer(viewport);
  QPainter painter(&image);
  painter.drawImage(QRectF(viewport->mapTo(window, QPoint{}), viewport->size()), frame);
  return image;
}
}  // namespace bts
