// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SpsmMaterialImport.hh"
#include "SpsmLevelsParser.hh"
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <cctype>
#include <set>
#include <mutex>
extern int qInitResources_builtin_grunges();
namespace bts {
namespace spsm_native_detail {
namespace sem = spsm::semantic;
inline std::shared_ptr<FillTexture> spsm_grunge_texture(std::string_view hint)
{
  qInitResources_builtin_grunges();
  const char *slug = "crease_dirt";
  if (hint.find("drip") != hint.npos || hint.find("leak") != hint.npos)
    slug = "leaks_streaks";
  else if (hint.find("scratch") != hint.npos || hint.find("fiber") != hint.npos)
    slug = "scratches";
  else if (hint.find("rust") != hint.npos || hint.find("bnw_spots") != hint.npos)
    slug = "rust_spots";
  else if (hint.find("moss") != hint.npos || hint.find("cells") != hint.npos ||
           hint.find("organic") != hint.npos)
    slug = "moss_organic";
  else if (hint.find("mud") != hint.npos || hint.find("fractal") != hint.npos)
    slug = "ground_mud";
  else if (hint.find("edge") != hint.npos || hint.find("concrete") != hint.npos ||
           hint.find("damage") != hint.npos)
    slug = "edge_wear";
  else if (hint.find("dust") != hint.npos || hint.find("cloud") != hint.npos ||
           hint.find("perlin") != hint.npos || hint.find("microscope") != hint.npos)
    slug = "fine_dust";
  static std::mutex cache_mutex;
  const std::lock_guard lock(cache_mutex);
  static std::map<std::string, std::shared_ptr<FillTexture>> cache;
  if (const auto it = cache.find(slug); it != cache.end())
    return it->second;
  const auto filename = QString("grunge_%1.png").arg(slug);
  QFile source(":/builtin-grunges/" + filename);
  if (!source.open(QIODevice::ReadOnly))
    return {};
  const auto bytes = source.readAll();
  const auto decoded = QImage::fromData(bytes, "PNG");
  if (decoded.isNull() || !decoded.isGrayscale())
    return {};
  auto label = QString::fromLatin1(slug);
  label.replace('_', ' ');
  for (int i = 0; i < label.size(); ++i)
    if (i == 0 || label[i - 1] == ' ')
      label[i] = label[i].toUpper();
  const auto id = QUuid::createUuidV5(QUuid("{954c6511-3b9d-4f1f-bbdd-173dfbb99b69}"),
                                      filename.toUtf8());
  auto texture = std::make_shared<FillTexture>();
  texture->asset = id.toString(QUuid::WithoutBraces).toStdString();
  texture->hash = QString::fromLatin1(
                      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
                      .toStdString();
  texture->name = ("Grunge " + label).toStdString();
  texture->raw = true;
  texture->input_space = "Non-Color/raw";
  texture->width = decoded.width();
  texture->height = decoded.height();
  auto pixels = std::make_shared<std::vector<FillTexture::Pixel>>(std::size_t(texture->width) *
                                                                  texture->height);
  for (int y = 0; y < texture->height; ++y)
    for (int x = 0; x < texture->width; ++x) {
      const float v = qRed(decoded.pixel(x, texture->height - 1 - y)) / 255.f;
      (*pixels)[std::size_t(y) * texture->width + x] = {v, v, v, 1};
    }
  texture->pixels = std::move(pixels);
  texture->validate();
  cache.emplace(slug, texture);
  return texture;
}
// Reads only our retained preset arena. Never opens a package resource.
class Lowerer {
  using NodeId = spsm::NodeId;
  PreparedSpsm &result;
  const spsm::Preset &p;
  const sem::PresetInspection &ir;
  std::function<std::array<float, 3>(std::array<float, 3>)> transform;
  std::map<NodeId, const sem::Source *> sources;
  std::map<NodeId, const sem::Procedural *> procedurals;
  std::optional<NodeId> initial_fill_action;
  std::shared_ptr<const LinearColorTransform> color_boundary;
  bool boundary_emitted = false;
  std::array<bool, 6> seen_content{};
  std::set<NodeId> consumed_colors;
  void emit_boundary(TextureLayer &out)
  {
    if (color_boundary && !boundary_emitted) {
      LevelsEffect effect;
      effect.kind = ContentEffectKind::LinearColorTransform;
      effect.channel = 0;
      effect.linear_color = color_boundary;
      out.effects.push_back(std::move(effect));
      boundary_emitted = true;
    }
  }
  static constexpr std::array<std::uint64_t, 6> bits{1, 8192, 128, 4194304, 2, 8388608};
  const spsm::Node &node(NodeId id) const
  {
    return p.nodes.at(id);
  }
  std::optional<NodeId> field(NodeId id, std::string_view name) const
  {
    if (const auto o = std::get_if<spsm::Object>(&node(id).value))
      for (const auto &f : o->fields)
        if (f.name == name)
          return f.value;
    return {};
  }
  std::string string(std::optional<NodeId> id) const
  {
    if (id)
      if (const auto x = std::get_if<std::string>(&node(*id).value))
        return *x;
    return {};
  }
  std::optional<double> number(std::optional<NodeId> id) const
  {
    if (!id)
      return {};
    const auto &v = node(*id).value;
    if (const auto x = std::get_if<spsm::FloatValues>(&v); x && x->count == 1)
      return x->values[0];
    if (const auto x = std::get_if<spsm::IntValues>(&v); x && x->count == 1)
      return x->values[0];
    if (const auto x = std::get_if<std::uint64_t>(&v))
      return double(*x);
    if (const auto x = std::get_if<std::int64_t>(&v))
      return double(*x);
    if (const auto x = std::get_if<bool>(&v))
      return double(*x);
    return {};
  }
  std::uint64_t mask(std::optional<NodeId> id, std::uint64_t fallback = 0) const
  {
    if (!id)
      return fallback;
    const auto &v = node(*id).value;
    if (const auto x = std::get_if<std::uint64_t>(&v))
      return *x;
    if (const auto x = std::get_if<std::int64_t>(&v); x && *x >= 0)
      return *x;
    const auto x = number(id);
    return x && *x >= 0 && *x < 18446744073709551616. ? std::uint64_t(*x) : fallback;
  }
  bool enabled(std::optional<NodeId> id) const
  {
    if (id)
      if (const auto b = std::get_if<bool>(&node(*id).value))
        return *b;
    return true;
  }
  void note(NodeId source, SpsmDiagnosticKind kind, std::string_view code, std::string_view detail)
  {
    result.diagnostics.push_back({*result.source->primary_preset, source, kind, code, detail});
    if (kind != SpsmDiagnosticKind::SourceMetadata)
      result.partial = true;
  }
  void lowered(NodeId source)
  {
    for (auto &binding : result.actions)
      if (binding.source == source)
        binding.lowered = true;
  }
  static std::string key(std::string_view value)
  {
    std::string r;
    for (unsigned char c : value)
      if (std::isalnum(c))
        r += char(std::tolower(c));
    return r;
  }
  std::map<std::string, double> tweaks(const sem::Procedural &proc)
  {
    std::map<std::string, double> result;
    for (const auto id : proc.tweaks) {
      const auto o = std::get_if<spsm::Object>(&node(id).value);
      if (!o)
        continue;
      const auto name = string(field(id, "identifier"));
      if (name.empty())
        continue;
      if (o->class_name != "DataTweakFloat" && o->class_name != "DataTweakInt" &&
          o->class_name != "DataTweakBool")
        continue;
      if (const auto value = number(field(id, "value")); value && std::isfinite(*value))
        result[key(name)] = *value;
    }
    return result;
  }
  // A native approximation must identify the controls/connections it does not
  // consume. Values stay in the immutable typed arena; diagnostics point to the
  // exact input/tweak node instead of claiming the enclosing graph is complete.
  void unsupported_tweaks(const sem::Procedural &proc, const std::set<std::string> &known)
  {
    for (const auto id : proc.tweaks) {
      const auto name = key(string(field(id, "identifier")));
      const auto object = std::get_if<spsm::Object>(&node(id).value);
      const bool numeric = object &&
                           (object->class_name == "DataTweakFloat" ||
                            object->class_name == "DataTweakInt" ||
                            object->class_name == "DataTweakBool") &&
                           number(field(id, "value")).has_value();
      if (!known.contains(name) || !numeric)
        note(
            id,
            SpsmDiagnosticKind::Omission,
            "native-procedural-parameter-omitted",
            "This typed source parameter is retained unchanged but has no native generator/filter mapping; the surrounding supported action remains active");
      else if (const auto value = number(field(id, "value")); value && *value < 0 && name == "ao")
        note(
            id,
            SpsmDiagnosticKind::Assumption,
            "native-generator-ao-polarity",
            "Signed source AO control has no equivalent signed native influence; the native influence is clamped to zero, not interpreted as an inverted source graph");
    }
  }
  void generator_inputs(const sem::Procedural &proc,
                        MeshGeneratorSettings &e,
                        const std::map<std::string, double> &t)
  {
    if (!proc.inputs)
      return;
    const auto links = std::get_if<spsm::Array>(&node(*proc.inputs).value);
    if (!links) {
      note(
          *proc.inputs,
          SpsmDiagnosticKind::Omission,
          "native-generator-input-shape",
          "Unsupported procedural input container remains in source; no nested source graph is executed");
      return;
    }
    for (const auto link : links->elements) {
      const auto source = field(link, "source");
      const auto bitmap = source ? field(*source, "bitmap") : std::optional<NodeId>{};
      const auto uri = bitmap ? string(field(*bitmap, "urlToBitmapRes")) : std::string{};
      if (uri == "resourcealias:AmbientOcclusion" || uri == "resourcealias:Curvature" ||
          uri == "resourcealias:Position" || uri == "resourcealias:NormalWorldSpace" ||
          uri == "resourcealias:Thickness")
        note(
            link,
            SpsmDiagnosticKind::Assumption,
            "native-generator-input-map",
            "Source mesh-map input uses the corresponding destination baked map through native generator influences; original connection-specific graph math is not evaluated");
      else {
        note(
            link,
            SpsmDiagnosticKind::Omission,
            "native-generator-input-omitted",
            "This custom procedural/uniform input and its authored connection/tweaks are retained but not evaluated; native built-in grunge is an explicitly different approximation");
        if (source)
          if (const auto nested = field(*source, "procedural"))
            if (const auto child = procedure(*nested)) {
              const auto cname = package(*child);
              if (cname.find("drip") != cname.npos || cname.find("leak") != cname.npos) {
                e.grunge_pattern = 3;
                e.triplanar_grunge = true;
                if (e.generator == 8 || e.generator == 9)
                  e.response = 2;
              }
              else if (cname.find("fiber") != cname.npos)
                e.grunge_pattern = 2;
              else if (cname.find("spots") != cname.npos)
                e.grunge_pattern = 1;
              for (const auto id : child->tweaks)
                note(
                    id,
                    SpsmDiagnosticKind::Omission,
                    "native-generator-input-parameter-omitted",
                    "Parameter belongs to an unsupported custom generator input; its exact value is retained and is not consumed by the native built-in grunge");
            }
      }
    }
  }
  bool standalone_mask() const
  {
    const auto root = std::get_if<spsm::Object>(&node(p.root).value);
    return root && root->class_name == "DataSmartMask";
  }
  static float tweak(const std::map<std::string, double> &t,
                     std::initializer_list<const char *> names,
                     float fallback,
                     float low,
                     float high)
  {
    for (const auto name : names)
      if (const auto v = t.find(name); v != t.end())
        return std::clamp(float(v->second), low, high);
    return fallback;
  }
  std::string package(const sem::Procedural &proc) const
  {
    auto uri = string(proc.uri);
    if (const auto q = uri.find('?'); q != uri.npos)
      uri.resize(q);
    if (uri.starts_with("pkg://"))
      uri.erase(0, 6);
    for (auto &c : uri)
      c = char(std::tolower(static_cast<unsigned char>(c)));
    return uri;
  }
  std::optional<LayerBlend> blend(std::uint64_t value) const
  {
    switch (value) {
      case 0:
        return LayerBlend::Disabled;
      case 1:
        return LayerBlend::Replace;
      case 2:
        return LayerBlend::Normal;
      case 3:
        return LayerBlend::Darken;
      case 4:
        return LayerBlend::Multiply;
      case 7:
        return LayerBlend::Lighten;
      case 8:
        return LayerBlend::Screen;
      case 10:
        return LayerBlend::LinearDodge;
      case 11:
        return LayerBlend::Overlay;
      case 12:
        return LayerBlend::SoftLight;
      case 13:
        return LayerBlend::HardLight;
      case 14:
        return LayerBlend::VividLight;
      case 15:
        return LayerBlend::LinearLight;
      case 16:
        return LayerBlend::PinLight;
      case 17:
        return LayerBlend::Difference;
      case 18:
        return LayerBlend::Exclusion;
      case 19:
        return LayerBlend::Tint;
      case 20:
        return LayerBlend::Saturation;
      case 21:
        return LayerBlend::Color;
      case 22:
        return LayerBlend::Value;
      case 23:
        return LayerBlend::Divide;
      case 24:
        return LayerBlend::InverseDivide;
      case 25:
        return LayerBlend::Passthrough;
      case 29:
        return LayerBlend::Subtract;
      case 30:
        return LayerBlend::InverseSubtract;
      case 31:
        return LayerBlend::SignedAddition;
      default:
        return {};
    }
  }
  std::pair<float, LayerBlend> blending(const sem::Action &action, int channel, bool is_mask)
  {
    for (const auto &row : action.blending)
      if (is_mask ? (mask(row.channel_types) == 0 || mask(row.channel_types) == UINT64_MAX) :
                    bool(mask(row.channel_types) & bits[channel]))
      {
        if (is_mask && mask(row.channel_types) == UINT64_MAX)
          note(
              row.source,
              SpsmDiagnosticKind::Assumption,
              "native-mask-all-channels-mix",
              "All-channel blend sentinel is interpreted in this action's scalar mask context; its authored blend and opacity apply to the mask value");
        const auto raw = mask(row.source_mode);
        const auto mode = !is_mask && channel == 4 && raw == 10 ?
                              std::optional<LayerBlend>(LayerBlend::SignedAddition) :
                              blend(raw);
        if (!mode) {
          note(row.source,
               SpsmDiagnosticKind::Omission,
               "native-action-blend",
               "Unknown action blend retained in source; native Normal interpretation used");
        }
        return {std::clamp(float(number(row.opacity).value_or(1)), 0.f, 1.f),
                mode.value_or(LayerBlend::Normal)};
      }
    return {1, LayerBlend::Normal};
  }
  bool levels_mix_supported(const sem::Action &action, bool is_mask)
  {
    const auto reject = [&](NodeId id) {
      note(
          id,
          SpsmDiagnosticKind::Omission,
          "native-levels-mix-unsupported",
          "Levels mix metadata has unsupported structure/channel/blend/opacity; the curve subaction remains in source and is omitted independently");
      return false;
    };
    const auto integral = [&](std::optional<NodeId> id) -> std::optional<std::uint64_t> {
      if (!id)
        return {};
      const auto &v = node(*id).value;
      if (const auto n = std::get_if<std::uint64_t>(&v))
        return *n;
      if (const auto n = std::get_if<std::int64_t>(&v); n && *n >= 0)
        return std::uint64_t(*n);
      if (const auto n = std::get_if<spsm::IntValues>(&v); n && n->count == 1 && n->values[0] >= 0)
        return std::uint64_t(n->values[0]);
      return {};
    };
    const auto raw = field(action.source, "perChannelBlending");
    if (!raw)
      return action.blending.empty() || reject(action.source);
    const auto rows = std::get_if<spsm::Array>(&node(*raw).value);
    if (!rows || rows->elements.size() != action.blending.size() || rows->elements.size() > 6)
      return reject(*raw);
    const auto channels = integral(field(action.source, "channelTypes"));
    if (!channels)
      return reject(action.source);
    std::uint64_t covered = 0;
    for (std::size_t i = 0; i < rows->elements.size(); ++i) {
      const auto id = rows->elements[i];
      const auto object = std::get_if<spsm::Object>(&node(id).value);
      if (!object || object->class_name != "DataBlending" || action.blending[i].source != id)
        return reject(id);
      std::set<std::string> seen;
      for (const auto &f : object->fields)
        if (!seen.insert(f.name).second || (f.name != "channelTypes" && f.name != "blendingMode" &&
                                            f.name != "opacity" && f.name != "uid"))
          return reject(id);
      const auto selected = integral(field(id, "channelTypes")),
                 mode = integral(field(id, "blendingMode"));
      if (!selected || !mode || !blend(*mode) ||
          (is_mask ? ((*selected != 0 && *selected != UINT64_MAX) || rows->elements.size() != 1) :
                     (!*selected || (*selected & ~*channels) || (*selected & covered))))
        return reject(id);
      covered |= *selected;
      if (const auto opacity = field(id, "opacity")) {
        const auto value = number(opacity);
        if (std::holds_alternative<bool>(node(*opacity).value) || !value ||
            !std::isfinite(*value) || *value < 0 || *value > 1)
          return reject(*opacity);
      }
    }
    return true;
  }
  std::optional<MeshGeneratorSettings> generator(const sem::Procedural &proc)
  {
    const auto name = package(proc);
    const auto t = tweaks(proc);
    MeshGeneratorSettings e;
    e.map_filter = 1;
    e.optional_inputs = true;
    e.surface_fallback = true;
    e.response = 4;
    e.axis = 1;
    const bool dirt = name.find("mg_dirt") != name.npos || name.find("rust") != name.npos ||
                      name.find("fiberglass") != name.npos;
    const bool edge = name.find("mask_builder") != name.npos ||
                      name.find("mask_editor") != name.npos ||
                      name.find("edge_wear") != name.npos || name.find("peeling") != name.npos;
    if (dirt)
      e.generator = 9;
    else if (edge)
      e.generator = 8;
    else if (name.find("curvature") != name.npos || name.find("edge_blur") != name.npos)
      e.generator = 2;
    else if (name.find("ambient_occlusion") != name.npos)
      e.generator = 3;
    else if (name.find("grunge") != name.npos || name.find("noise") != name.npos ||
             name.find("spots") != name.npos || name.find("cloud") != name.npos ||
             name.find("perlin") != name.npos || name.find("cells") != name.npos ||
             name.find("scratch") != name.npos || name.find("fiber") != name.npos ||
             name.find("matfinish_rough") != name.npos ||
             name.find("microscope_view") != name.npos || name.find("concrete") != name.npos ||
             name.find("alpha_dirt") != name.npos || name.find("leak") != name.npos ||
             name.find("drip") != name.npos || name.find("stain") != name.npos)
      e.generator = 7;
    else
      return {};
    const bool leak = name.find("leak") != name.npos || name.find("drip") != name.npos;
    const float level = tweak(t,
                              {"dirtlevel",
                               "wearlevel",
                               "peelinglevel",
                               "rustspreading",
                               "level",
                               "balance",
                               "globalbalance",
                               "histogramposition"},
                              e.generator == 7 ? .5f : .35f,
                              0,
                              1);
    e.contrast = 1 + 7 * tweak(t,
                               {"dirtcontrast",
                                "wearcontrast",
                                "rustcontrast",
                                "contrast",
                                "globalcontrast",
                                "historgramcontrast"},
                               .3f,
                               0,
                               1);
    e.bias = level - .5f;  // Increasing Painter Level/Balance increases coverage.
    e.invert = tweak(t, {"invert", "globalinvert"}, 0, 0, 1) != 0;
    e.grunge_amount = e.generator == 7 ?
                          0 :
                          tweak(
                              t, {"grungeamount", "grunge", "tex", "textureopacity"}, .25f, 0, 1);
    e.grunge_scale = tweak(
        t,
        {"grungescale", "dripsscale", "noisescale", "scale", "tile", "scaletex", "texturescale"},
        leak ? 3.f : 8.f,
        .01f,
        4096);
    e.grunge_seed = int(std::fmod(tweak(t, {"randomseed"}, 0, 0, 2147483647), 65536.f));
    e.grunge_pattern = leak                                                                 ? 3 :
                       name.find("scratch") != name.npos || name.find("fiber") != name.npos ? 2 :
                       name.find("spots") != name.npos                                      ? 1 :
                                                                                              0;
    e.triplanar_grunge =
        tweak(t, {"usetriplanar", "tex1trip", "texturetriplanar"}, leak ? 1.f : 0.f, 0, 1) != 0;
    if (edge && (tweak(t, {"curvatureconcaverange"}, 0.f, -1.f, 1.f) >
                     tweak(t, {"curvatureconvexrange"}, .5f, -1.f, 1.f) ||
                 tweak(t, {"curvaturemode"}, 1, 0, 2) == 2))
    {
      e.generator = 9;
    }
    e.ao_influence = tweak(
        t, {"aoinfluence", "ao", "ambientocclusionmasking", "aoopacity"}, 1, 0, 1);
    e.curvature_influence = tweak(t,
                                  {"curvatureinfluence",
                                   "curvature",
                                   "curvatureweight",
                                   "edgesmasking",
                                   "curv",
                                   "curvatureopacity"},
                                  1,
                                  0,
                                  1);
    e.position_influence = tweak(
        t,
        {"topdowngradient", "positioninfluence", "positiongradient", "positionopacity"},
        0,
        0,
        1);
    e.normal_influence = tweak(
        t, {"worldspacenormal", "normalinfluence", "ws", "wsopacity"}, 0, 0, 1);
    e.thickness_influence = tweak(
        t, {"thickness", "thicknessinfluence", "thick", "thicknessopacity"}, 0, 0, 1);
    unsupported_tweaks(proc,
                       {"dirtlevel",
                        "wearlevel",
                        "peelinglevel",
                        "rustspreading",
                        "level",
                        "balance",
                        "globalbalance",
                        "histogramposition",
                        "dirtcontrast",
                        "wearcontrast",
                        "contrast",
                        "globalcontrast",
                        "historgramcontrast",
                        "invert",
                        "globalinvert",
                        "grungeamount",
                        "grunge",
                        "grungescale",
                        "dripsscale",
                        "noisescale",
                        "scale",
                        "tile",
                        "randomseed",
                        "curvatureconvexrange",
                        "curvatureconcaverange",
                        "usetriplanar",
                        "aoinfluence",
                        "ao",
                        "ambientocclusionmasking",
                        "curvatureinfluence",
                        "curvature",
                        "curvatureweight",
                        "edgesmasking",
                        "topdowngradient",
                        "positioninfluence",
                        "worldspacenormal",
                        "normalinfluence",
                        "thickness",
                        "thicknessinfluence",
                        "tex",
                        "scaletex",
                        "curv",
                        "ws",
                        "thick",
                        "positiongradient",
                        "tex1trip",
                        "curvaturemode",
                        "curvaturesoft",
                        "curvaturemedium",
                        "curvaturelarge",
                        "curvaturebig",
                        "curvaturehuge",
                        "invertao",
                        "aoinvert",
                        "aoopacity",
                        "curvatureopacity",
                        "textureopacity",
                        "texturescale",
                        "texturetriplanar",
                        "wsopacity",
                        "positionopacity",
                        "thicknessopacity",
                        "rustcontrast"});
    const bool invert_ao = tweak(t, {"invertao", "aoinvert"}, 0, 0, 1) != 0;
    // AO-only editors must keep their cavity input even when curvature is off.
    if (edge && invert_ao && e.ao_influence > 0)
      e.generator = 9;
    if (edge) {
      e.curvature_radius = tweak(t, {"curvaturesoft"}, 0, 0, 1) * 2 +
                           tweak(t, {"curvaturemedium"}, 0, 0, 1) * 4 +
                           tweak(t, {"curvaturelarge"}, 0, 0, 1) * 8 +
                           tweak(t, {"curvaturebig"}, 0, 0, 1) * 16 +
                           tweak(t, {"curvaturehuge"}, 0, 0, 1) * 32;
      if (e.curvature_influence == 0 && e.grunge_amount > 0)
        e.generator = invert_ao ? 9 : 7;
    }
    generator_inputs(proc, e, t);
    if (leak && (e.generator == 8 || e.generator == 9)) {
      e.response = 2;
      e.grunge_pattern = 3;
      e.triplanar_grunge = true;
    }
    note(
        proc.source,
        SpsmDiagnosticKind::Assumption,
        "native-procedural-approximation",
        "Named package and typed tweaks mapped to our native live mesh-map/grunge generator; original graph is not read or executed and pixel equivalence is not claimed");
    return e;
  }
  std::optional<FilterSettings> filter(const sem::Procedural &proc)
  {
    const auto name = package(proc);
    const auto t = tweaks(proc);
    FilterSettings f;
    if (name.find("hsl_perceptive") != name.npos) {
      f.resource = 4;
      f.hue = tweak(t, {"hue", "hueshift"}, 0, -1, 1);
      f.saturation = 2 * tweak(t, {"saturation"}, .5f, 0, 1);
      f.value_scale = 2 * tweak(t, {"lightness", "luminosity"}, .5f, 0, 1);
    }
    else if (name.find("sharpen") != name.npos) {
      f.resource = 8;
      f.radius = tweak(t, {"radius"}, 1, 1, 4);
    }
    else if ((name.find("fx_warp") != name.npos || (name == "warp/warp" || name == "/warp/warp")))
    {
      f.resource = 9;
      f.value_scale = std::clamp(tweak(t, {"intensity"}, .2f, 0, 1000) /
                                     tweak(t, {"intensitydivider"}, 1, .001f, 1000),
                                 0.f,
                                 4.f);
      f.radius = tweak(t, {"noisescale", "radius", "scale"}, 2, 1, 4);
      f.hue = tweak(t, {"warpangle", "angle"}, 0, -1, 1);
    }
    else if (name.find("blur_slope") != name.npos) {
      f.resource = 10;
      f.value_scale = tweak(t, {"intensity"}, .2f, 0, 4);
      f.radius = tweak(t, {"radius"}, 2, 1, 4);
    }
    else if (name.find("blur") != name.npos) {
      f.resource = 7;
      f.radius = tweak(t, {"blurradius", "radius", "intensity"}, 1, 1, 4);
    }
    else
      return {};
    unsupported_tweaks(proc,
                       {"hue",
                        "hueshift",
                        "saturation",
                        "lightness",
                        "luminosity",
                        "radius",
                        "intensity",
                        "intensitydivider",
                        "noisescale",
                        "scale",
                        "warpangle",
                        "angle",
                        "blurradius",
                        "sharpenalpha"});
    if (proc.inputs)
      note(
          *proc.inputs,
          SpsmDiagnosticKind::Omission,
          "native-filter-input-omitted",
          "Custom filter inputs remain in source; the native filter uses its own editable deterministic kernel rather than executing those input graphs");
    note(
        proc.source,
        SpsmDiagnosticKind::Assumption,
        "native-filter-approximation",
        "Named source filter mapped to our editable native filter; parameter scaling and appearance are native approximations");
    return f;
  }
  const sem::Procedural *procedure(NodeId id) const
  {
    const auto found = procedurals.find(id);
    return found == procedurals.end() ? nullptr : found->second;
  }
  std::optional<std::array<float, 3>> procedural_color(const sem::Procedural &proc)
  {
    std::optional<NodeId> selected;
    for (const auto id : proc.tweaks) {
      const auto object = std::get_if<spsm::Object>(&node(id).value);
      if (!object ||
          (object->class_name != "DataTweakColor" && object->class_name != "DataTweakFloat3"))
        continue;
      const auto name = key(string(field(id, "identifier")));
      if (name != "color" && name != "basecolor" && name != "diffusecolor")
        continue;
      if (selected) {
        note(
            id,
            SpsmDiagnosticKind::Omission,
            "native-generator-color-ambiguous",
            "Multiple recognized source Color controls remain in the retained graph; no arbitrary first control is used as a tint");
        return {};
      }
      selected = id;
    }
    if (!selected)
      return {};
    if (!transform) {
      note(
          *selected,
          SpsmDiagnosticKind::Omission,
          "native-generator-color-transform-missing",
          "Source Color is retained but requires an explicit named scene-linear conversion before it can tint a native generator");
      return {};
    }
    const auto color = field(*selected, "value");
    const auto values = color ? std::get_if<spsm::FloatValues>(&node(*color).value) : nullptr;
    const auto valid = [](const std::array<float, 3> &rgb) {
      return std::all_of(rgb.begin(), rgb.end(), [](float v) {
        return std::isfinite(v) && std::abs(v) <= 65504;
      });
    };
    if (!values || values->count < 3) {
      note(*selected,
           SpsmDiagnosticKind::Omission,
           "native-generator-color-invalid",
           "Source Color has no supported RGB value; the original typed parameter is retained");
      return {};
    }
    std::array<float, 3> rgb{values->values[0], values->values[1], values->values[2]};
    if (!valid(rgb)) {
      note(*selected,
           SpsmDiagnosticKind::Omission,
           "native-generator-color-invalid",
           "Source Color exceeds the native finite RGB domain; its typed value is retained");
      return {};
    }
    rgb = transform(rgb);
    if (!valid(rgb)) {
      note(
          *selected,
          SpsmDiagnosticKind::Omission,
          "native-generator-color-invalid",
          "Named Color conversion produced values outside the native finite RGB domain; no tint is installed");
      return {};
    }
    consumed_colors.insert(*selected);
    note(
        *selected,
        SpsmDiagnosticKind::Assumption,
        "native-generator-color-source",
        "Recognized Color RGB is converted once to scene-linear values and tints the native scalar source before blend/opacity; proprietary multi-output and procedural alpha graphs remain unevaluated");
    return rgb;
  }
  std::optional<std::array<float, 3>> uniform(const sem::Source &source, int channel)
  {
    if (!source.color)
      return {};
    const auto v = std::get_if<spsm::FloatValues>(&node(*source.color).value);
    if (!v || v->count != 3)
      return {};
    std::array<float, 3> result{v->values[0], v->values[1], v->values[2]};
    for (float f : result)
      if (!std::isfinite(f))
        return {};
    if (channel == 0) {
      if (!transform)
        return {};
      return transform(result);
    }
    if (channel == 4)
      for (float &f : result)
        f += .5f;
    return result;
  }
  void channel(TextureLayer &out, int channel)
  {
    if (channel == 5) {
      out.ao_enabled = true;
      result.material.set.ao_channel = true;
    }
    else
      out.channels[channel] = true;
  }
  bool mask_source(const sem::Source &source,
                   const sem::Action &action,
                   TextureLayer &out,
                   float group_opacity)
  {
    auto [opacity, mode] = blending(action, 2, true);
    opacity *= group_opacity * std::clamp(float(number(source.opacity).value_or(1)), 0.f, 1.f);
    MaskEffect e;
    e.opacity = opacity;
    e.blending = mode;
    e.name = "Fill";
    if (source.kind == sem::Source::Kind::Uniform) {
      const auto value = uniform(source, 2);
      if (!value)
        return false;
      e.kind = MaskEffectKind::Fill;
      e.fill = (*value)[0];
    }
    else if (source.kind == sem::Source::Kind::Procedural && source.procedural) {
      if (const auto projection = field(action.source, "projection");
          projection && mask(projection) != 0)
        note(
            *projection,
            SpsmDiagnosticKind::Assumption,
            "native-procedural-projection",
            "Authored procedural projection has no direct native graph mapping; native generator coordinates are used and the original projection value is retained");
      const auto proc = procedure(*source.procedural);
      if (!proc)
        return false;
      auto settings = generator(*proc);
      if (!settings)
        return false;
      const auto pname = package(*proc);
      e.kind = MaskEffectKind::Generator;
      static_cast<MeshGeneratorSettings &>(e) = *settings;
      e.name = pname;
    }

    else if (source.kind == sem::Source::Kind::Bitmap &&
             result.bitmap_mask_sources.contains(source.source))
    {
      e.kind = MaskEffectKind::Fill;
      e.fill = 1;
      e.mapping.projection = result.bitmap_mask_sources.at(source.source);
      e.name = "Bitmap Fill";
    }
    else if (source.kind == sem::Source::Kind::Bitmap && source.bitmap) {
      const auto uri = string(field(*source.bitmap, "urlToBitmapRes"));
      e.kind = MaskEffectKind::Generator;
      e.map_filter = 1;
      e.optional_inputs = true;
      if (uri == "resourcealias:AmbientOcclusion")
        e.generator = 3;
      else if (uri == "resourcealias:Curvature")
        e.generator = 2;
      else if (uri == "resourcealias:Position")
        e.generator = 1;
      else if (uri == "resourcealias:NormalWorldSpace")
        e.generator = 6;
      else if (uri == "resourcealias:Thickness")
        e.generator = 4;
      else
        return false;
      e.name = uri;
      note(
          source.source,
          SpsmDiagnosticKind::Assumption,
          "native-meshmap-alias",
          "Optional native mesh-map alias uses the destination baked map when present; before baking this subaction contributes no generated value and authored base Fill remains retained");
    }
    else
      return false;
    out.mask->effects.push_back(std::move(e));
    return true;
  }
  void actions(const std::vector<std::size_t> &indices,
               TextureLayer &out,
               bool is_mask,
               bool &initial)
  {
    struct Pending {
      std::size_t index;
      std::array<float, 6> opacity{1, 1, 1, 1, 1, 1};
    };
    std::vector<Pending> todo;
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
      todo.push_back({*it});
    while (!todo.empty()) {
      const auto current = todo.back();
      todo.pop_back();
      const auto &a = ir.actions.at(current.index);
      if (!enabled(a.enabled))
        continue;
      bool any = false;
      if (a.kind == sem::ActionKind::Group) {
        if (!a.children.empty()) {
          if (is_mask) {
            const auto [op, mode] = blending(a, 0, true);
            if (mode != LayerBlend::Passthrough) {
              TextureLayer group;
              group.mask = LayerMask{};
              group.mask->background = 0;
              bool unused = false;
              actions(a.children, group, true, unused);
              MaskEffect effect;
              effect.kind = MaskEffectKind::Group;
              effect.name = "Group";
              effect.opacity = op * current.opacity[0];
              effect.blending = mode;
              effect.group_effects = std::make_shared<const std::vector<MaskEffect>>(
                  std::move(group.mask->effects));
              out.mask->effects.push_back(std::move(effect));
            }
            else {
              auto inherited = current.opacity;
              inherited[0] *= op;
              for (auto it = a.children.rbegin(); it != a.children.rend(); ++it)
                todo.push_back({*it, inherited});
            }
          }
          else {
            // A group's Color row cannot stand in for Metallic/Roughness/Bump/AO.
            // Disabled is a zero contribution for this channel, including filters.
            auto inherited = current.opacity;
            bool approximate = false;
            for (int c = 0; c < 6; ++c) {
              const auto [op, mode] = blending(a, c, false);
              inherited[c] *= mode == LayerBlend::Disabled ? 0.f : op;
              approximate |= mode != LayerBlend::Passthrough && mode != LayerBlend::Disabled;
            }
            if (approximate)
              note(
                  a.source,
                  SpsmDiagnosticKind::Assumption,
                  "native-content-group",
                  "Content group children retain source order, per-channel inherited opacity and Disabled channel state; isolated content-group blending remains approximate");
            for (auto it = a.children.rbegin(); it != a.children.rend(); ++it)
              todo.push_back({*it, inherited});
          }
        }
        lowered(a.source);
        continue;
      }
      if (!is_mask && color_boundary &&
          (!initial_fill_action || a.source != *initial_fill_action) &&
          a.kind != sem::ActionKind::Group && a.kind != sem::ActionKind::Levels &&
          (mask(field(a.source, "channelTypes"), ~std::uint64_t(0)) & 1))
        emit_boundary(out);
      if (a.kind == sem::ActionKind::Fill) {
        for (const auto id : a.sources) {
          const auto found = sources.find(id);
          if (found == sources.end())
            continue;
          const auto &source = *found->second;
          if (is_mask) {
            if (mask(field(id, "channelTypes")) == 0 || source.kind == sem::Source::Kind::Bitmap ||
                standalone_mask())
            {
              if (standalone_mask() && mask(field(id, "channelTypes")) != 0)
                note(
                    id,
                    SpsmDiagnosticKind::Assumption,
                    "native-smart-mask-source-channel",
                    "Standalone DataSmartMask source channel selection is interpreted as scalar mask input, not filtered through material channel bits; raw scalar R/native procedural output is used");
              any |= mask_source(source, a, out, current.opacity[0]);
            }
            continue;
          }
          const auto source_bits = mask(field(id, "channelTypes"));
          const auto action_bits = mask(field(a.source, "channelTypes"), ~std::uint64_t(0));
          for (int c : {0, 1, 2, 3, 4, 5})
            if (source_bits & action_bits & bits[c]) {
              auto [opacity, mode] = blending(a, c, false);
              opacity *= current.opacity[c] *
                         std::clamp(float(number(source.opacity).value_or(1)), 0.f, 1.f);
              LevelsEffect e;
              e.channel = c;
              e.opacity = opacity;
              e.blending = mode;
              if (source.kind == sem::Source::Kind::Uniform) {
                const auto value = uniform(source, c);
                if (!value)
                  continue;
                e.kind = ContentEffectKind::Fill;
                e.fill_value = *value;
                e.name = "Fill";
              }
              else if (source.kind == sem::Source::Kind::Procedural && source.procedural) {
                if (c == 3) {
                  note(
                      id,
                      SpsmDiagnosticKind::Omission,
                      "native-normal-graph-unavailable",
                      "Authored Normal procedural data remains in the retained source; this proprietary normal-producing graph is not evaluated or replaced by scalar noise");
                  continue;
                }
                const auto proc = procedure(*source.procedural);
                if (!proc)
                  continue;
                const auto settings = generator(*proc);
                if (!settings)
                  continue;
                e.kind = ContentEffectKind::Generator;
                static_cast<MeshGeneratorSettings &>(e) = *settings;
                e.name = package(*proc);
                if (c == 0)
                  e.generator_color = procedural_color(*proc);
              }
              else if (source.kind == sem::Source::Kind::Bitmap && initial_fill_action &&
                       a.source == *initial_fill_action && fill_channel_texture(out.fill, c))
              {
                e.kind = ContentEffectKind::Fill;
                e.source_coverage = true;
                e.name = "Source coverage";
              }
              else if (source.kind == sem::Source::Kind::Bitmap &&
                       result.bitmap_content_sources.contains(source.source) &&
                       result.bitmap_content_channels.contains(source.source) &&
                       (result.bitmap_content_channels.at(source.source) & bits[c]))
              {
                e.kind = ContentEffectKind::Fill;
                e.bitmap_fill.emplace();
                e.bitmap_fill->projection = result.bitmap_content_sources.at(source.source);
                e.fill_value.fill(c == 4 ? .5f : 0.f);
                e.name = "Bitmap Fill";
                if (c == 0)
                  e.linear_color = result.bitmap_color_transform;
              }
              else
                continue;
              if (!seen_content[c]) {
                if (e.kind == ContentEffectKind::Fill)
                  e.initialize_content = true;
                else {
                  LevelsEffect empty;
                  empty.kind = ContentEffectKind::Fill;
                  empty.channel = c;
                  empty.opacity = 0;
                  empty.initialize_content = true;
                  empty.fill_value = {0, 0, 0};
                  empty.name = "Empty source";
                  out.effects.push_back(std::move(empty));
                }
                seen_content[c] = true;
              }
              channel(out, c);
              if (c == 3) {
                result.material.set.channels[3] = true;
                note(
                    id,
                    SpsmDiagnosticKind::Assumption,
                    "native-internal-normal",
                    "Encoded tangent Normal retained in the hidden internal native channel using the destination tangent convention; source handedness is not verified and no public Normal channel or export is added");
              }
              out.effects.push_back(std::move(e));
              any = true;
            }
        }
        if (!is_mask)
          initial = false;
      }
      else if (a.kind == sem::ActionKind::Levels) {
        try {
          if (!levels_mix_supported(a, is_mask))
            throw SpsmLevelsUnsupported{a.source};
          if (is_mask) {
            MaskEffect e;
            e.kind = MaskEffectKind::Levels;
            e.name = "Levels";
            e.levels = parse_spsm_scalar_levels(p,
                                                a,
                                                SpsmLevelsPolicy::NativeScalarLevels,
                                                SpsmScalarDomain::MaskAuxiliary0,
                                                true);
            e.levels.id = e.id;
            if (mask(field(a.source, "channelTypes")) == UINT64_MAX)
              note(
                  a.source,
                  SpsmDiagnosticKind::Assumption,
                  "native-mask-all-channels-levels",
                  "All-channel Levels sentinel is interpreted as one scalar curve in this mask stack under the native Levels policy");
            const auto [op, mode] = blending(a, 2, true);
            e.opacity = op * current.opacity[0];
            e.blending = mode;
            out.mask->effects.push_back(std::move(e));
          }
          else {
            std::vector<LevelsEffect> curves;
            static constexpr std::array<std::string_view, 6> omitted{
                "native-levels-color-omitted",
                "native-levels-metallic-omitted",
                "native-levels-roughness-omitted",
                "native-levels-normal-omitted",
                "native-levels-bump-omitted",
                "native-levels-ao-omitted"};
            for (int c : {0, 1, 2, 4, 5})
              if (mask(field(a.source, "channelTypes")) & bits[c])
                try {
                  auto e = spsm_levels_detail::Parser(p, true).ordered_curve(a, c, bits[c]);
                  e.name = "Levels";
                  const auto [op, mode] = blending(a, c, false);
                  e.opacity = op * current.opacity[c];
                  e.blending = mode;
                  curves.push_back(std::move(e));
                }
                catch (const SpsmLevelsUnsupported &error) {
                  note(
                      error.source,
                      SpsmDiagnosticKind::Omission,
                      omitted[c],
                      "Unsupported selected Levels channel remains in source; supported sibling curves retain their authored action position and mix");
                }
            if (curves.empty())
              throw SpsmLevelsUnsupported{a.source};
            out.effects.insert(out.effects.end(),
                               std::make_move_iterator(curves.begin()),
                               std::make_move_iterator(curves.end()));
          }
          any = true;
        }
        catch (const SpsmLevelsUnsupported &) {
        }
      }
      else if (a.kind == sem::ActionKind::ProceduralFilter) {
        const auto ref = field(a.source, "procedural");
        const auto proc = ref ? procedure(*ref) : nullptr;
        if (proc) {
          if (const auto settings = filter(*proc)) {
            if (is_mask) {
              MaskEffect e;
              e.kind = MaskEffectKind::Filter;
              e.filter = *settings;
              e.name = package(*proc);
              const auto [op, mode] = blending(a, 2, true);
              e.opacity = op * current.opacity[0];
              if (settings->resource == 8)
                e.opacity *= tweak(tweaks(*proc), {"sharpenalpha", "intensity"}, 1, 0, 1);
              e.blending = mode;
              out.mask->effects.push_back(std::move(e));
            }
            else
              for (int c : {0, 1, 2, 4, 5})
                if (mask(field(a.source, "channelTypes"), ~std::uint64_t(0)) & bits[c]) {
                  LevelsEffect e;
                  e.kind = ContentEffectKind::Filter;
                  e.filter = *settings;
                  e.channel = c;
                  e.name = package(*proc);
                  const auto [op, mode] = blending(a, c, false);
                  e.opacity = op * current.opacity[c];
                  if (settings->resource == 8)
                    e.opacity *= tweak(tweaks(*proc), {"sharpenalpha", "intensity"}, 1, 0, 1);
                  e.blending = mode;
                  out.effects.push_back(std::move(e));
                }
            any = true;
          }
          else if (const auto settings = generator(*proc)) {
            const auto name = package(*proc);
            if (is_mask) {
              MaskEffect e;
              e.kind = MaskEffectKind::Generator;
              static_cast<MeshGeneratorSettings &>(e) = *settings;
              const auto [op, mode] = blending(a, 2, true);
              e.opacity = op * current.opacity[0];
              e.blending = mode;
              e.name = name;
              out.mask->effects.push_back(std::move(e));
              any = true;
            }
            else {
              // This approximation is an action-local source, including rust/peeling.
              // Changing the layer's base values or shared mask would also change
              // earlier/later actions and bypass the authored channel mix entirely.
              for (int c : {0, 1, 2, 4, 5})
                if (mask(field(a.source, "channelTypes"), ~std::uint64_t(0)) & bits[c]) {
                  LevelsEffect e;
                  e.kind = ContentEffectKind::Generator;
                  static_cast<MeshGeneratorSettings &>(e) = *settings;
                  e.channel = c;
                  e.name = name;
                  const auto [op, mode] = blending(a, c, false);
                  e.opacity = op * current.opacity[c];
                  e.blending = mode;
                  channel(out, c);
                  out.effects.push_back(std::move(e));
                  any = true;
                }
              note(
                  proc->source,
                  SpsmDiagnosticKind::Assumption,
                  "native-generator-filter-approximation",
                  "Native scalar generator is mixed at this filter action's position on each selected supported channel; proprietary multi-output colors and input graph processing are not evaluated, and authored layer base/mask values remain unchanged");
            }
          }
        }
      }
      if (any)
        lowered(a.source);
      else
        note(
            a.source,
            SpsmDiagnosticKind::Omission,
            "native-subaction-omitted",
            "Unsupported subaction retained in source and omitted independently; authored layer visibility, channels and supported content remain active");
    }
  }

 public:
  Lowerer(PreparedSpsm &prepared, std::function<std::array<float, 3>(std::array<float, 3>)> color)
      : result(prepared),
        p(prepared.source->source().presets.at(*prepared.source->primary_preset)),
        ir(prepared.source->presets.at(*prepared.source->primary_preset)),
        transform(std::move(color))
  {
    for (const auto &s : ir.sources)
      sources.emplace(s.source, &s);
    for (const auto &s : ir.procedurals)
      procedurals.emplace(s.source, &s);
  }
  void run()
  {
    for (const auto &src : ir.layers) {
      const auto binding = std::find_if(result.layers.begin(),
                                        result.layers.end(),
                                        [&](const auto &b) { return b.source == src.source; });
      if (binding == result.layers.end())
        continue;
      auto layer = std::find_if(result.material.set.layers.begin(),
                                result.material.set.layers.end(),
                                [&](const auto &l) { return l.id == binding->native_layer; });
      if (layer == result.material.set.layers.end())
        continue;
      auto &out = *layer;
      out.visible = enabled(src.enabled);
      bool initial = true;
      seen_content.fill(false);
      if (standalone_mask() && result.source->source().source != "synthetic.spmsk") {
        if (!out.mask)
          out.mask = LayerMask{};
        out.mask->background = 0;
      }
      if (standalone_mask())
        note(
            p.root,
            SpsmDiagnosticKind::Assumption,
            "smart-mask-background-policy",
            "Standalone DataSmartMask stores no initial value. Real Painter comparisons start from an explicit black mask (0); synthetic legacy fixtures retain white (1). This is an application policy, not authored graph data");
      // Action coverage belongs to the source/effect, never the whole layer.
      for (int c = 0; c < 6; ++c) {
        float opacity = 1;
        for (const auto &row : src.blending)
          if (mask(row.channel_types) & bits[c])
            opacity = std::clamp(float(number(row.opacity).value_or(1)), 0.f, 1.f);
        if (c == 5)
          out.ao_opacity = opacity;
        else
          out.opacity[c] = opacity;
      }
      if (!out.mask)
        out.mask = LayerMask{};
      out.mask->effects.clear();
      bool unused = false;
      actions(src.mask_actions, out, true, unused);
      initial_fill_action.reset();
      if (const auto retained = result.initial_fill_actions.find(src.source);
          retained != result.initial_fill_actions.end())
        initial_fill_action = retained->second;
      color_boundary.reset();
      boundary_emitted = false;
      for (const auto &effect : out.effects)
        if (effect.kind == ContentEffectKind::LinearColorTransform)
          color_boundary = effect.linear_color;
      out.effects.clear();
      actions(src.actions, out, false, initial);
      emit_boundary(out);
      for (auto &effect : out.effects)
        if (effect.kind == ContentEffectKind::Generator)
          effect.optional_inputs = true;
      if (out.mask)
        for (auto &effect : mask_effects_recursive(out.mask->effects))
          if (effect.kind == MaskEffectKind::Generator)
            effect.optional_inputs = true;
      binding->disabled = false;
    }
    std::erase_if(result.diagnostics, [&](const auto &d) {
      if (d.code == "native-procedural-parameter-omitted" && consumed_colors.contains(d.source))
        return true;
      if (d.code != "pending-mask-action" && d.code != "pending-content-action")
        return false;
      return std::any_of(result.actions.begin(), result.actions.end(), [&](const auto &a) {
        return a.source == d.source && a.lowered;
      });
    });
  }
};
}  // namespace spsm_native_detail
inline void lower_spsm_native_actions(
    PreparedSpsm &prepared,
    std::function<std::array<float, 3>(std::array<float, 3>)> transform = {})
{
  if (!prepared.source || !prepared.source->primary_preset)
    return;
  spsm_native_detail::Lowerer(prepared, std::move(transform)).run();
}
}  // namespace bts
