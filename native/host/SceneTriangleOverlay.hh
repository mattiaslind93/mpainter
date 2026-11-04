#pragma once
#include <QJsonObject>
#include <memory>
namespace blender::gpu {class Texture;}
namespace bts {
struct ViewFrame;
class SceneTriangleOverlay {
 struct Impl;std::unique_ptr<Impl> impl_;
 public:
 SceneTriangleOverlay();~SceneTriangleOverlay();
 void draw(const ViewFrame &,blender::gpu::Texture *depth,blender::gpu::Texture *color);
 void free();QJsonObject stats()const;
};
}
