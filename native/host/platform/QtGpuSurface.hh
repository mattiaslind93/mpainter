#pragma once
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QSurface>
#include <functional>
namespace bts {
// The renderer needs a current context and target, not a visible window.
// This keeps offscreen verification on the same GHOST/GPU path as the app.
class QtGpuSurface {
  QOpenGLWidget *widget_ = nullptr;
  QOpenGLContext *context_ = nullptr;
  QSurface *surface_ = nullptr;
  unsigned framebuffer_ = 0;
  std::function<unsigned()> framebuffer_provider_;

 public:
  explicit QtGpuSurface(QOpenGLWidget &widget) : widget_(&widget) {}
  QtGpuSurface(QOpenGLContext &context, QSurface &surface, unsigned framebuffer)
      : context_(&context), surface_(&surface), framebuffer_(framebuffer)
  {
  }
  QtGpuSurface(QOpenGLContext &context, QSurface &surface, std::function<unsigned()> framebuffer)
      : context_(&context), surface_(&surface), framebuffer_provider_(std::move(framebuffer))
  {
  }
  void makeCurrent()
  {
    if (widget_)
      widget_->makeCurrent();
    else
      context_->makeCurrent(surface_);
  }
  void doneCurrent()
  {
    if (widget_)
      widget_->doneCurrent();
    else
      context_->doneCurrent();
  }
  QOpenGLContext *context() const
  {
    return widget_ ? widget_->context() : context_;
  }
  unsigned defaultFramebufferObject() const
  {
    return widget_               ? widget_->defaultFramebufferObject() :
           framebuffer_provider_ ? framebuffer_provider_() :
                                   framebuffer_;
  }
};
}  // namespace bts
