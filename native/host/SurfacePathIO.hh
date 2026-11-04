#pragma once
#include "SurfacePath.hh"
#include <QJsonObject>
namespace bts {
QJsonObject surface_path_json(const SurfacePath &);
std::shared_ptr<const SurfacePath> read_surface_path(const QJsonValue &);
}
