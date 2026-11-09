#pragma once
#include "FillProjection.hh"
#include "NativeMaterialParameters.hh"
#include <QUuid>
namespace bts {
inline QJsonValue native_material_fill_json(const FillProjection &fill)
{
  if (!fill.native_material)
    return QJsonValue(QJsonValue::Null);
  if (QUuid(QString::fromStdString(fill.material_resource)).isNull())
    throw std::invalid_argument("Native material fill requires a project resource");
  return native_material_description_json(*fill.native_material);
}
inline void read_native_material_fill(FillProjection &fill, const QJsonValue &value)
{
  if (value.isUndefined() || value.isNull()) {
    fill.native_material.reset();
    return;
  }
  if (!value.isObject() || QUuid(QString::fromStdString(fill.material_resource)).isNull())
    throw std::invalid_argument("Native material fill resource/interface is invalid");
  fill.native_material = std::make_shared<const NativeMaterialDescription>(
      read_native_material_description(value.toObject()));
}
}  // namespace bts
