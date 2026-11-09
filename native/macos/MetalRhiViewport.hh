#pragma once
#include <QImage>
#include <memory>
namespace blender::gpu {class Texture;}
namespace bts {
// Windowless staging surface for the Metal/Qt RHI boundary. It shares Blender's
// device and command queue; no CPU pixel upload is needed to present a frame.
// All calls and destruction must occur on the Blender GPU owner thread.
// This is not yet connected to the editor's QOpenGLWidget replacement.
class MetalRhiViewport {
 struct Impl;std::unique_ptr<Impl> impl_;
 public:
 MetalRhiViewport();
 ~MetalRhiViewport();
 MetalRhiViewport(const MetalRhiViewport &)=delete;
 // Diagnostic capture of the RHI-owned copy. The readback waits for completion;
 // a future widget must sample the texture directly instead of using capture.
 QImage capture(blender::gpu::Texture *source);
};
}
