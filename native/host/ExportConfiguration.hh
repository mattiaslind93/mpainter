#pragma once
#include "TextureExport.hh"
#include <QString>
#include <span>
namespace bts {
enum class ExportPresetKind {Channels,UsdMetalRough,Studio};
struct ExportOutputPreset {QUuid id=QUuid::createUuid();QString name;std::array<std::string,9> naming;bool standard_outputs=true;std::optional<PackedOutput> packed;std::vector<ExportMapOutput> outputs;ExportPresetKind kind=ExportPresetKind::Channels;std::array<std::optional<ExportFormat>,9> formats{};};
struct ExportConfiguration {
  QString directory;
  ExportFormat format=ExportFormat::Png8;
  std::set<QUuid> selected;
  QUuid preset;
  int size=0;
  std::map<QUuid,int> sizes;
  ExportPadding padding=ExportPadding::None;
  int padding_width=16;
  std::vector<ExportOutputPreset> presets;
  bool mesh_displacement=false;
  ExportNamingConvention naming_convention=ExportNamingConvention::Classic;
  bool openpbr_materialx=false;
};
// Used only for newly created templates. Legacy templates retain inheritance.
void set_default_export_formats(ExportOutputPreset &);
ExportOutputPreset converted_specular_glossiness_preset();
ExportOutputPreset usd_metal_rough_preset();
// Built-in studio default: OpenEXR float32, ACEScg color, raw linear data, and
// only channels with authored content. New projects and projects that never
// chose a template select it; an explicit later choice is preserved.
inline const QUuid studio_preset_id{"6e15bcb2-191a-4e19-b6d1-5db2bea50004"};
ExportOutputPreset studio_preset();
ExportOutputPreset material_id_preset();
// Metadata-only test: true when a visible, enabled layer authors this standard
// channel (paint pages, fill, generator/anchor, paint effect, material layering).
// Texture Set base values alone do not count. Performs no pixel readback.
bool export_channel_used(const TextureSet &,int channel);
void apply_export_preset(ExportRequest &,const ExportOutputPreset &);
QJsonObject export_configuration_json(const ExportConfiguration &);
ExportConfiguration read_export_configuration(const QJsonObject &,std::span<const QUuid> sets);
}
