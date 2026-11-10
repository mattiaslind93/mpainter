// SPDX-License-Identifier: GPL-3.0-or-later
// Distinct owned LINEAR Color UDIM family. RawUdimFilter's graph bytes and
// provenance remain unchanged; only its strict standard-node builder is reused.
struct ColorUdimProvenance {
  std::shared_ptr<const FillTexture> source;
  bool encoded = false;
};
ColorUdimProvenance color_udim_source(bNodeTree &tree, const std::string &prefix)
{
  auto *n = bke::node_find_node_by_name(tree, prefix + "Representation");
  if (!n || !STREQ(n->idname, "ShaderNodeValue") || n->id || (n->flag & NODE_MUTED) || !n->prop ||
      n->prop->type != IDP_GROUP || BLI_listbase_count(&n->prop->data.group) != 1)
    throw std::invalid_argument("Linear Color UDIM provenance is missing");
  auto *p = IDP_GetPropertyFromGroup(n->prop, "source");
  if (!p || p->type != IDP_STRING || p->len < 1 || !IDP_string_get(p) ||
      IDP_string_get(p)[p->len - 1])
    throw std::invalid_argument("Linear Color UDIM provenance changed");
  QJsonParseError error;
  const auto json = QJsonDocument::fromJson(QByteArray::fromRawData(IDP_string_get(p), p->len - 1),
                                            &error);
  if (error.error != QJsonParseError::NoError || !json.isObject())
    throw std::invalid_argument("Invalid linear Color UDIM metadata");
  const auto o = json.object();
  if (o.size() != 4 || o["contract"] != "linear-color-udim-extend-float-bilinear-v1" ||
      !o["encoded"].isBool() || !o["members"].isArray())
    throw std::invalid_argument("Unsupported linear Color UDIM provenance");
  const auto descriptor = read_fill_texture_json(o["source"]);
  if (!descriptor || descriptor->raw || !descriptor->tiles ||
      fill_texture_json(descriptor) != o["source"])
    throw std::invalid_argument("Linear Color UDIM source identity changed");
  auto source = std::make_shared<FillTexture>(*descriptor);
  auto members = std::make_shared<std::map<int, std::shared_ptr<const FillTexture>>>();
  for (const auto &value : o["members"].toArray()) {
    const auto row = value.toObject();
    const auto number = row["tile"];
    const int tile = number.toInt();
    if (!number.isDouble() || number.toDouble() != tile || !descriptor->tiles->contains(tile) ||
        !row["input_space"].isString() || row["input_space"].toString().isEmpty())
      throw std::invalid_argument("Linear Color UDIM member color identity changed");
    auto member = std::make_shared<FillTexture>(*descriptor->tiles->at(tile));
    member->input_space = row["input_space"].toString().toStdString();
    if (!members->emplace(tile, member).second)
      throw std::invalid_argument("Duplicate linear Color UDIM member");
  }
  if (members->size() != descriptor->tiles->size())
    throw std::invalid_argument("Linear Color UDIM processor table is incomplete");
  source->tiles = std::move(members);
  source->input_space = source->tiles->begin()->second->input_space;
  if (material_layer_color_members_json(*source) != o["members"].toArray())
    throw std::invalid_argument("Linear Color UDIM processor/matrix provenance changed");
  return {std::move(source), o["encoded"].toBool()};
}
struct ColorUdimFilter : RawUdimFilter {
  std::string root;
  bool encoded;
  ColorUdimFilter(Builder &builder,
                  std::string prefix,
                  std::string root,
                  const FillTexture &source,
                  bool encoded,
                  bool check)
      : RawUdimFilter{builder, std::move(prefix), source, check},
        root(std::move(root)),
        encoded(encoded)
  {
  }
  Source build(const std::map<int, Image *> *images = nullptr)
  {
    if (source.raw || !source.tiles)
      throw std::invalid_argument("Linear Color UDIM graph requires a Color sequence");
    const auto transforms = material_layer_color_source_transforms(source);
    auto &marker = node("ShaderNodeValue", "Representation");
    scalar(b.out({&marker, "Value"}), 1);
    if (check) {
      const auto old = color_udim_source(b.tree, prefix);
      if (old.encoded != encoded ||
          fill_texture_json(old.source) !=
              fill_texture_json(std::make_shared<const FillTexture>(source)) ||
          material_layer_color_members_json(*old.source) !=
              material_layer_color_members_json(source))
        throw std::invalid_argument("Linear Color UDIM graph differs from its typed source");
    }
    else {
      IDPropertyTemplate empty{};
      if (!marker.prop)
        marker.prop = IDP_New(IDP_GROUP, &empty, "Linear Color UDIM provenance");
      const QJsonObject metadata{
          {"contract", "linear-color-udim-extend-float-bilinear-v1"},
          {"source", fill_texture_json(std::make_shared<const FillTexture>(source))},
          {"members", material_layer_color_members_json(source)},
          {"encoded", encoded}};
      const auto bytes = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
      IDP_ReplaceInGroup(marker.prop, IDP_NewString(bytes.constData(), "source"));
      b.changed = true;
    }
    auto *mapping = bke::node_find_node_by_name(b.tree, root + "Mapping");
    if (!mapping || !STREQ(mapping->idname, "ShaderNodeMapping") || (mapping->flag & NODE_MUTED))
      throw std::invalid_argument("Linear Color UDIM mapping is missing");
    const auto floor = vector("Floor", NODE_VECTOR_MATH_FLOOR, {mapping, "Vector"}, {0, 0, 0});
    auto &xy = node("ShaderNodeSeparateXYZ", "FloorXY");
    edge(floor, xy, b.in(xy, "Vector"));
    auto &fallback = node("ShaderNodeRGB", "Missing");
    auto *rgba = static_cast<bNodeSocketValueRGBA *>(b.out({&fallback}).default_value)->value;
    const std::array<float, 4> magenta{1, 0, 1, 1};
    if (check) {
      if (!std::equal(magenta.begin(), magenta.end(), rgba))
        throw std::invalid_argument("Linear Color UDIM missing sentinel changed");
    }
    else {
      std::copy(magenta.begin(), magenta.end(), rgba);
      BKE_ntree_update_tag_node_property(&b.tree, &fallback);
      b.changed = true;
    }
    Source result{&fallback};
    int index = 0;
    for (const auto &[tile, member] : *source.tiles) {
      const auto t = "Tile" + std::to_string(tile);
      const float x = (tile - 1001) % 10, y = (tile - 1001) / 10;
      const auto compare = [&](const char *component, float value) {
        auto &n = node("ShaderNodeMath", t + component);
        op(n, NODE_MATH_COMPARE);
        edge({&xy, component}, n, b.in(n, 0));
        scalar(b.in(n, 1), value);
        scalar(b.in(n, 2), .25f);
        return Source{&n, "Value"};
      };
      const auto gate = math(t + "Gate", NODE_MATH_MULTIPLY, compare("X", x), compare("Y", y));
      const auto local = vector(t + "UV", NODE_VECTOR_MATH_ADD, {mapping, "Vector"}, {-x, -y, 0});
      if (member->width < 1 || member->height < 1)
        throw std::invalid_argument("Linear Color UDIM member dimensions changed");
      // Nearest texel centers plus shader float weights avoid hardware linear
      // fraction quantization. EXTEND clamps each tap, never the signed RGB.
      const auto pixel = vector(t + "Pixels",
                                NODE_VECTOR_MATH_MULTIPLY,
                                local,
                                {float(member->width), float(member->height), 0});
      const auto centered = vector(t + "Centered", NODE_VECTOR_MATH_ADD, pixel, {-.5f, -.5f, 0});
      const auto texel_floor = vector(t + "Floor", NODE_VECTOR_MATH_FLOOR, centered, {0, 0, 0});
      const auto fraction = vector(t + "Fraction", NODE_VECTOR_MATH_FRACTION, centered, {0, 0, 0});
      auto &fraction_xy = node("ShaderNodeSeparateXYZ", t + "FractionXY");
      edge(fraction, fraction_xy, b.in(fraction_xy, "Vector"));
      std::array<Source, 4> texels;
      ID *identity = nullptr;
      for (int tap = 0; tap < 4; ++tap) {
        const auto center = vector(t + "Center" + std::to_string(tap),
                                   NODE_VECTOR_MATH_ADD,
                                   texel_floor,
                                   {tap % 2 ? 1.5f : .5f, tap / 2 ? 1.5f : .5f, 0});
        const auto uv = vector(t + "TexelUv" + std::to_string(tap),
                               NODE_VECTOR_MATH_MULTIPLY,
                               center,
                               {1.f / member->width, 1.f / member->height, 0});
        auto &image = node("ShaderNodeTexImage",
                           t + "Image" + (tap ? "Tap" + std::to_string(tap) : std::string{}));
        if (!image.storage)
          throw std::invalid_argument("Linear Color UDIM image settings missing");
        auto &settings = *static_cast<NodeTexImage *>(image.storage);
        if (check) {
          if (settings.extension != SHD_IMAGE_EXTENSION_EXTEND ||
              settings.interpolation != SHD_INTERP_CLOSEST ||
              settings.projection != SHD_PROJ_FLAT || !image.id)
            throw std::invalid_argument("Linear Color UDIM closest sampling changed");
        }
        else {
          if (images) {
            if (!images->contains(tile) || !images->at(tile))
              throw std::invalid_argument("Linear Color UDIM member is not GPU-ready");
            if (image.id != &images->at(tile)->id) {
              if (image.id)
                id_us_min(image.id);
              image.id = &images->at(tile)->id;
              id_us_plus(image.id);
            }
          }
          else if (!image.id)
            throw std::invalid_argument("Linear Color UDIM conversion lost its audited Image");
          settings.extension = SHD_IMAGE_EXTENSION_EXTEND;
          settings.interpolation = SHD_INTERP_CLOSEST;
          settings.projection = SHD_PROJ_FLAT;
          BKE_ntree_update_tag_node_property(&b.tree, &image);
          b.changed = true;
        }
        const auto &u = settings.iuser;
        const auto &m = settings.base.tex_mapping;
        if (u.scene || u.frames != 100 || u.sfra != 1 || u.offset || u.cycl || u.multiview_eye ||
            u.pass || u.multi_index || u.view || u.layer || (u.tile != 0 && u.tile != 1001) ||
            (u.flag & ~(IMA_NEED_FRAME_RECALC | IMA_USER_FRAME_IN_RANGE)) || m.ob ||
            m.type != TEXMAP_TYPE_POINT || m.projx != PROJ_X || m.projy != PROJ_Y ||
            m.projz != PROJ_Z || m.mapping != MTEX_FLAT || (m.flag & ~TEXMAP_UNIT_MATRIX))
          throw std::invalid_argument("Linear Color UDIM hidden image context changed");
        for (int c = 0; c < 3; ++c)
          if (m.loc[c] || m.rot[c] || m.size[c] != 1)
            throw std::invalid_argument("Linear Color UDIM hidden SRT changed");
        for (int c = 0; c < 4; ++c)
          for (int r = 0; r < 4; ++r)
            if (m.mat[c][r] != (c == r ? 1.f : 0.f))
              throw std::invalid_argument("Linear Color UDIM hidden matrix changed");
        if (identity && image.id != identity)
          throw std::invalid_argument("Linear Color UDIM taps do not share one Image");
        identity = image.id;
        edge(uv, image, b.in(image, "Vector"));
        texels[tap] = {&image};
        if (!tap)
          samples.emplace(tile, &image);
      }
      const auto low = mix(t + "MixLow", {&fraction_xy, "X"}, texels[0], texels[1]),
                 high = mix(t + "MixHigh", {&fraction_xy, "X"}, texels[2], texels[3]);
      const auto sampled = mix(t + "Bilinear", {&fraction_xy, "Y"}, low, high);
      auto &ap1 = node("ShaderNodeCombineXYZ", t + "ToAP1");
      const char *axes[]{"X", "Y", "Z"};
      const auto &transform = transforms.at(tile);
      for (int row = 0; row < 3; ++row) {
        auto &dot = node("ShaderNodeVectorMath", t + "ToAP1" + std::to_string(row));
        op(dot, NODE_VECTOR_MATH_DOT_PRODUCT);
        edge(sampled, dot, b.in(dot, 0));
        const std::array<float, 3> coefficient = encoded ?
                                                     std::array<float, 3>{
                                                         transform.to_aces[row],
                                                         transform.to_aces[3 + row],
                                                         transform.to_aces[6 + row]} :
                                                     std::array<float, 3>{row == 0 ? 1.f : 0.f,
                                                                          row == 1 ? 1.f : 0.f,
                                                                          row == 2 ? 1.f : 0.f};
        vector(b.in(dot, 1), coefficient);
        edge({&dot, "Value"}, ap1, b.in(ap1, axes[row]));
      }
      result = mix("Mix" + std::to_string(index++), gate, result, {&ap1, "Vector"});
    }
    auto &final = node("ShaderNodeVectorMath", "Mixed");
    op(final, NODE_VECTOR_MATH_ADD);
    edge(result, final, b.in(final, 0));
    vector(b.in(final, 1), {0, 0, 0});
    return {&final, "Vector"};
  }
  void audit()
  {
    auto *final = bke::node_find_node_by_name(b.tree, prefix + "Mixed");
    for (const auto &n : b.tree.nodes)
      if (std::string(n.name).starts_with(prefix) && !nodes.contains(const_cast<bNode *>(&n)))
        throw std::invalid_argument("Unknown linear Color UDIM namespace node");
    std::set<int> outputs;
    for (const auto &l : b.tree.links)
      if (nodes.contains(l.fromnode) || nodes.contains(l.tonode)) {
        if (edges.contains({l.fromsock, l.tosock}))
          continue;
        bool known = false;
        for (int row = 0; row < 3; ++row) {
          auto *dot = bke::node_find_node_by_name(b.tree,
                                                  root + "ColorToScene" + std::to_string(row));
          if (l.fromnode == final && STREQ(l.fromsock->identifier, "Vector") && dot &&
              STREQ(dot->idname, "ShaderNodeVectorMath") &&
              dot->custom1 == NODE_VECTOR_MATH_DOT_PRODUCT && l.tonode == dot &&
              l.tosock == dot->inputs.first && STREQ(l.tosock->identifier, "Vector") &&
              (l.flag & NODE_LINK_VALID) && !(l.flag & NODE_LINK_MUTED))
          {
            if (!outputs.insert(row).second)
              throw std::invalid_argument("Duplicate linear Color UDIM output");
            known = true;
          }
        }
        if (!known)
          throw std::invalid_argument("Linear Color UDIM graph has an unowned use");
      }
    if (outputs.size() != 3)
      throw std::invalid_argument("Linear Color UDIM decoder fanout changed");
  }
};
