// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneTriangleOverlay.hh"
#include "BlenderHost.hh"
#include "Performance.hh"
#include "GPU_batch.hh"
#include "GPU_vertex_buffer.hh"
#include "GPU_vertex_format.hh"
#include "GPU_shader_builtin.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_matrix.hh"
#include "GPU_framebuffer.hh"
#include "GPU_texture.hh"
namespace bts {
using namespace blender;
struct SceneTriangleOverlay::Impl {
  gpu::Batch *batch = nullptr;
  gpu::FrameBuffer *target = nullptr, *depth_source = nullptr;
  gpu::Texture *resized_depth = nullptr;
  const UvFaceTriangle *data = nullptr;
  std::size_t count = 0;
  std::uint32_t identity = 0;
  std::uint64_t builds = 0, bytes = 0, draws = 0;
  void clear()
  {
    if (batch)
      GPU_batch_discard(batch);
    batch = nullptr;
    if (target)
      GPU_framebuffer_free(target);
    target = nullptr;
    if (depth_source)
      GPU_framebuffer_free(depth_source);
    depth_source = nullptr;
    if (resized_depth)
      GPU_texture_free(resized_depth);
    resized_depth = nullptr;
    data = nullptr;
    count = 0;
    identity = 0;
  }
};
SceneTriangleOverlay::SceneTriangleOverlay() : impl_(std::make_unique<Impl>()) {}
SceneTriangleOverlay::~SceneTriangleOverlay() = default;
void SceneTriangleOverlay::free()
{
  impl_->clear();
}
QJsonObject SceneTriangleOverlay::stats() const
{
  const auto &s = *impl_;
  return {{"triangle_vbo_builds", qint64(s.builds)},
          {"triangle_upload_bytes", qint64(s.bytes)},
          {"triangle_draws", qint64(s.draws)}};
}
void SceneTriangleOverlay::draw(const ViewFrame &frame, gpu::Texture *depth, gpu::Texture *color)
{
  performance::Scope submit("gpu_triangle_overlay_submit");
  auto &s = *impl_;
  const auto *scene = frame.wire_source;
  if (!scene || scene->uv_triangles.empty() || !depth || !color)
    return;
  const auto &triangles = scene->uv_triangles;
  const auto identity = scene->object_names.empty() ? 0 : scene->object_names.begin()->first;
  if (s.data != triangles.data() || s.count != triangles.size() || s.identity != identity) {
    performance::Scope build("gpu_triangle_overlay_build");
    if (s.batch)
      GPU_batch_discard(s.batch);
    s.batch = nullptr;
    std::vector<std::array<float, 3>> positions;
    positions.reserve(triangles.size() * 6);
    for (const auto &triangle : triangles)
      for (int k = 0; k < 3; ++k) {
        positions.push_back(triangle.positions[k]);
        positions.push_back(triangle.positions[(k + 1) % 3]);
      }
    GPUVertFormat format{};
    const auto pos = GPU_vertformat_attr_add(&format, "pos", gpu::VertAttrType::SFLOAT_32_32_32);
    auto *vertices = GPU_vertbuf_create_with_format(format);
    GPU_vertbuf_data_alloc(*vertices, uint(positions.size()));
    GPU_vertbuf_attr_fill(vertices, pos, positions.data());
    s.batch = GPU_batch_create_ex(GPU_PRIM_LINES, vertices, nullptr, GPU_BATCH_OWNS_VBO);
    s.data = triangles.data();
    s.count = triangles.size();
    s.identity = identity;
    ++s.builds;
    s.bytes += positions.size() * sizeof(positions[0]);
    if (qEnvironmentVariableIsSet("BTS_OVERLAY_PROFILE"))
      std::printf("BTS_GPU_TRIANGLE_VBO_BUILD triangles=%zu bytes=%zu\n",
                  triangles.size(),
                  positions.size() * sizeof(positions[0]));
  }
  auto *previous = GPU_framebuffer_active_get();
  const int width = frame.pane.width, height = frame.pane.height;
  auto *test_depth = depth;
  if (GPU_texture_width(depth) != width || GPU_texture_height(depth) != height) {
    if (!s.resized_depth || GPU_texture_width(s.resized_depth) != width ||
        GPU_texture_height(s.resized_depth) != height ||
        GPU_texture_format(s.resized_depth) != GPU_texture_format(depth))
    {
      if (s.target) {
        GPU_framebuffer_free(s.target);
        s.target = nullptr;
      }
      if (s.resized_depth)
        GPU_texture_free(s.resized_depth);
      s.resized_depth = GPU_texture_create_2d("BTS triangle overlay depth",
                                              width,
                                              height,
                                              1,
                                              GPU_texture_format(depth),
                                              GPU_TEXTURE_USAGE_GENERAL,
                                              nullptr);
    }
    GPU_framebuffer_ensure_config(&s.depth_source, {GPU_ATTACHMENT_TEXTURE(depth)});
    GPU_framebuffer_ensure_config(
        &s.target, {GPU_ATTACHMENT_TEXTURE(s.resized_depth), GPU_ATTACHMENT_TEXTURE(color)});
    GPU_framebuffer_blit(s.depth_source, 0, s.target, 0, GPU_DEPTH_BIT);
    test_depth = s.resized_depth;
  }
  GPU_framebuffer_ensure_config(
      &s.target, {GPU_ATTACHMENT_TEXTURE(test_depth), GPU_ATTACHMENT_TEXTURE(color)});
  GPU_framebuffer_bind(s.target);
  GPU_matrix_push();
  GPU_matrix_push_projection();
  GPU_matrix_set(reinterpret_cast<const float (*)[4]>(frame.view.data()));
  auto projection = frame.projection;
  for (int column = 0; column < 4; ++column)
    projection[column * 4 + 2] -= 1e-6f * projection[column * 4 + 3];
  GPU_matrix_projection_set(reinterpret_cast<const float (*)[4]>(projection.data()));
  const auto blend = GPU_blend_get();
  const auto depth_mode = GPU_depth_test_get();
  const bool depth_write = GPU_depth_mask_get();
  GPU_blend(GPU_BLEND_ALPHA);
  GPU_depth_test(GPU_DEPTH_LESS_EQUAL);
  GPU_depth_mask(false);
  GPU_scissor_test(false);
  GPU_viewport(0, 0, width, height);
  GPU_batch_program_set_builtin(s.batch, GPU_SHADER_3D_POLYLINE_UNIFORM_COLOR);
  const float size[2]{float(width), float(height)},
      color_value[4]{232 / 255.f, 107 / 255.f, 72 / 255.f, 150 / 255.f};
  GPU_shader_uniform_2fv(s.batch->shader, "viewportSize", size);
  GPU_shader_uniform_1f(
      s.batch->shader, "lineWidth", std::max(.0f, frame.overlay_line_width - .5f));
  GPU_shader_uniform_4fv(s.batch->shader, "color", color_value);
  GPU_batch_draw(s.batch);
  ++s.draws;
  GPU_depth_mask(depth_write);
  GPU_depth_test(depth_mode);
  GPU_blend(blend);
  GPU_matrix_pop_projection();
  GPU_matrix_pop();
  GPU_framebuffer_bind(previous);
}
}  // namespace bts
