#pragma once
#include "SymmetryJSON.hh"
#include <QJsonObject>
namespace blender {
struct Scene;
}
namespace bts {
SymmetrySettings read_project_symmetry(const blender::Scene &);
void write_project_symmetry(blender::Scene &, const SymmetrySettings &);
}  // namespace bts
