#pragma once

#include <QImage>
#include <QOpenGLWidget>

namespace bts {
// QOpenGLWidget::grabFramebuffer is nonvirtual. A private offscreen viewport
// owns a different FBO and must capture it without initializing a second context.
class ViewportCapture {
 public:
  virtual ~ViewportCapture() = default;
  virtual QImage capture_framebuffer() = 0;
};
inline QImage capture_viewport_framebuffer(QWidget *viewport)
{
  if (auto *source = dynamic_cast<ViewportCapture *>(viewport))
    return source->capture_framebuffer();
  if (auto *gl = qobject_cast<QOpenGLWidget *>(viewport))
    return gl->grabFramebuffer();
  return {};
}
}  // namespace bts
