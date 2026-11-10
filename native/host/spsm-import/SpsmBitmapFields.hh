// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SpsmLevelsParser.hh"
#include <span>

namespace bts::spsm_bitmap_detail {
// Shared admission for content and mask bitmap actions. Source ordinals remain
// source data: the caller's UV/HQ policy supplies destination interpretation.
// Absent optional fields mean the defaults below, not unbounded unknown state.
class Fields : public spsm_levels_detail::Parser {
 public:
  using Parser::Parser;
  void type(spsm::NodeId id, std::string_view name) const
  {
    if (object(id).class_name != name)
      throw SpsmLevelsUnsupported{id};
  }
  void zero_selection(spsm::NodeId id) const
  {
    if (auto f = field(id, "selection")) {
      auto v = std::get_if<spsm::Blob>(&node(*f).value);
      if (!v || v->size)
        throw SpsmLevelsUnsupported{*f};
    }
    equal_bits(id, "tags", 0);
  }
  void equal_bits(spsm::NodeId id, std::string_view key, std::uint64_t expected) const
  {
    if (auto f = field(id, key); f && bits(*f) != expected)
      throw SpsmLevelsUnsupported{*f};
  }
  void equal_bool(spsm::NodeId id, std::string_view key, bool expected) const
  {
    if (auto f = field(id, key)) {
      auto v = std::get_if<bool>(&node(*f).value);
      if (!v || *v != expected)
        throw SpsmLevelsUnsupported{*f};
    }
  }
  void floats(spsm::NodeId id, std::initializer_list<float> expected) const
  {
    auto v = std::get_if<spsm::FloatValues>(&node(id).value);
    if (!v || v->count != expected.size() ||
        !std::equal(expected.begin(), expected.end(), v->values.begin()))
      throw SpsmLevelsUnsupported{id};
  }
  void equal_floats(spsm::NodeId id,
                    std::string_view key,
                    std::initializer_list<float> expected) const
  {
    if (auto f = field(id, key))
      floats(*f, expected);
  }
  void empty_text(spsm::NodeId id, std::string_view key) const
  {
    if (auto f = field(id, key)) {
      auto v = std::get_if<std::string>(&node(*f).value);
      if (!v || !v->empty())
        throw SpsmLevelsUnsupported{*f};
    }
  }
  void metadata(spsm::NodeId id) const
  {
    if (auto f = field(id, "uid"))
      bits(*f);
    if (auto f = field(id, "label"))
      if (!std::holds_alternative<std::string>(node(*f).value))
        throw SpsmLevelsUnsupported{*f};
  }
  void null_field(spsm::NodeId id, std::string_view key) const
  {
    if (auto f = field(id, key))
      if (!std::holds_alternative<std::monostate>(node(*f).value))
        throw SpsmLevelsUnsupported{*f};
  }
  void blend(spsm::NodeId id) const
  {
    type(id, "DataBlending");
    fields(id, {"blendingMode", "channelTypes", "opacity"});
    for (auto key : {"blendingMode", "channelTypes", "opacity"})
      required(id, key);
  }
  void grid(spsm::NodeId id) const
  {
    type(id, "DataGridDeformation");
    fields(id, {"depth", "gridSize", "uid", "vertices"});
    metadata(id);
    floats(required(id, "depth"), {.1f});
    auto dimensions = std::get_if<spsm::IntValues>(&node(required(id, "gridSize")).value);
    if (!dimensions || dimensions->count != 2 || dimensions->values[0] != 3 ||
        dimensions->values[1] != 3)
      throw SpsmLevelsUnsupported{id};
    auto vertices = std::get_if<spsm::Array>(&node(required(id, "vertices")).value);
    if (!vertices || vertices->elements.size() != 9)
      throw SpsmLevelsUnsupported{id};
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 3; ++x) {
        auto v = vertices->elements[y * 3 + x];
        type(v, "DataGridVertex");
        fields(v,
               {"position3D",
                "positionUV",
                "tangentColumnNext",
                "tangentColumnPrev",
                "tangentRowNext",
                "tangentRowPrev"});
        floats(required(v, "position3D"), {float(x - 1), float(y - 1), 0});
        floats(required(v, "positionUV"), {x * .5f, y * .5f});
        floats(required(v, "tangentColumnNext"), {x == 2 ? 0.f : 1.f / 3, 0, 0});
        floats(required(v, "tangentColumnPrev"), {x == 0 ? 0.f : -1.f / 3, 0, 0});
        floats(required(v, "tangentRowNext"), {0, y == 2 ? 0.f : 1.f / 3, 0});
        floats(required(v, "tangentRowPrev"), {0, y == 0 ? 0.f : -1.f / 3, 0});
      }
  }
  void stack(spsm::NodeId id, std::string_view key, std::span<const spsm::NodeId> expected) const
  {
    auto f = field(id, key);
    if (!f || std::holds_alternative<std::monostate>(node(*f).value)) {
      if (!expected.empty())
        throw SpsmLevelsUnsupported{id};
      return;
    }
    type(*f, "DataStackActions");
    fields(*f, {"items", "uid"});
    metadata(*f);
    auto a = std::get_if<spsm::Array>(&node(required(*f, "items")).value);
    if (!a || a->elements.size() != expected.size() ||
        !std::equal(expected.begin(), expected.end(), a->elements.begin()))
      throw SpsmLevelsUnsupported{*f};
  }
  void layer(spsm::NodeId id, bool group) const
  {
    type(id, group ? "DataLayerGroup" : "DataLayerColor");
    fields(id,
           {"actions",
            "colorTag",
            "enabled",
            "enabledGeometryMask",
            "enabledMeshDefault",
            "enabledMeshList",
            "enabledUVTileDefault",
            "enabledUVTileList",
            "explicitPersistentCachePin",
            "gammaCompensation",
            "geometryMaskType",
            "label",
            "maskActions",
            "maskEnabled",
            "maskInitial",
            "perChannelBlending",
            "uid",
            "subStack",
            "GUIcollapsedState"});
    if (!group && (field(id, "subStack") || field(id, "GUIcollapsedState")))
      throw SpsmLevelsUnsupported{id};
    if (auto f = field(id, "GUIcollapsedState"))
      if (!std::holds_alternative<bool>(node(*f).value))
        throw SpsmLevelsUnsupported{*f};
    metadata(id);
    equal_bool(id, "gammaCompensation", false);
    equal_bool(id, "explicitPersistentCachePin", false);
  }
  void fill(spsm::NodeId id) const
  {
    type(id, "DataActionFill");
    fields(id,
           {"backfaceCulling",
            "backfaceCullingAngle",
            "backfaceCullingHardness",
            "channelTypes",
            "cylinderCullingAngle",
            "cylinderHideCaps",
            "dddEulerAngles",
            "dddGrid",
            "dddPosition",
            "dddSize",
            "dddWrap",
            "depthCulling",
            "depthCullingHardness",
            "enabled",
            "filtering",
            "label",
            "perChannelBlending",
            "projection",
            "selection",
            "sideHardness",
            "sources",
            "tags",
            "uid",
            "uvGrid",
            "uvSamplingWrap",
            "uvTransferChannel",
            "uvTransformation"});
    metadata(id);
    zero_selection(id);
    equal_bits(id, "projection", 0);
    equal_bits(id, "filtering", 2);
    equal_bits(id, "uvSamplingWrap", 3);
    equal_bits(id, "uvTransferChannel", 0);
    null_field(id, "uvGrid");
    equal_bool(id, "backfaceCulling", false);
    equal_bool(id, "depthCulling", true);
    equal_bool(id, "cylinderHideCaps", false);
    equal_floats(id, "backfaceCullingAngle", {.5f});
    equal_floats(id, "backfaceCullingHardness", {.5f});
    equal_floats(id, "depthCullingHardness", {.75f});
    equal_floats(id, "cylinderCullingAngle", {1});
    equal_floats(id, "sideHardness", {.5f});
    equal_floats(id, "dddEulerAngles", {0, 0, 0});
    equal_floats(id, "dddPosition", {0, 0, 0});
    equal_floats(id, "dddSize", {1, 1, 1});
    equal_bits(id, "dddWrap", 1);
    if (auto f = field(id, "dddGrid"))
      grid(*f);
    auto uv = required(id, "uvTransformation");
    type(uv, "Data2DTransformation");
    fields(uv,
           {"GUIPivot",
            "GUIScaleRatioLocked",
            "physicalSizeBoundedByScaleFactors",
            "rotation",
            "scaleFactors",
            "scaleMode",
            "scalePhysicalSize",
            "translation"});
    equal_bool(uv, "GUIScaleRatioLocked", true);
    equal_bool(uv, "physicalSizeBoundedByScaleFactors", false);
    equal_bits(uv, "scaleMode", 0);
    equal_floats(uv, "scalePhysicalSize", {10, 10});
    floats(required(uv, "GUIPivot"), {.5f, .5f});
    floats(required(uv, "rotation"), {0});
    floats(required(uv, "translation"), {0, 0});
    auto scale = std::get_if<spsm::FloatValues>(&node(required(uv, "scaleFactors")).value);
    if (!scale || scale->count != 2 || !std::isfinite(scale->values[0]) ||
        !std::isfinite(scale->values[1]))
      throw SpsmLevelsUnsupported{uv};
  }
  void bitmap(spsm::NodeId source) const
  {
    type(source, "DataSourceBitmap");
    fields(source, {"bitmap", "channelTypes", "opacity", "uid"});
    metadata(source);
    auto b = required(source, "bitmap");
    type(b, "DataBitmap");
    fields(b, {"alphaType", "colorSpaceOverride", "uid", "urlToBitmapRes"});
    metadata(b);
    equal_bits(b, "alphaType", 0);
    if (auto c = field(b, "colorSpaceOverride")) {
      type(*c, "DataColorSpaceOverride");
      fields(*c, {"main", "workflowACE", "workflowLegacy", "workflowOCIO"});
      equal_bits(*c, "main", 0);
      equal_bits(*c, "workflowLegacy", 0);
      empty_text(*c, "workflowACE");
      empty_text(*c, "workflowOCIO");
    }
  }
};
}  // namespace bts::spsm_bitmap_detail
