// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BrushPreset.hh"
#include <functional>
namespace bts {
// Stock presets change stroke settings, retaining the current ink at application.
inline std::vector<BrushPreset> default_brush_presets(
    const std::function<std::shared_ptr<const AlphaBitmap>(const char *)> &alpha)
{
  std::vector<BrushPreset> result;
  const auto add = [&](const char *name, auto edit) {
    BrushSettings b;
    edit(b);
    result.push_back({QString::fromUtf8(name), std::move(b)});
  };
  add("Basic Hard", [](auto &b) {
    b.hardness = 1;
    b.spacing = 12;
  });
  add("Basic Soft", [](auto &b) {
    b.hardness = 0;
    b.spacing = 10;
    b.flow = 35;
    b.pressure_flow = true;
  });
  add("Basic Hard Pressure", [](auto &b) {
    b.hardness = .9;
    b.pressure_size = b.pressure_flow = true;
    b.minimum_size = 15;
    b.minimum_flow = 10;
  });
  const auto bitmap = [&](auto &b, const char *file) {
    b.alpha.kind = AlphaKind::Bitmap;
    b.alpha.bitmap = alpha(file);
    if (!b.alpha.bitmap)
      throw std::invalid_argument("Missing bundled brush alpha");
  };
  add("Dirt & Grime", [&](auto &b) {
    bitmap(b, "dirt_grime");
    b.spacing = 28;
    b.jitter = {35, 30, 180, 18};
  });
  add("Scratches / Rake", [&](auto &b) {
    bitmap(b, "scratches");
    b.follow_path = true;
    b.spacing = 10;
    b.jitter = {20, 25, 14.4f, 5};
  });
  add("Rust & Pitting", [&](auto &b) {
    bitmap(b, "rust_pitting");
    b.spacing = 32;
    b.jitter = {45, 35, 180, 25};
  });
  add("Chalk / Charcoal", [&](auto &b) {
    bitmap(b, "chalk_charcoal");
    b.spacing = 14;
    b.flow = 65;
    b.jitter = {12, 20, 180, 4};
  });
  add("Skin Pores / Dots", [&](auto &b) {
    bitmap(b, "pores_dots");
    b.spacing = 45;
    b.jitter = {25, 20, 180, 20};
  });
  add("Stitch / Seam", [&](auto &b) {
    bitmap(b, "stitch_seam");
    b.follow_path = true;
    b.spacing = 95;
    b.hardness = 1;
  });
  return result;
}
std::vector<BrushPreset> builtin_brush_presets();
inline BrushSettings apply_stock_brush(const BrushSettings &current, BrushSettings stock)
{
  stock.erase = current.erase;
  stock.projected = current.projected;
  stock.smudge = current.smudge;
  stock.clone = current.clone;
  stock.clone_absolute = current.clone_absolute;
  stock.values = current.values;
  stock.channels = current.channels;
  stock.users = current.users;
  stock.user_values = current.user_values;
  stock.ao = current.ao;
  stock.displacement = current.displacement;
  stock.displacement_value = current.displacement_value;
  stock.mask_value = current.mask_value;
  return stock;
}
}  // namespace bts
