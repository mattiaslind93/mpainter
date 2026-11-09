#pragma once
#include "MaterialLayering.hh"
#include "ShaderInstances.hh"
#include "TextureResource.hh"
#include <QByteArray>
#include <atomic>
#include <functional>

namespace bts {
struct TextureSet;
// Original encoded bytes only. A bytes value shares an existing immutable asset;
// otherwise descriptor.path is read on the explicit package worker.
struct MaterialParameterFile {
  TextureSourceFile descriptor;
  QByteArray bytes;
};
struct MaterialParameterOriginal {
  QUuid asset;
  std::vector<MaterialParameterFile> files;
};
struct MaterialParameterColorMember {
  QString input_space, processor;
  bool operator==(const MaterialParameterColorMember &) const = default;
};
struct MaterialLayerParameters {
  QString name, working_space;
  std::array<double, 9> scene_linear_to_xyz{};  // Row-major, exact source context.
  bool enabled = true;
  ShaderParameters shader;
  std::array<LayerMaterialInput, 4> materials;
  std::array<QString, 4> color_processors;  // Ordinary v1 source processor IDs.
  std::array<std::map<int, MaterialParameterColorMember>, 4>
      color_members;  // Version2 LINEAR UDIM members, never first-member aliases.
  std::map<int, std::array<int, 2>> tiles;
  // Three evaluated mesh-UV masks, always explicit tile members, raw R/alpha1.
  std::array<std::shared_ptr<const FillTexture>, 3> masks;
  std::vector<MaterialParameterOriginal> originals;
};
// Metadata codec does not decode, certify OCIO, or make a value publishable.
QJsonObject material_layer_parameters_json(const MaterialLayerParameters &);
MaterialLayerParameters read_material_layer_parameters_json(const QJsonValue &);
// New-directory publication only; cancellation never modifies the project or
// an existing package. Reader verifies canonical containment/size/SHA for ALL files.
void write_material_layer_parameter_package(const QString &,
                                            const MaterialLayerParameters &,
                                            const std::atomic_bool &);
MaterialLayerParameters read_material_layer_parameter_package(const QString &,
                                                              const std::atomic_bool &);
// Decoder must use the shared actual image decoder and processor gate. Color
// result is checked against its stored effective input space + processor ID.
using MaterialParameterDecoder = std::function<std::shared_ptr<const FillTexture>(
    const FillTexture &, const MaterialParameterOriginal &, bool mask)>;
using MaterialParameterProcessor = std::function<QString(const FillTexture &)>;
// The caller must check the real owned Principled surface. TextureSet metadata
// cannot describe arbitrary retained Alpha/Emission/SSS/native socket inputs.
// Mandatory before decoding a Color sequence; its native implementation must
// inspect every HYDRATED member and reject nonlinear/unsupported processors.
using MaterialParameterColorSequenceValidator = std::function<void(const FillTexture &)>;
using MaterialParameterTargetValidator = std::function<void(const TextureSet &)>;
void require_material_parameter_main(const TextureSet &);
TextureSet reconstruct_material_layer_parameters(
    const TextureSet &,
    const MaterialLayerParameters &,
    const QString &working_space,
    const std::array<double, 9> &scene_linear_to_xyz,
    const MaterialParameterDecoder &,
    const MaterialParameterProcessor &,
    const MaterialParameterTargetValidator &,
    const MaterialParameterColorSequenceValidator & = {});
}  // namespace bts
