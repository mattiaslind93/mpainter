// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "DerivedOutput.hh"
#include "ExportPacking.hh"
#include "ExportPadding.hh"
#include "ExportWorkflow.hh"
#include "ExportNormalToBump.hh"
#include <atomic>
#include <functional>
#include <string>
namespace bts {
class TexturePadding;
inline constexpr std::array<const char *, 9> export_channel_suffixes{"color",
                                                                     "metallic",
                                                                     "roughness",
                                                                     "normal",
                                                                     "bump",
                                                                     "ambient_occlusion",
                                                                     "normal_opengl",
                                                                     "mixed_ao",
                                                                     "displacement"};

// GPU-completed straight scene/data values; alpha is authored coverage. The
// owner applies base values, channel precision and Bump origin before capture.
// These immutable packets are encoding inputs, never CPU composition inputs.
struct ExportCapturedPlanes {
  int width = 0, height = 0;
  std::array<std::shared_ptr<const std::vector<float>>, 17> channels;
  std::shared_ptr<const std::vector<float>> mixed_ao;
  // Per-layer effective coverage (R, other components unused), captured in bounded GPU packets.
  std::map<QUuid, std::shared_ptr<const std::vector<float>>> material_id_coverage;
  // OpenGL target-UV perturbation only; excludes scalar channel4 and mesh curvature.
  std::shared_ptr<const std::vector<float>> normal_perturbation;
};
bool texture_export_has_adjustment(const TextureSet &);

struct ExportTile {
  TextureSet set;
  int number = 1001, resolution = 1024;
  // Complete native material normals (associated encoded XYZ+A), and optional complete AO.
  std::vector<float> normal, ao;
  int output_resolution = 0;            // Zero inherits the source size.
  std::vector<unsigned char> coverage;  // Native geometry coverage.
  int height = 0;
  int rows() const
  {
    return height ? height : resolution;
  }
  // Immutable geometry source; clone and derive normals/AO/coverage on export worker.
  std::shared_ptr<TexturePadding> deferred_padding;
  int output_rows() const
  {
    return output_resolution ? output_resolution : rows();
  }
  float bump_distance = .001f;  // Snapshot of native Bump Distance, independent of image pixels.
  std::shared_ptr<const ExportCapturedPlanes> captured;
  std::vector<ExportNormalContribution>
      normal_contributions;          // optional explicit per-source or baked-chain export input
  bool gpu_capture_planned = false;  // Filename planning only; encoder requires a full packet.
};
struct ExportRequest {
  std::string directory;
  ExportFormat format = ExportFormat::Png8;
  std::vector<ExportTile> tiles;
  std::string mesh;
  std::array<std::string, 9> naming{};
  bool standard_outputs = true;
  std::optional<PackedOutput> packed;
  ExportPadding padding = ExportPadding::None;
  int padding_width = 16;
  bool reference_composite =
      false;  // Verification oracle: original repeated full-channel evaluation.
  std::vector<ExportMapOutput> outputs;
  std::optional<std::array<bool, 9>>
      selected_channels;  // Explicit preset maps, including neutral missing channels.
  std::array<std::optional<ExportFormat>, 9> formats{};
  bool dither_normals = false;
  ExportNamingConvention naming_convention = ExportNamingConvention::Classic;
  bool openpbr_materialx = false;  // Document channels, EXR32, explicit OpenPBR-profile sets only.
  // Studio recipe: only channels with authored content, ACEScg color, raw data,
  // tagged float32 EXR, and $textureSet inserted when several sets share a mesh.
  bool studio = false;
};
struct ExportFile {
  std::string name;
  int resolution = 0, channels = 0, height = 0;
  std::uint32_t material_uid = 0;
  int tile = 1001, channel = -1;
  std::string asset_path;
};
std::vector<ExportFile> export_file_list(const ExportRequest &);
// Output is written to temporary sibling files and atomically renamed after each complete
// encoding. Cancellation retains completed files, removes the current temporary file, and never
// changes a project.
void write_texture_export(const ExportRequest &,
                          const std::atomic_bool &cancel,
                          const std::function<void(int, const std::string &)> &progress);
}  // namespace bts
