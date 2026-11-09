#pragma once
#include <QModelIndex>
#include <QRect>
namespace bts {
inline constexpr int layer_controls_width = 60;
inline QRect layer_controls_rect(const QRect &row)
{
  return row.adjusted(row.width() - layer_controls_width, 0, 0, 0);
}
inline QRect layer_effect_controls_rect(const QRect &row)
{
  return row.adjusted(row.width() - 94, 0, -2, 0);
}
inline QRect layer_effect_blend_rect(const QRect &row)
{
  const auto box = layer_effect_controls_rect(row);
  return {box.left(), box.top(), 40, box.height()};
}
inline QRect layer_effect_opacity_rect(const QRect &row)
{
  const auto box = layer_effect_controls_rect(row);
  return {box.left() + 40, box.top(), 32, box.height()};
}
inline QRect layer_effect_delete_rect(const QRect &row)
{
  const auto box = layer_effect_controls_rect(row);
  return {box.right() - 19, box.top(), 20, box.height()};
}
inline QRect layer_eye_rect(const QRect &row, int indent = 0)
{
  return {row.left() + 4 + indent, row.center().y() - 8, 16, 16};
}
inline int layer_thumbnail_x(const QModelIndex &index, const QRect &row)
{
  return row.left() + 26 + index.data(Qt::UserRole + 2).toInt() * 20 +
         (index.data(Qt::UserRole + 3).toBool() ? 30 : 0);
}
inline QRect layer_mask_rect(const QModelIndex &index, const QRect &row)
{
  return {layer_thumbnail_x(index, row) + 30, row.center().y() - 12, 24, 24};
}
inline QRect layer_geometry_rect(const QModelIndex &index, const QRect &row)
{
  return {layer_thumbnail_x(index, row) + (index.data(Qt::UserRole + 6).toBool() ? 60 : 30),
          row.center().y() - 12,
          24,
          24};
}

inline QRect layer_instance_rect(const QModelIndex &index, const QRect &row)
{
  return {layer_geometry_rect(index, row).left() + 30, row.center().y() - 12, 24, 24};
}
}  // namespace bts
