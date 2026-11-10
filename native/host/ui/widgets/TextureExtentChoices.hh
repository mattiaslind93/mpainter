// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureExtent.hh"
#include <QComboBox>
#include <QSignalBlocker>
#include <optional>
#include <vector>
namespace bts {
// Choices describe dimensions only. Pixel allocation is budgeted by its owner.
inline void refresh_texture_extent_choices(QComboBox &box,
                                           int minimum,
                                           std::optional<int> selected = {})
{
  require_texture_resolution(minimum, "Resolution menu minimum", 1, true);
  const QSignalBlocker block(&box);
  const int requested = selected.value_or(box.currentData().toInt());
  const int current = std::min(requested, max_texture_resolution);
  if (requested > max_texture_resolution)
    box.setToolTip(QComboBox::tr("Resolution limited to 8192 (8K)."));
  // Keep special entries such as the per-tile Default item and its separator.
  int prefix = 0;
  while (prefix < box.count() && box.itemData(prefix).toInt() <= 0)
    ++prefix;
  const int gpu = texture_gpu_max_size.load(std::memory_order_relaxed);
  const int maximum = gpu > 0 ? std::min(gpu, max_texture_resolution) : max_texture_resolution;
  std::vector<int> values;
  for (int size = minimum; size <= maximum; size *= 2)
    values.push_back(size);
  if (current > 0 && std::find(values.begin(), values.end(), current) == values.end())
    values.push_back(current);
  bool same = box.count() - prefix == int(values.size());
  for (int i = 0; same && i < int(values.size()); ++i)
    same = box.itemData(prefix + i).toInt() == values[i];
  if (!same) {
    while (box.count() > prefix)
      box.removeItem(prefix);
    for (int size : values)
      box.addItem(QString::number(size), size);
  }
  const int index = box.findData(current);
  box.setCurrentIndex(index >= 0 ? index : box.count() ? 0 : -1);
}
class TextureExtentComboBox final : public QComboBox {
  const int minimum_;

 public:
  explicit TextureExtentComboBox(int minimum, QWidget *parent = nullptr)
      : QComboBox(parent), minimum_(minimum)
  {
    refresh_texture_extent_choices(*this, minimum_);
  }
  void showPopup() override
  {
    refresh_texture_extent_choices(*this, minimum_);
    QComboBox::showPopup();
  }
};
}  // namespace bts
