// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportCapture.hh"
#if defined(__APPLE__) || defined(Q_OS_MACOS)
#include <QWidget>
namespace bts {
// Raster overlays and the offscreen test surface do not require a GL context.
// The Metal host supplies frames through its QRhi bridge. Keep the type shared
// with acceptance drivers so they exercise the real editor on either backend.
class ViewportWidget : public QWidget {
 Q_OBJECT
 public:
 using QWidget::QWidget;
 virtual QImage grabFramebuffer() { return {}; }
 virtual bool isValid() const { return property("headlessGpuReady").toBool(); }
};
}
#else
#include <QOpenGLWidget>
namespace bts { using ViewportWidget = QOpenGLWidget; }
#endif
