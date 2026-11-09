#pragma once
#include "TextureDocument.hh"
#include "MaskEffectPath.hh"
#include <QObject>
#include <QModelIndex>
#include <map>
#include <variant>
namespace bts {
// Own effect metadata while a popup is open. Shared paint/resource buffers stay
// shared; callbacks never retain pointers into a document refresh.
class LayerEffectRowState final : public QObject {
 public:
  using Value = std::variant<LevelsEffect, MaskEffect>;
  explicit LayerEffectRowState(QObject *parent) : QObject(parent)
  {
    setObjectName("layerEffectRowState");
  }
  static bool current_target_matches(const QModelIndex &row, const TextureSet &set)
  {
    if (!row.isValid() || QUuid(row.data(Qt::UserRole).toString()) != set.selected_layer ||
        QUuid(row.data(Qt::UserRole + 10).toString()) != set.selected_effect)
      return false;
    return row.data(set.selected_effect.isNull() ? Qt::UserRole + 8 : Qt::UserRole + 12)
               .toBool() == set.selected_mask;
  }
  void synchronize(const TextureSet *set)
  {
    values.clear();
    if (set)
      for (const auto &layer : set->layers) {
        for (const auto &effect : layer.effects)
          values.emplace(effect.id, effect);
        if (layer.mask)
          for (const auto &entry : mask_effect_entries(layer.mask->effects))
            values.emplace(entry.effect->id, *entry.effect);
      }
  }
  std::map<QUuid, Value> values;
};
}  // namespace bts
