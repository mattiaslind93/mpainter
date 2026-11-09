// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include <QCommandLineParser>
#include <QString>
#include <QtGlobal>

#include "BakeSourceCpuDemo.hh"
#include "BlenderHost.hh"
#include "ExportCpuDemo.hh"
#include "NativeMaterialInspectionDemo.hh"
#include "PolyHavenPreparation.hh"
#include "RenderStatusTest.hh"
#include "SkewBakeCpuDemo.hh"
#include "TextureExtent.hh"
#include "TextureImportCpuDemo.hh"
#include "ViewportHeadlessDemo.hh"
#include "WorkspaceShell.hh"

// Defined in native/tests and linked into the app for the CPU checks.
namespace bts { int run_workspace_shutdown_test(); int run_asset_workspace_drop_test(const QString &); }

namespace bts::harness {

// Harness runs that main() hands over right after parsing the command line. An exit code means
// the run is over: an argument error or a test that ran without Blender. Otherwise the app starts.
inline std::optional<int> run_pre_host_test(const QCommandLineParser &parser)
{
  if (parser.isSet("gui-fix-resolution")) {
    bool okay=false;
    const int size=parser.value("gui-fix-resolution").toInt(&okay);
    if (!okay || size<128 || size>bts::max_texture_resolution || (size&(size-1))) {
      qCritical("--gui-fix-resolution must be a power of two from 128 to 8192 (8K)");
      return 2;
    }
  }
  if(parser.isSet("brush-presets-demo")){if(parser.isSet("gui-fix-demo")||parser.isSet("material-layering-demo")){qCritical("Choose one diagnostic actor");return 2;}qputenv("BTS_BO_PRESET_DEMO",parser.value("brush-presets-demo").toUtf8());}
  if(parser.isSet("material-layering-demo")&&parser.isSet("gui-fix-demo")){qCritical("Choose one diagnostic actor");return 2;}
  if(parser.isSet("cpu-asset-dragdrop-test"))return bts::run_asset_workspace_drop_test(parser.value("cpu-asset-dragdrop-test"));
  if(parser.isSet("cpu-render-status-test"))return bts::run_render_status_test();
  if(parser.isSet("cpu-workspace-shutdown-test"))return bts::run_workspace_shutdown_test();
  if(parser.isSet("ac-ui-capture-demo"))return bts::capture_ac_panels(parser.value("ac-ui-capture-demo"));
  return std::nullopt;
}

// Harness runs that need the loaded Blender host but no workspace or viewport.
inline std::optional<int> run_host_test(bts::BlenderHost &host, const QCommandLineParser &parser)
{
  if(parser.isSet("headless-cycles-cuda-test")){host.diagnostic_cycles_cuda_render(parser.value("headless-cycles-cuda-test"));return 0;}
  if(parser.isSet("headless-viewport-test"))return bts::run_viewport_headless_demo(host,parser.value("headless-viewport-test"),parser.value("open-document"));
  if(parser.isSet("cpu-prepare-polyhaven"))return bts::prepare_polyhaven(host,parser.value("cpu-prepare-polyhaven"),parser.value("polyhaven-source"));
  if(parser.isSet("cpu-native-material-test"))return bts::run_native_material_inspection_demo(host,parser.value("cpu-native-material-test"));
  if(parser.isSet("cpu-bake-source-test"))return bts::run_bake_source_cpu_demo(host,parser.value("cpu-bake-source-test"));
  if(parser.isSet("headless-surface-bake-test"))return bts::run_bake_source_cpu_demo(host,parser.value("headless-surface-bake-test"));
  if(parser.isSet("cpu-skew-test"))return bts::run_skew_bake_cpu_demo(parser.value("cpu-skew-test"));
  if(parser.isSet("cpu-texture-import-test"))return bts::run_texture_import_cpu_demo(parser.value("cpu-texture-import-test"));
  if(parser.isSet("cpu-export-test"))return bts::run_export_cpu_demo(parser.value("cpu-export-test"));
  return std::nullopt;
}

}  // namespace bts::harness
