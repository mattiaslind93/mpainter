#include "NativeJsonProperty.hh"
#include "SymmetryIO.hh"
#include <QJsonArray>
#include <QJsonDocument>
#include "BKE_idprop.hh"
#include "DNA_scene_types.h"
namespace bts {
using namespace blender;
SymmetrySettings read_project_symmetry(const Scene &scene) {
  const auto *root=scene.id.properties;const auto *property=root?IDP_GetPropertyFromGroup(root,"bts_symmetry"):nullptr;if(!property)return {};
  QJsonParseError error;const auto document=QJsonDocument::fromJson(native_json_property_bytes(*property,"Project symmetry JSON decoding"),&error);
  if(error.error!=QJsonParseError::NoError||!document.isObject())throw std::runtime_error("Invalid project symmetry JSON");const auto object=document.object();
  return read_symmetry_json(object);
}
void write_project_symmetry(Scene &scene,const SymmetrySettings &s) {
  s.validate();const auto data=QJsonDocument(symmetry_json(s)).toJson(QJsonDocument::Compact);
  IDP_ReplaceInGroup(IDP_EnsureProperties(&scene.id),new_native_json_property(data,"bts_symmetry","Project symmetry JSON storage"));
}
}
