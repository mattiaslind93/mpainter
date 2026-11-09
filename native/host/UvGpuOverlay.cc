#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include "SceneTriangleOverlay.hh"
#include "UvGpuOverlay.hh"
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
#include <QRegion>
#include <QRectF>
#include <algorithm>
#include <array>
namespace bts {
using namespace blender;
using Edges = std::vector<std::array<float, 4>>;
struct UvGpuOverlay::Impl {
  struct Buffer {
    std::shared_ptr<const Edges> source;
    blender::gpu::Batch *batch = nullptr;
    void clear()
    {
      if (batch)
        GPU_batch_discard(batch);
      batch = nullptr;
      source.reset();
    }
  };
  std::array<Buffer, 3> buffers;
  std::shared_ptr<const Edges> generation;
  std::uint64_t builds = 0, bytes = 0, draws = 0, hits = 0;
  void clear()
  {
    for (auto &buffer : buffers)
      buffer.clear();
    generation.reset();
  }
};
UvGpuOverlay::UvGpuOverlay() : impl_(std::make_unique<Impl>()) {}
UvGpuOverlay::~UvGpuOverlay() =
    default;  // Owner releases GPU buffers before native context teardown.
void UvGpuOverlay::free()
{
  impl_->clear();
}
QJsonObject UvGpuOverlay::stats() const
{
  const auto &s = *impl_;
  return {{"uv_vbo_builds", qint64(s.builds)},
          {"uv_vbo_upload_bytes", qint64(s.bytes)},
          {"uv_draws", qint64(s.draws)},
          {"uv_cache_hits", qint64(s.hits)}};
}
static void verify_overlay_render(const QString &directory);
void UvGpuOverlay::draw(const UvPreviewSummary &summary, const ViewFrame &frame)
{
  static const auto test_directory = qEnvironmentVariable("BTS_OVERLAY_SELF_TEST_DIR");
  static bool tested = false;
  if (!tested && !test_directory.isEmpty()) {
    tested = true;
    verify_overlay_render(test_directory);
  }
  performance::Scope timing("gpu_uv_overlay_submit");
  auto &s = *impl_;
  if (!summary.contours)
    return;
  if (s.generation != summary.contours) {
    s.clear();
    s.generation = summary.contours;
  }
  const int kind = frame.wireframe ? (frame.wireframe_triangles ? 2 : 1) : 0;
  const auto source = kind == 2 ? summary.triangulated_wireframe :
                      kind == 1 ? summary.wireframe :
                                  summary.contours;
  if (!source || source->empty())
    return;
  auto &buffer = s.buffers[kind];
  if (buffer.source != source) {
    performance::Scope build_timing("gpu_uv_overlay_build");
    buffer.clear();
    GPUVertFormat format{};
    const auto pos = GPU_vertformat_attr_add(
        &format, "pos", blender::gpu::VertAttrType::SFLOAT_32_32);
    auto *vertices = GPU_vertbuf_create_with_format(format);
    GPU_vertbuf_data_alloc(*vertices, uint(source->size() * 2));
    GPU_vertbuf_attr_fill(vertices, pos, source->data());
    buffer.batch = GPU_batch_create_ex(GPU_PRIM_LINES, vertices, nullptr, GPU_BATCH_OWNS_VBO);
    buffer.source = source;
    ++s.builds;
    s.bytes += source->size() * sizeof(Edges::value_type);
  }
  else
    ++s.hits;
  const int width = frame.pane.width, height = frame.pane.height;
  QRegion visible(QRect(0, 0, width, height));
  if (!frame.hidden_uv_tiles.empty()) {
    const double pixels = height / frame.uv_extent;
    const auto point = [&](double u, double v) {
      return QPointF(width * .5 + (u - frame.uv_center[0]) * pixels,
                     height * .5 - (v - frame.uv_center[1]) * pixels);
    };
    for (int tile : frame.hidden_uv_tiles) {
      const int number = tile - 1001;
      const auto rect = QRectF(point(number % 10, number / 10),
                               point(number % 10 + 1, number / 10 + 1))
                            .normalized()
                            .toAlignedRect();
      if (rect.intersects(visible.boundingRect()))
        visible -= rect;
    }
  }
  if (visible.isEmpty())
    return;
  GPU_matrix_push();
  GPU_matrix_push_projection();
  GPU_matrix_set(reinterpret_cast<const float (*)[4]>(frame.view.data()));
  GPU_matrix_projection_set(reinterpret_cast<const float (*)[4]>(frame.projection.data()));
  const auto blend = GPU_blend_get();
  const auto depth = GPU_depth_test_get();
  const bool depth_write = GPU_depth_mask_get();
  GPU_blend(GPU_BLEND_ALPHA);
  GPU_depth_test(GPU_DEPTH_NONE);
  GPU_depth_mask(false);
  GPU_viewport(0, 0, width, height);
  GPU_scissor_test(true);
  GPU_batch_program_set_builtin(buffer.batch, GPU_SHADER_3D_POLYLINE_UNIFORM_COLOR);
  const float viewport[2]{float(width), float(height)};
  GPU_shader_uniform_2fv(buffer.batch->shader, "viewportSize", viewport);
  GPU_shader_uniform_1f(
      buffer.batch->shader, "lineWidth", std::max(.0f, frame.overlay_line_width - .5f));
  const float dark[4]{35 / 255.f, 35 / 255.f, 35 / 255.f, 200 / 255.f},
      orange[4]{232 / 255.f, 107 / 255.f, 72 / 255.f, 150 / 255.f};
  GPU_shader_uniform_4fv(buffer.batch->shader, "color", frame.wireframe ? orange : dark);
  for (const QRect &rect : visible) {
    GPU_scissor(rect.x(), height - rect.bottom() - 1, rect.width(), rect.height());
    GPU_batch_draw(buffer.batch);
    ++s.draws;
  }
  GPU_scissor_test(false);
  GPU_depth_mask(depth_write);
  GPU_depth_test(depth);
  GPU_blend(blend);
  GPU_matrix_pop_projection();
  GPU_matrix_pop();
}
static void verify_overlay_render(const QString &directory)
{
  const auto require = [](bool ok, const char *message) {
    if (!ok)
      throw std::runtime_error(message);
  };
  auto *previous = GPU_framebuffer_active_get();
  int previous_viewport[4];
  GPU_viewport_size_get_i(previous_viewport);
  constexpr int width = 256, height = 128;
  char error[256]{};
  auto *offscreen = GPU_offscreen_create(width,
                                         height,
                                         false,
                                         gpu::TextureFormat::UNORM_8_8_8_8,
                                         GPU_TEXTURE_USAGE_GENERAL,
                                         true,
                                         error);
  require(offscreen, "Overlay test color allocation failed");
  auto *color = GPU_offscreen_color_texture(offscreen);
  auto *depth = GPU_texture_create_2d("BTS overlay occlusion test",
                                      width,
                                      height,
                                      1,
                                      gpu::TextureFormat::UNORM_16_DEPTH,
                                      GPU_TEXTURE_USAGE_GENERAL,
                                      nullptr);
  gpu::FrameBuffer *depth_fb = nullptr;
  GPU_framebuffer_ensure_config(&depth_fb, {GPU_ATTACHMENT_TEXTURE(depth)});
  GPU_framebuffer_bind(depth_fb);
  GPU_framebuffer_clear_depth(depth_fb, .5f);
  GPU_offscreen_bind(offscreen, false);
  auto *target = GPU_framebuffer_active_get();
  const auto clear = [&] {
    GPU_framebuffer_bind(target);
    GPU_framebuffer_clear_color(target, {0, 0, 0, 1});
  };
  const auto pixels = [&](const char *name) {
    std::vector<unsigned char> rgba(width * height * 4);
    GPU_framebuffer_read_color(target, 0, 0, width, height, 4, 0, GPU_DATA_UBYTE, rgba.data());
    QImage(rgba.data(), width, height, QImage::Format_RGBA8888)
        .mirrored()
        .save(directory + "/" + name + ".png");
    return rgba;
  };
  const auto count = [&](const auto &rgba, int x0, int x1) {
    int total = 0;
    for (int y = 0; y < height; ++y)
      for (int x = x0; x < x1; ++x) {
        const int i = (y * width + x) * 4;
        total += rgba[i] > 4 || rgba[i + 1] > 4 || rgba[i + 2] > 4;
      }
    return total;
  };
  ViewFrame frame{{ViewKind::Scene, 0, 0, width, height}, {}, {}};
  frame.view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  frame.projection = frame.view;
  frame.wireframe = frame.wireframe_triangles = true;
  SceneSnapshot behind;
  behind.object_names[1] = "occlusion";
  UvFaceTriangle triangle;
  triangle.positions = {{{-.7f, -.7f, .5f}, {.7f, -.7f, .5f}, {0, .7f, .5f}}};
  behind.uv_triangles.push_back(triangle);
  frame.wire_source = &behind;
  SceneTriangleOverlay triangles;
  clear();
  triangles.draw(frame, depth, color);
  const auto hidden_pixels = pixels("triangle-hidden");
  require(count(hidden_pixels, 0, width) == 0, "Hidden triangle edges leaked through depth");
  SceneSnapshot front = behind;
  for (auto &p : front.uv_triangles[0].positions)
    p[2] = -.5f;
  frame.wire_source = &front;
  clear();
  triangles.draw(frame, depth, color);
  const auto front_pixels = pixels("triangle-front");
  const int front_count = count(front_pixels, 0, width);
  require(front_count > 50, "Visible triangle edges were not rendered");
  const auto triangle_uploads = triangles.stats()["triangle_vbo_builds"].toInteger();
  for (int i = 0; i < 20; ++i) {
    clear();
    frame.view[12] = float(i) * .001f;
    triangles.draw(frame, depth, color);
  }
  require(triangles.stats()["triangle_vbo_builds"].toInteger() == triangle_uploads,
          "Camera change uploaded triangle vertices");
  frame.view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  frame.projection = {1, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1};
  frame.pane.kind = ViewKind::Uv;
  frame.uv_center = {1, .5};
  frame.uv_extent = 1;
  frame.wireframe = false;
  UvPreviewSummary summary;
  auto edges = std::make_shared<Edges>();
  edges->push_back({.2f, .5f, .8f, .5f});
  edges->push_back({1.2f, .5f, 1.8f, .5f});
  summary.contours = summary.wireframe = summary.triangulated_wireframe = edges;
  UvGpuOverlay uv;
  clear();
  uv.draw(summary, frame);
  const auto both = pixels("uv-both-tiles");
  require(count(both, 0, width / 2) > 20 && count(both, width / 2, width) > 20,
          "GPU UV lines missed a tile");
  frame.hidden_uv_tiles = {1002};
  clear();
  uv.draw(summary, frame);
  const auto first = pixels("uv-hidden-1002");
  require(count(first, 0, width / 2) > 20 && count(first, width / 2, width) == 0,
          "Hidden UV tile lines were not clipped");
  for (int i = 0; i < 20; ++i) {
    frame.view[12] = float(i) * .001f;
    uv.draw(summary, frame);
  }
  require(uv.stats()["uv_vbo_builds"].toInteger() == 1, "UV pan uploaded vertex data");
  QFile proof(directory + "/gpu-overlay-self-test.json");
  require(proof.open(QIODevice::WriteOnly), "Overlay self-test proof failed");
  proof.write(QJsonDocument(QJsonObject{{"occluded_pixels", 0},
                                        {"visible_pixels", front_count},
                                        {"triangle_uploads", triangle_uploads},
                                        {"uv_uploads", 1},
                                        {"camera_frames_without_upload", 20},
                                        {"hidden_uv_tile", 1002},
                                        {"test_only_readbacks", 4},
                                        {"production_depth_readbacks", 0}})
                  .toJson());
  uv.free();
  triangles.free();
  GPU_framebuffer_free(depth_fb);
  GPU_texture_free(depth);
  GPU_offscreen_free(offscreen);
  GPU_framebuffer_bind(previous);
  GPU_viewport(
      previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
  std::puts(
      "BTS_GPU_OVERLAY_SELF_TEST depth_occlusion=pass hidden_uv_tiles=pass cached_camera=pass");
}

}  // namespace bts
