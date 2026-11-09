/* SPDX-License-Identifier: GPL-2.0-or-later */
// Explicit CLI diagnostic. No widgets, NSWindow, swapchain or presentation calls.
#include <QGuiApplication>
#include "MetalRhiViewport.hh"
#include <QDir>
#include <QImage>
#include <QMatrix4x4>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <cstdio>
#include <stdexcept>
#include <memory>

#include "BKE_context.hh"
#include "BKE_global.hh"
#include "BKE_scene.hh"
#include "BKE_wm_runtime.hh"
#include "BLI_listbase.h"
#include "DNA_scene_types.h"
#include "BLI_string.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"
#include "DNA_windowmanager_types.h"
#include "ED_screen.hh"
#include "ED_view3d_offscreen.hh"
#include "GPU_viewport.hh"
#include "BPY_extern_run.hh"
#include "DRW_engine.hh"
#include "GHOST_ContextMTL.hh"
#include "MetalContextOwner.hh"
#include "GPU_context.hh"
#include "GPU_framebuffer.hh"
#include "GPU_init_exit.hh"
#include "GPU_platform.hh"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include "WM_api.hh"
#include "GpuBakeRays.hh"
#include "MetalOperationsTest.hh"

int BTS_blender_init(int, const char **, blender::bContext **);
namespace blender { void BTS_appdir_documents_path_set(const char *); }

