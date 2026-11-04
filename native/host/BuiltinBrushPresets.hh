#pragma once
#include "BrushPreset.hh"
#include <functional>
namespace bts {
// Basic procedural brushes use no bundled image inputs.
inline std::vector<BrushPreset> default_brush_presets(const std::function<std::shared_ptr<const AlphaBitmap>(const char *)> &){
 std::vector<BrushPreset> result;const auto add=[&](const char *name,auto edit){BrushSettings b;edit(b);result.push_back({QString::fromUtf8(name),std::move(b)});};
 add("Basic Hard",[](auto &b){b.hardness=1;b.spacing=12;});
 add("Basic Soft",[](auto &b){b.hardness=0;b.spacing=10;b.flow=35;b.pressure_flow=true;});
 add("Basic Hard Pressure",[](auto &b){b.hardness=.9;b.pressure_size=b.pressure_flow=true;b.minimum_size=15;b.minimum_flow=10;});
 return result;
}
std::vector<BrushPreset> builtin_brush_presets();
inline BrushSettings apply_stock_brush(const BrushSettings &current,BrushSettings stock){
 stock.erase=current.erase;stock.projected=current.projected;stock.smudge=current.smudge;stock.clone=current.clone;stock.clone_absolute=current.clone_absolute;stock.values=current.values;stock.channels=current.channels;stock.users=current.users;stock.user_values=current.user_values;stock.ao=current.ao;stock.displacement=current.displacement;stock.displacement_value=current.displacement_value;stock.mask_value=current.mask_value;return stock;
}
}
