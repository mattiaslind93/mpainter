// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialMultiObjectDemo.hh"
#include "NativeMaterialCaptureWorkflow.hh"
#include "FlattenLayer.hh"
#include "LayerCapture.hh"
#include <QElapsedTimer>
namespace bts {
inline QJsonObject verify_native_capture(BlenderHost &host, const std::function<void()> &frame)
{
  if (!qEnvironmentVariableIsSet("BTS_NATIVE_CAPTURE_BASELINE"))
    return verify_native_capture_workflow(host, frame);
  using namespace native_object_demo;
  const auto output = qEnvironmentVariable("BTS_NATIVE_MATERIAL_GPU_TEST"),
             library = output + "/native-capture-fixture.blend";
  std::async(std::launch::async, [&] { fixture(library, true); }).get();
  auto prepared = host.import_native_material(
                          library, "AW_BumpUnavailable", std::make_shared<std::atomic_bool>(false))
                      .get();
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  options.uv_workflow = UvWorkflow::PreserveTiles;
  options.blend.objects = {"AW_Object_A", "AW_Object_B"};
  const auto environment = qEnvironmentVariable("BTS_ENVIRONMENT");
  const auto imported = host.new_project(
      library.toStdString(), environment.toStdString(), &options);
  const auto scene = host.scene_snapshot();
  require(scene.mesh_objects == 2 && scene.materials.size() == 1,
          "Native capture fixture lost its two owners");
  TextureDocument document(options, imported, scene);
  auto &set = document.set(scene.materials.front().session_id);
  set.precision[0] = ChannelPrecision::RGB32F;
  set.precision[4] = ChannelPrecision::L32F;
  NativeMaterialSourceLoader loader(host, nullptr, [] {}, [](const QString &) {});
  QString failure;
  loader.prepared(prepared, [](const auto &) {}, [&](const QString &value) { failure = value; });
  const auto drain = [&] {
    QElapsedTimer clock;
    clock.start();
    do {
      frame();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
      require(clock.elapsed() < 120000, "Native capture source did not drain");
    } while (loader.pending() || host.texture_updates_pending());
    require(failure.isEmpty(), qPrintable(failure));
  };
  drain();
  const auto selected = apply_native_material_asset(set, prepared.material);
  host.set_selected_material(set.material_session_uid);
  host.apply_texture_set(set, 128);
  drain();
  const auto plan = prepare_flatten_layer(set, selected);
  std::fprintf(
      stderr,
      "BTS_NATIVE_CAPTURE_PLAN normal=%d bump=%d native_normal_supported=%d native_bump_supported=%d\n",
      plan.enabled[3],
      plan.enabled[4],
      native_material_channel_supported(prepared.material, 3),
      native_material_channel_supported(prepared.material, 4));
  for (int channel = 0; channel < 15; ++channel)
    if (plan.enabled[channel]) {
      const auto ticket = host.request_layer_capture(
          set, {selected, channel, 1001, 0, 0, 128, 128, 128, 128, false, false});
      QElapsedTimer clock;
      clock.start();
      std::optional<LayerCaptureResult> result;
      do {
        frame();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        result = host.layer_capture_result();
        require(clock.elapsed() < 120000, "Native capture packet did not finish");
      } while (!result || result->ticket != ticket);
      require(result->valid && result->pixels && result->pixels->size() == 128 * 128,
              "Native capture returned an invalid packet");
      if (channel == 0) {
        for (int x : {16, 80}) {
          const auto &value = result->pixels->at(32 * 128 + x);
          require(value[3] > .999f && std::isfinite(value[0]) && std::abs(value[0]) > 0,
                  "Native baseline Color capture lost owner pixels");
        }
        std::fprintf(stderr, "BTS_NATIVE_CAPTURE_COLOR_BASELINE gpu_packet=1 owners=2\n");
      }
    }
  return {{"baseline_probe", true}};
}
}  // namespace bts
