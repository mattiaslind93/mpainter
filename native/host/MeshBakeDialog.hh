#include "BakeSource.hh"
#pragma once
#include "MeshBaker.hh"
#include "TextureDocument.hh"
#include <functional>
class QWidget;
namespace bts {
QJsonObject mesh_bake_template(const QUuid &);
void apply_mesh_bake_template(const QUuid &, const QJsonObject &);
std::optional<std::vector<MeshBakeResult>> choose_mesh_bake(
    QWidget *,
    const TextureDocument &,
    const SceneSnapshot &,
    const BakeSourceLoader &load_source = {},
    std::function<void(const std::function<void()> &)> with_gpu = {});
}  // namespace bts