static void require(bool ok, const char *message)
{
  if (!ok) throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if(qEnvironmentVariable("QT_QPA_PLATFORM")!="offscreen") {
    std::fprintf(stderr,"Metal probe requires QT_QPA_PLATFORM=offscreen\n");return 2;
  }
  QGuiApplication app(argc, argv);
  if (argc != 3 || std::string(argv[1]) != "--self-test") {
    std::fprintf(stderr, "Usage: mpainter-metal-probe --self-test OUTPUT_DIRECTORY\n");
    return 2;
  }
  const QString output = QDir(argv[2]).absolutePath();
  if (!QDir().mkpath(output)) return 2;
  @autoreleasepool {
    blender::bContext *context = nullptr;
    std::unique_ptr<bts::GpuContextOwner> ghost;
    bool gpu_active = false;
    try {
      using namespace blender;
      id<MTLDevice> preflight_device = MTLCreateSystemDefaultDevice();
      require(preflight_device != nil, "No Metal device available");
      [preflight_device release];
      BTS_appdir_documents_path_set(output.toUtf8().constData());
      const QByteArray executable = QCoreApplication::applicationFilePath().toUtf8();
      const char *args[] = {executable.constData(), "--background", "--factory-startup",
                           "--disable-autoexec", "--threads", "6", "--gpu-backend", "metal"};
      require(BTS_blender_init(int(std::size(args)), args, &context) == 0 && context,
              "Embedded Blender initialization failed");
      require(G.background, "Blender must remain in background mode");
      require(BPY_run_string_exec(context, nullptr,
              "import bpy\nassert bpy.app.background\nassert bpy.app.version[:2] == (5, 2)\n"
              "print('MPAINTER_BLENDER_RUNTIME_OK', bpy.app.version_string, flush=True)\n"),
              "Embedded Python runtime failed");

      ghost = std::make_unique<bts::MetalContextOwner>();
      require(ghost->ghost_context()->activateDrawingContext() == GHOST_kSuccess, "Metal activation failed");
      GPU_backend_type_selection_set(ghost->backend());
      WM_init_gpu_hosted(ghost->ghost_context());
      DRW_gpu_context_enable_ex(false);
      gpu_active = true;
      require(GPU_backend_get_type() == GPU_BACKEND_METAL, "Wrong GPU backend");
      const QString renderer = QString::fromUtf8(GPU_platform_renderer());
      std::printf("MPAINTER_METAL_READY renderer=%s\n", renderer.toUtf8().constData());

      bts::verify_metal_brush();
      bts::verify_metal_polygon();
      bts::verify_gpu_texture_probe();
      bts::verify_metal_metadata();
      bts::verify_metal_project_readback();
      bts::verify_metal_margin_tables();

      // Exercise the actual production BVH kernel against a simple analytic fixture.
      // Every forward ray hits a large plane at z=.5, while backward rays miss.
      const std::vector<bts::bake_bvh::Triangle> triangles = {
        {{{-100,-100,.5f},{100,-100,.5f},{100,100,.5f}}},
        {{{-100,-100,.5f},{100,100,.5f},{-100,100,.5f}}}};
      std::vector<bts::FillSurfacePoint> points(4);
      for (auto &point : points) point.valid = true;
      bts::GpuBakeRayStats stats;
      constexpr int rays = 32;
      const auto hit = bts::gpu_bake_cosine_occlusion(triangles, points, rays, 2.f, false, &stats);
      const auto miss = bts::gpu_bake_cosine_occlusion(triangles, points, rays, 2.f, true);
      float expected = 0;
      for (int i = 0; i < rays; ++i) {
        const float z = std::sqrt(float(i + .5f) / rays);
        expected += std::min(1.f, (.5f - 2e-5f) / (2.f * z)) / rays;
      }
      for (size_t i = 0; i < hit.size(); ++i) {
        std::printf("MPAINTER_BAKE_SAMPLE point=%zu expected=%.8f actual=%.8f miss=%.8f\n", i, expected, hit[i], miss[i]);
        require(std::isfinite(hit[i]) && std::abs(hit[i] - expected) < 2e-4f,
                "Metal bake differs from analytic plane result");
        require(std::abs(miss[i] - 1.f) < 1e-6f, "Metal bake miss differs from analytic result");
      }
      std::printf("MPAINTER_METAL_BAKE_OK points=4 rays=32 expected=%.8f actual=%.8f\n", expected, hit[0]);

      // GPU render target -> readback -> Qt image is a conservative presentation bridge.
      // This is a diagnostic GPU image, never a claim of editor viewport parity.
      constexpr int width = 256, height = 128;
      char error[256]{};
      GPUOffScreen *target = GPU_offscreen_create(width, height, false,
          gpu::TextureFormat::UNORM_8_8_8_8, GPU_TEXTURE_USAGE_GENERAL, true, error);
      require(target, error);
      const float color[] = {.125f, .375f, .75f, 1.f};
      GPU_texture_clear(GPU_offscreen_color_texture(target), GPU_DATA_FLOAT, color);
      GPU_finish();
      QImage image(width, height, QImage::Format_RGBA8888);
      GPU_offscreen_read_color(target, GPU_DATA_UBYTE, image.bits());
      const auto pixel = image.pixelColor(32, 32);
      require(std::abs(pixel.red() - 32) <= 1 && std::abs(pixel.green() - 96) <= 1 &&
              std::abs(pixel.blue() - 191) <= 1 && pixel.alpha() == 255,
              "Metal framebuffer readback pixel mismatch");
      require(image.save(output + "/metal-readback.png"), "Could not save GPU readback");
      GPU_offscreen_free(target);
      // Render the factory cube through the same Blender offscreen entry point
      // used by BlenderHost::draw, but with no Qt GL framebuffer or GL blit.
      auto *wm = CTX_wm_manager(context);
      require(wm && !wm->windows.is_empty(), "No startup workspace");
      auto *window = static_cast<wmWindow *>(wm->windows.first);
      require(window->runtime->ghostwin == nullptr, "Unexpected native window");
      CTX_wm_window_set(context, window);
      auto *screen = CTX_wm_screen(context);
      View3D *view3d = nullptr;
      ARegion *region = nullptr;
      for (ScrArea &area : screen->areabase) {
        if (area.spacetype != SPACE_VIEW3D) continue;
        ED_area_and_region_types_init(&area);
        for (ARegion &candidate : area.regionbase) {
          if (candidate.regiontype == RGN_TYPE_WINDOW && candidate.regiondata) {
            CTX_wm_area_set(context, &area);
            CTX_wm_region_set(context, &candidate);
            view3d = CTX_wm_view3d(context);
            region = &candidate;
            break;
          }
        }
        if (view3d) break;
      }
      require(view3d && region, "No startup View3D region");
      auto *scene = CTX_data_scene(context);
      auto *main = CTX_data_main(context);
      auto *layer = CTX_data_view_layer(context);
      STRNCPY(scene->r.engine, "BLENDER_EEVEE");
      view3d->shading.type = OB_MATERIAL;
      view3d->flag2 |= V3D_HIDE_OVERLAYS;
      auto *depsgraph = BKE_scene_ensure_depsgraph(main, scene, layer);
      BKE_scene_graph_evaluated_ensure(depsgraph, main);
      constexpr int render_width = 640, render_height = 480;
      auto *render_target = GPU_offscreen_create(render_width, render_height, true,
          gpu::TextureFormat::UNORM_8_8_8_8, GPU_TEXTURE_USAGE_GENERAL, true, error);
      require(render_target, error);
      auto *viewport = GPU_viewport_create();
      QMatrix4x4 view, projection;
      view.lookAt({5, -7, 5}, {0, 0, 0}, {0, 0, 1});
      projection.perspective(45, float(render_width) / render_height, .1f, 100.f);
      // Several samples also exercise persistent viewport reuse on Metal.
      for (int frame = 0; frame < 4; ++frame) {
        GPU_viewport_tag_update(viewport);
        GPU_offscreen_bind(render_target, true);
        ED_view3d_draw_offscreen(depsgraph, scene, OB_MATERIAL, view3d, region, nullptr,
          render_width, render_height,
          reinterpret_cast<const float (*)[4]>(view.constData()),
          reinterpret_cast<const float (*)[4]>(projection.constData()),
          false, true, "", true, true, render_target, viewport);
        GPU_offscreen_unbind(render_target, true);
      }
      GPU_finish();
      QImage rendered(render_width, render_height, QImage::Format_RGBA8888);
      GPU_offscreen_read_color(render_target, GPU_DATA_UBYTE, rendered.bits());
      rendered = rendered.mirrored(false, true);
      const QColor corner = rendered.pixelColor(0, 0);
      int differing = 0;
      for (int y = 0; y < render_height; ++y)
        for (int x = 0; x < render_width; ++x)
          differing += rendered.pixelColor(x, y) != corner;
      require(differing > 1000, "Offscreen viewport rendered a uniform image");
      require(rendered.save(output + "/embedded-viewport.png"), "Cannot save viewport image");
      {
        bts::MetalRhiViewport bridge;
        auto rhi_image=bridge.capture(GPU_offscreen_color_texture(render_target)).mirrored(false,true);
        require(rhi_image==rendered,"Qt RHI texture import/copy differs from Blender readback");
        require(rhi_image.save(output + "/metal-rhi-viewport.png"),"Cannot save RHI capture");
        // Exercise queue ordering with a producer operation still pending.
        // No diagnostic GPU_finish/readback is allowed between clear and copy.
        for(int frame=0;frame<3;++frame){
          const float clear[4]={frame*.25f,.5f,.75f,1};
          GPU_texture_clear(GPU_offscreen_color_texture(render_target),GPU_DATA_FLOAT,clear);
          const auto queued=bridge.capture(GPU_offscreen_color_texture(render_target));
          const auto pixel=queued.pixelColor(20,20);
          require(std::abs(pixel.red()-int(std::round(clear[0]*255)))<=1&&
                  std::abs(pixel.green()-128)<=1&&std::abs(pixel.blue()-191)<=1,
                  "RHI read a stale producer revision");
        }
        std::puts("MPAINTER_METAL_RHI_PASS shared_queue=1 pending_producer=1 cpu_upload=0 visible_windows=0");
      }
      GPU_viewport_free(viewport);
      GPU_offscreen_free(render_target);
      std::puts("MPAINTER_METAL_VIEWPORT_OK frames=4 engine=EEVEE visible_windows=0");
      DRW_gpu_context_disable_ex(false);
      gpu_active = false;
      QJsonObject result{{"diagnostic_only", true}, {"blender_runtime", true},
                         {"metal_backend", true}, {"renderer", renderer},
                         {"bvh_bake", true}, {"framebuffer_readback", true},
                         {"brush_cpu_parity_cases", 400}, {"polygon_fragment_parity_cases", 60}, {"texture_views", true},
                         {"async_metadata", true}, {"project_gpu_snapshot_packets", true},
                         {"metal_qt_rhi_copy", true}, {"rhi_pixel_exact", true},
                         {"full_project_save_open", false},
                         {"embedded_eevee_viewport", true}, {"editor_viewport", false}, {"visible_windows", 0},
                         {"bake_expected", expected}, {"bake_actual", hit[0]},
                         {"bake_dispatches", double(stats.dispatches)}};
      QFile report(output + "/metal-probe.json");
      require(report.open(QIODevice::WriteOnly), "Cannot write report");
      report.write(QJsonDocument(result).toJson());
      report.close();
      // Runtime owns the draw context, and must release it before GHOST.
      WM_exit_ex(context, true, false);
      context = nullptr;
      ghost.reset();
      std::puts("MPAINTER_METAL_PROBE_PASS");
      return 0;
    }
    catch (const std::exception &error) {
      std::fprintf(stderr, "MPAINTER_METAL_PROBE_FAIL %s\n", error.what());
      if (gpu_active) blender::DRW_gpu_context_disable_ex(false);
      if (context) blender::WM_exit_ex(context, true, false);
      ghost.reset();
      return 1;
    }
  }
}
