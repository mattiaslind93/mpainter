// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit test actor, included in the existing private artist namespace.
// Requires the separately reviewed SurfaceGraphSnapshot/Observation overlay.
class SurfaceArtist final : public QObject {
  using Reference = pd149_surface_test::PrincipledReference;
  using GraphExpected = pd149_surface_test::SurfaceGraphExpected;
  QWidget *window_, *viewport_;
  QString output_, mesh_, package_;
  MaterialLayeringArtistHooks hooks_;
  QTimer timer_;
  QElapsedTimer clock_, phase_clock_;
  bool busy_ = false, closed_ = false, terminal_ = false;
  const bool noop_ = qEnvironmentVariableIsSet("BTS_PD149_SURFACE_NOOP_CONTROL");
  const bool cold_ = qEnvironmentVariableIntValue("BTS_PD149_SURFACE_COLD_CONTROL") == 1;
  bool discovering_ = cold_;
  QJsonArray discovery_rows_, cold_generations_;
  QJsonObject discovery_terminal_, cold_environment_;
  QString cold_projection_;
  int stage_ = 0, phase_ = 0, history_ = 0, invalidations_ = 0;
  bool imported_ = false;
  std::uint64_t ticket_ = 0, floor_ = 0, revision_ = 0;
  std::uint32_t uid_ = 0;
  QUuid document_, set_id_;
  QJsonObject stable_document_, color_, fixture_, mask_proof_;
  QString geometry_;
  GraphExpected expected_;
  SurfaceGraphSnapshot baseline_;
  QJsonArray rows_, frames_, phases_, roi_rows_;
  std::unique_ptr<Reference> reference_;
  std::function<void()> restore_view_;
  std::array<QImage, 4> images_;
  QByteArray roi_;
  std::array<int, 2> roi_counts_{};
  std::uint64_t previous_index_ = 0, last_index_ = 0;
  bool have_index_ = false, unfinished_ = false, progressed_ = false, restarted_ = false;
  template<class T> T &get(const char *name)
  {
    auto *p = window_->findChild<T *>(name);
    pd149_test::require(p, name);
    return *p;
  }
  const TextureSet &set() const
  {
    pd149_test::require(hooks_.document(), "Surface document missing");
    for (const auto &value : hooks_.document()->texture_sets())
      if (value.material_session_uid == hooks_.material())
        return value;
    throw std::runtime_error("Surface selected set missing");
  }
  Material &material() const
  {
    pd149_test::require(G_MAIN, "Surface Main missing");
    for (Material &value : G_MAIN->materials)
      if (value.id.session_uid == uid_)
        return value;
    throw std::runtime_error("Surface Material missing");
  }
  static QString digest(const QByteArray &bytes)
  {
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  }
  static QJsonObject node(const SurfaceGraphNode &v)
  {
    return {{"identifier", v.identifier},
            {"name", QString::fromStdString(v.name)},
            {"type", QString::fromStdString(v.type)},
            {"muted", v.muted}};
  }
  static QJsonObject socket(const SurfaceGraphSocket &v)
  {
    QJsonArray values, links;
    for (double x : v.value)
      values.append(x);
    for (const auto &l : v.incoming)
      links.append(QJsonObject{{"from", node(l.from)},
                               {"to", node(l.to)},
                               {"output", QString::fromStdString(l.output)},
                               {"input", QString::fromStdString(l.input)},
                               {"valid", l.valid},
                               {"muted", l.muted},
                               {"available", l.available}});
    return {{"identifier", QString::fromStdString(v.identifier)},
            {"type", v.type},
            {"components", v.components},
            {"value", values},
            {"available", v.available},
            {"incoming", links}};
  }
  static QJsonObject graph(const SurfaceGraphState &v)
  {
    QJsonArray route, inputs;
    for (const auto &s : v.route)
      route.append(socket(s));
    for (const auto &s : v.inputs)
      inputs.append(socket(s));
    return {{"valid", v.valid},
            {"output", node(v.output)},
            {"principled", node(v.principled)},
            {"route", route},
            {"inputs", inputs}};
  }
  static QJsonArray slots(const std::vector<SurfaceGraphSlot> &values)
  {
    QJsonArray rows;
    for (const auto &v : values)
      rows.append(QJsonObject{{"object_uid", qint64(v.object_uid)},
                              {"material_original_uid", qint64(v.material_original_uid)},
                              {"slot", v.slot},
                              {"faces", v.faces},
                              {"evaluated_object", v.evaluated_object},
                              {"evaluated_material", v.evaluated_material},
                              {"selected_binding", v.selected_binding}});
    return rows;
  }

  static QJsonObject observed(const SurfaceGraphSnapshot &g, const EeveeCompletionSnapshot &s)
  {
    auto row = pd149_eevee_observation::snapshot_json(s);
    row["surface_graph_status_enum"] = int(g.status);
    row["graph_host_draw"] = QString::number(g.host_draw);
    row["graph_target_generation"] = QString::number(g.target_generation);
    row["selected_material_uid"] = qint64(g.selected_material_uid);
    row["distinct_evaluated"] = g.distinct_evaluated;
    row["original_backlink_matches"] = g.original_backlink_matches;
    row["distinct_evaluated_tree"] = g.distinct_evaluated_tree;
    row["tree_original_backlink_matches"] = g.tree_original_backlink_matches;
    row["material_override"] = g.material_override;
    row["original_graph"] = graph(g.original);
    row["evaluated_graph"] = graph(g.evaluated);
    row["visible_slots"] = slots(g.visible_slots);
    return row;
  }
  QString projected_fixture() const
  {
    QJsonArray vertices;
    const auto scene = hooks_.host.scene_snapshot();
    for (const auto &triangle : scene.uv_triangles)
      if (scene.materials.at(triangle.material).session_id == uid_)
        for (const auto &position : triangle.positions) {
          const auto p = hooks_.scene_point({position[0], position[1], position[2]});
          pd149_test::require(std::isfinite(p.x()) && std::isfinite(p.y()),
                              "Cold fixture projection nonfinite");
          vertices.append(QJsonArray{p.x(), p.y()});
        }
    pd149_test::require(vertices.size() == 12,
                        "Cold fixture projection needs four actual triangles");
    return digest(QJsonDocument(vertices).toJson(QJsonDocument::Compact));
  }
  QString geometry() const
  {
    QJsonArray triangles;
    const auto scene = hooks_.host.scene_snapshot();
    for (const auto &t : scene.uv_triangles) {
      QJsonArray p, uv, n;
      for (const auto &v : t.positions)
        p.append(QJsonArray{v[0], v[1], v[2]});
      for (const auto &v : t.corners)
        uv.append(QJsonArray{v[0], v[1]});
      for (const auto &v : t.normals)
        n.append(QJsonArray{v[0], v[1], v[2]});
      triangles.append(QJsonObject{{"material", qint64(scene.materials.at(t.material).session_id)},
                                   {"triangle", QString::number(t.triangle_id)},
                                   {"positions", p},
                                   {"uv", uv},
                                   {"normals", n},
                                   {"has_normals", t.has_normals}});
    }
    return digest(QJsonDocument(triangles).toJson(QJsonDocument::Compact));
  }
  void write(bool passed = false, const QString &error = {})
  {
    QSaveFile file(output_ + "/pd149-surface.json");
    pd149_test::require(file.open(QIODevice::WriteOnly), "Surface proof open failed");
    const QJsonObject proof{
        {"profile",
         cold_ ? "uniform-eevee-surface-cold-viewport-v1" :
         noop_ ? "uniform-eevee-surface-noop-repeatability-v1" :
                 "uniform-eevee-surface-baseline-v1"},
        {"passed", passed},
        {"error", error},
        {"native_pid", qint64(QCoreApplication::applicationPid())},
        {"stage", stage_},
        {"phase", phase_},
        {"actual_product_import", imported_},
        {"import_one_history_commit", imported_},
        {"fixture", fixture_},
        {"masks", mask_proof_},
        {"observations", rows_},
        {"discovery_observations", discovery_rows_},
        {"discovery_terminal", discovery_terminal_},
        {"cold_viewport_generations", cold_generations_},
        {"cold_environment", cold_environment_},
        {"cold_projection_sha256", cold_projection_},
        {"phases", phases_},
        {"frames", frames_},
        {"roi_counts", QJsonArray{roi_counts_[0], roi_counts_[1]}},
        {"diagnostic_graph_invalidations", invalidations_},
        {"settings_written", false},
        {"sampling_reset_called", false},
        {"timer_redraw_requests", 0},
        {"historical_BA_negatives_unchanged", true},
        {"history_extension_tested", false},
        {"scope",
         cold_ ?
             "Symmetric fresh-view-history reference control only; not live surface parity or a production fix" :
         noop_ ?
             "No-op original-graph repeatability and duplicate FBO readback control only; no surface-parity claim" :
             "Same-backend uniform canonical Principled baseline only; literal RGB/Metal/Rough/AO, neutral Normal/Bump/geometry AO; strict RGB8 interior equality; no Cycles/nonlinear/maps/latency claim"}};
    pd149_test::require(file.write(QJsonDocument(proof).toJson()) > 0 && file.commit(),
                        "Surface proof commit failed");
  }
  void unchanged() const
  {
    pd149_test::require(hooks_.document() && hooks_.document()->id() == document_ &&
                            set().id == set_id_ && hooks_.material() == uid_ &&
                            hooks_.document()->edit_state() == stable_document_ &&
                            hooks_.state()["undo_count"].toInt() == history_ &&
                            geometry() == geometry_ && hooks_.host.color_state() == color_,
                        "Surface comparison changed document/history/geometry/UV/color settings");
    if (cold_)
      pd149_test::require(projected_fixture() == cold_projection_ &&
                              hooks_.host.environment_state() == cold_environment_,
                          "Cold comparison changed actual projection/environment");
  }
  void invalidate()
  {
    ++invalidations_;
    viewport_->update();
  }  // ONE per owned RAII mutation/restoration; not a completion proof.
  void arm(int phase)
  {
    phase_ = phase;
    terminal_ = false;
    floor_ = hooks_.host.eevee_completion_snapshot().host_draw;
    previous_index_ = hooks_.host.eevee_completion_snapshot().sample_index;
    have_index_ = unfinished_ = progressed_ = restarted_ = false;
    phase_clock_.restart();
  }
  void recreate_cold_viewport()
  {
    pd149_test::require(cold_, "Cold viewport control not active");
    const auto previous = expected_.target_generation;
    const auto generation = hooks_.host.diagnostic_recreate_scene_viewport();
    pd149_test::require(generation > previous, "Cold viewport generation did not advance");
    expected_.target_generation = generation;
    cold_generations_.append(QJsonObject{{"phase", phase_},
                                         {"generation", QString::number(generation)},
                                         {"draw_floor", QString::number(floor_)}});
  }
  void constants_expected(const pd149_surface_test::UniformReference &v)
  {
    expected_.original = baseline_.original;
    for (int i = 0; i < 3; ++i) {
      auto &s = expected_.original.inputs[i];
      s.incoming.clear();
      if (i == 0) {
        for (int c = 0; c < 3; ++c)
          s.value[c] = float(v.color[c]);
      }
      else
        s.value[0] = float(i == 1 ? v.metallic : v.roughness);
    }
    expected_.evaluated = expected_.original;
  }
  void check_inputs_and_masks()
  {
    using pd149_test::require;
    const auto &s = set();
    require(s.tiles == std::set<int>({1001, 1002}) && s.width(1001) == 128 &&
                s.rows(1001) == 128 && s.width(1002) == 128 && s.rows(1002) == 128,
            "Surface needs two actual128 destination tiles");
    require_material_parameter_main(s);
    require(!s.mesh_maps[0] && !s.mesh_maps[3] && !s.ao_channel && !s.displacement.channel,
            "Surface fixture geometry Normal/AO/displacement must be neutral");
    require(s.shader_parameters.normal_intensity == 1 &&
                s.shader_parameters.ao_intensity ==
                    float(pd149_surface_test::uniform_ao_intensity),
            "Surface shader parameters differ");
    for (int i = 0; i < 4; ++i) {
      const auto &input = s.material_layering.materials[i];
      const auto &literal = pd149_surface_test::uniform_inputs[i];
      require(input.values.normal == std::array<float, 3>({0, 0, 1}) && input.values.bump == .5f,
              "Surface input Normal/Bump not neutral");
      for (const auto &t : input.textures)
        require(!t, "Surface uniform baseline cannot contain source textures");
      for (int c = 0; c < 3; ++c)
        require(input.values.base_color[c] == float(literal.color[c]),
                "Surface input RGB differs from literal");
      require(input.values.base_color[3] == 1 &&
                  input.values.metallic == float(literal.metallic) &&
                  input.values.roughness == float(literal.roughness) &&
                  input.values.ambient_occlusion == float(literal.ao),
              "Surface input scalar differs from literal");
    }
    std::vector<std::array<int, 2>> xy;
    xy.reserve(128 * 128);
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x)
        xy.push_back({x, y});
    QFile raw(output_ + "/actual-mask-R.f32");
    require(raw.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Surface mask raw open failed");
    QJsonArray formats;
    double maximum = 0;
    int components = 0;
    for (int i = 0; i < 3; ++i) {
      const int slot = s.material_layering.mask_slots[i];
      require(s.users[slot].precision == ChannelPrecision::L32F,
              "Surface mask document precision is not L32F");
      int children = 0;
      for (const auto &layer : s.layers)
        if (layer.parent == s.material_layering.mask_roots[i]) {
          ++children;
          require(layer.kind == LayerKind::Fill && layer.fill.mode == 6 &&
                      layer.fill.filtering == 2 && !layer.fill.alpha_mask[6 + slot],
                  "Surface imported mask sampling/alpha policy differs");
        }
      require(children == 1, "Surface mask must have exactly its imported Fill child");
      hooks_.host.with_gpu([&] {
        auto *n = blender::bke::node_find_node_by_name(*material().nodetree,
                                                       "__BTS_UserPixels" + std::to_string(slot));
        require(n && n->id && GS(n->id->name) == ID_IM, "Surface actual mask Image missing");
        auto *image = reinterpret_cast<Image *>(n->id);
        const auto cached = BKE_image_gpu_override_texture(image);
        auto *texture = cached.texture ? *cached.texture : nullptr;
        require(texture && GPU_texture_format(texture) == gpu::TextureFormat::SFLOAT_32_32_32_32 &&
                    image->alpha_mode == IMA_ALPHA_CHANNEL_PACKED,
                "Surface actual mask GPU precision/packed alpha differs");
        for (int tile : {1001, 1002}) {
          ImageGpuTileRegion region;
          require(BKE_image_gpu_override_tile_region(image, tile, region) && region.width == 128 &&
                      region.height == 128,
                  "Surface actual mask tile region missing");
        }
        formats.append(
            QJsonObject{{"slot", slot},
                        {"format", GPU_texture_format_name(GPU_texture_format(texture))},
                        {"alpha_channel_packed", true},
                        {"L32F", true},
                        {"nearest", true}});
      });
      for (int tile : {1001, 1002}) {
        const auto values = hooks_.host.diagnostic_gpu_pixels(uid_, 7 + slot, xy, tile);
        require(values.size() == xy.size(), "Surface mask plane shape missing");
        for (const auto &p : values) {
          const float value = p[0];
          require(raw.write(reinterpret_cast<const char *>(&value), sizeof(value)) ==
                      sizeof(value),
                  "Surface mask raw write failed");
          require(std::isfinite(value), "Surface actual mask is nonfinite");
          maximum = std::max(maximum,
                             std::abs(double(value) - pd149_surface_test::uniform_masks[i]));
          ++components;
        }
      }
    }
    raw.close();
    mask_proof_ = {{"components", components},
                   {"maximum_error", maximum},
                   {"required_exact", true},
                   {"formats", formats}};
    write();
    require(components == 98304 && maximum == 0,
            "Surface actual full-float mask differs from literal");
  }
  void make_roi(const QImage &frame)
  {
    using pd149_test::require;
    roi_ = QByteArray(frame.width() * frame.height(), char(0));
    const auto scene = hooks_.host.scene_snapshot();
    const double sx = double(frame.width()) / viewport_->width(),
                 sy = double(frame.height()) / viewport_->height();
    for (const auto &t : scene.uv_triangles) {
      if (scene.materials.at(t.material).session_id != uid_)
        continue;
      require(t.has_normals, "Surface fixture needs actual geometry normals");
      std::array<QPointF, 3> p;
      for (int i = 0; i < 3; ++i) {
        const auto q = hooks_.scene_point(
            {t.positions[i][0], t.positions[i][1], t.positions[i][2]});
        p[i] = {q.x() * sx, q.y() * sy};
        require(std::isfinite(p[i].x()) && std::isfinite(p[i].y()),
                "Surface projection nonfinite");
      }
      const auto a = p[1] - p[0], b = p[2] - p[0];
      const double determinant = a.x() * b.y() - b.x() * a.y();
      require(std::abs(determinant) > 1e-6, "Surface projected triangle degenerate");
      for (int y = 0; y < frame.height(); ++y)
        for (int x = 0; x < frame.width(); ++x) {
          if (x <= 52 * sx || y <= 42 * sy || x >= frame.width() - 100 * sx ||
              y >= frame.height() - 90 * sy)
            continue;
          const QPointF d(x + .5 - p[0].x(), y + .5 - p[0].y());
          const double u = (d.x() * b.y() - b.x() * d.y()) / determinant,
                       v = (a.x() * d.y() - d.x() * a.y()) / determinant;
          if (u <= .15 || v <= .15 || u + v >= .85)
            continue;
          const double tu = t.corners[0][0] + u * (t.corners[1][0] - t.corners[0][0]) +
                            v * (t.corners[2][0] - t.corners[0][0]),
                       tv = t.corners[0][1] + u * (t.corners[1][1] - t.corners[0][1]) +
                            v * (t.corners[2][1] - t.corners[0][1]);
          const int tile = 1001 + int(std::floor(tu)) + 10 * int(std::floor(tv));
          require(tile == 1001 || tile == 1002, "Surface ROI outside target tiles");
          const int at = y * frame.width() + x;
          require(roi_[at] == 0, "Surface fixture ROI overlaps another selected triangle");
          roi_[at] = char(tile - 1000);
          ++roi_counts_[tile - 1001];
        }
    }
    QFile file(output_ + "/interior-roi.u8");
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                file.write(roi_) == roi_.size(),
            "Surface ROI write failed");
    require(roi_counts_[0] > 0 && roi_counts_[1] > 0,
            "Surface interior ROI missed one actual tile");
  }
  void capture(const QJsonObject &row)
  {
    using pd149_test::require;
    unchanged();
    const auto before = hooks_.host.eevee_completion_snapshot();
    const int frames_before = hooks_.state()["render_frames"].toInt();
    const auto frame = capture_viewport_framebuffer(viewport_).convertToFormat(
        QImage::Format_RGBA8888);
    QImage duplicate;
    if (noop_ || cold_)
      duplicate = capture_viewport_framebuffer(viewport_).convertToFormat(QImage::Format_RGBA8888);
    require(!frame.isNull(), "Surface framebuffer capture empty");
    const QString stem = QString("/phase-%1").arg(phase_);
    QFile raw(output_ + stem + ".rgba8");
    require(raw.open(QIODevice::WriteOnly | QIODevice::NewOnly),
            "Surface raw capture open failed");
    for (int y = 0; y < frame.height(); ++y)
      require(raw.write(reinterpret_cast<const char *>(frame.constScanLine(y)),
                        frame.width() * 4) == frame.width() * 4,
              "Surface raw capture write failed");
    raw.close();
    require(frame.save(output_ + stem + ".png"), "Surface PNG save failed");
    images_[phase_ - 1] = frame;
    if (phase_ == 1)
      make_roi(frame);
    QJsonObject capture{{"phase", phase_},
                        {"width", frame.width()},
                        {"height", frame.height()},
                        {"raw", stem.mid(1) + ".rgba8"},
                        {"terminal", row},
                        {"raw_saved_before_pixel_assertions", true}};
    QByteArray first_bytes, duplicate_bytes;
    if (noop_ || cold_) {
      require(!duplicate.isNull() && duplicate.size() == frame.size(),
              "No-op duplicate framebuffer shape differs");
      for (int y = 0; y < frame.height(); ++y) {
        first_bytes.append(reinterpret_cast<const char *>(frame.constScanLine(y)),
                           frame.width() * 4);
        duplicate_bytes.append(reinterpret_cast<const char *>(duplicate.constScanLine(y)),
                               duplicate.width() * 4);
      }
      QFile second(output_ + stem + ".duplicate.rgba8");
      require(second.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
                  second.write(duplicate_bytes) == duplicate_bytes.size(),
              "No-op duplicate raw write failed");
      second.close();
      capture["duplicate_raw"] = stem.mid(1) + ".duplicate.rgba8";
      capture["first_sha256"] = digest(first_bytes);
      capture["duplicate_sha256"] = digest(duplicate_bytes);
      capture["duplicate_readback_no_pump"] = true;
      capture["readback_draw_before"] = QString::number(before.host_draw);
      capture["readback_draw_after"] = QString::number(
          hooks_.host.eevee_completion_snapshot().host_draw);
      capture["readback_frames_before"] = frames_before;
      capture["readback_frames_after"] = hooks_.state()["render_frames"];
    }
    if (cold_) {
      capture["projected_fixture_sha256"] = projected_fixture();
      capture["environment"] = hooks_.host.environment_state();
    }
    frames_.append(capture);
    write();
    require(before.host_draw == hooks_.host.eevee_completion_snapshot().host_draw &&
                frames_before == hooks_.state()["render_frames"].toInt(),
            "Surface readback advanced Scene/Paint identity");
    if (noop_ || cold_)
      require(first_bytes == duplicate_bytes, "No-op consecutive FBO readbacks differ");
    terminal_ = true;
  }
  void post_paint(
      std::uint64_t ticket, int phase, int frames, std::uint64_t revision, std::uint64_t draw)
  {
    using namespace pd149_surface_test;
    using pd149_test::require;
    if (closed_ || busy_ || terminal_ || phase != phase_ || ticket != ticket_)
      return;
    const QScopedValueRollback<bool> guard(busy_, true);
    const auto state = hooks_.state();
    if (!hooks_.document() || state["parameter_import_pending"].toBool() ||
        state["composite_pending"].toBool() || state["property_pending"].toBool() ||
        state["texture_loading"].toBool() || !set().material_layering.allocated())
      return;
    const auto s = hooks_.host.eevee_completion_snapshot();
    const auto g = hooks_.host.surface_graph_snapshot();
    const auto route = hooks_.host.render_state();
    const bool eevee = !route["active"].toBool() && route["device"] == "EEVEE" &&
                       route["status"] == "EEVEE";
    const auto current_revision = state["document_revision"].toVariant().toULongLong();
    if (phase_ == 1 && expected_.material_uid == 0) {
      auto availability = observed(g, s);
      availability["phase"] = phase_;
      availability["host_backend_route"] = route;
      availability["graph_available"] = false;
      (discovering_ ? discovery_rows_ : rows_).append(availability);
      write();
      if (g.status != SurfaceGraphStatus::Captured || g.host_draw != s.host_draw ||
          g.host_draw <= floor_ || g.host_draw <= draw || state["render_frames"].toInt() <= frames)
        return;
      require(g.selected_material_uid == uid_ && valid_surface_graph(g.original),
              "Surface baseline canonical graph observation invalid");
      for (int i = 0; i < 4; ++i) {
        const char *names[]{"__BTS_LayeredColor",
                            "__BTS_LayeredChannel1",
                            "__BTS_LayeredChannel2",
                            "__BTS_LayeredBump"};
        require(g.original.inputs[i].incoming.size() == 1 &&
                    g.original.inputs[i].incoming[0].from.name == names[i],
                "Surface actual canonical input endpoint differs");
      }
      std::set<std::uint32_t> selected_objects;
      int selected_faces = 0;
      for (const auto &slot : g.visible_slots)
        if (slot.selected_binding) {
          selected_objects.insert(slot.object_uid);
          selected_faces += slot.faces;
        }
      require(selected_objects.size() == 2 && selected_faces == 4,
              "Surface graph omitted one of the two real selected owners");
      require(state["undo_count"].toInt() == history_ + 1,
              "Surface Import must publish one full-document Undo");
      imported_ = true;
      baseline_ = g;
      expected_ = {uid_, g.target_generation, g.original, g.original, g.visible_slots};
      revision_ = current_revision;
      check_inputs_and_masks();
      document_ = hooks_.document()->id();
      set_id_ = set().id;
      stable_document_ = hooks_.document()->edit_state();
      geometry_ = geometry();
      color_ = hooks_.host.color_state();
      history_ = state["undo_count"].toInt();
      if (cold_) {
        cold_projection_ = projected_fixture();
        cold_environment_ = hooks_.host.environment_state();
      }
    }
    SurfaceGraphPaint paint{phase_,
                            phase,
                            phase_,
                            revision_,
                            revision,
                            current_revision,
                            floor_,
                            draw,
                            std::uint64_t(frames),
                            std::uint64_t(state["render_frames"].toInt()),
                            eevee};
    const bool graph_ok = surface_graph_matches(g, expected_, paint) &&
                          g.host_draw == s.host_draw && g.target_generation == s.target_generation;
    if (graph_ok) {
      unfinished_ |= !s.viewport_sampling_finished;
      restarted_ |= s.reset_pending || s.sample_index < previous_index_;
      if (have_index_ && s.sample_index > last_index_)
        progressed_ = true;
      last_index_ = s.sample_index;
      have_index_ = true;
    }
    const bool terminal = graph_ok && unfinished_ && progressed_ && (phase_ == 1 || restarted_) &&
                          pd149_eevee_observation::terminal_sampling_state(s);
    auto row = observed(g, s);
    row["host_backend_route"] = route;
    row["phase"] = phase_;
    row["paint_sequence"] = QString::number(ticket);
    row["frames_before"] = frames;
    row["frames_after"] = state["render_frames"];
    row["draw_before"] = QString::number(draw);
    row["phase_draw_floor"] = QString::number(floor_);
    row["revision_before"] = QString::number(revision);
    row["revision_after"] = QString::number(current_revision);
    row["graph_matches"] = graph_ok;
    row["graph_host_draw"] = QString::number(g.host_draw);
    row["graph_target_generation"] = QString::number(g.target_generation);
    row["selected_material_uid"] = qint64(g.selected_material_uid);
    row["distinct_evaluated"] = g.distinct_evaluated;
    row["original_backlink_matches"] = g.original_backlink_matches;
    row["distinct_evaluated_tree"] = g.distinct_evaluated_tree;
    row["tree_original_backlink_matches"] = g.tree_original_backlink_matches;
    row["material_override"] = g.material_override;
    row["original_graph"] = graph(g.original);
    row["evaluated_graph"] = graph(g.evaluated);
    row["expected_graph"] = graph(expected_.original);
    row["visible_slots"] = slots(g.visible_slots);
    row["expected_visible_slots"] = slots(expected_.visible_slots);
    row["unfinished_observed"] = unfinished_;
    row["progression_observed"] = progressed_;
    row["restart_observed"] = restarted_;
    row["terminal"] = terminal;
    (discovering_ ? discovery_rows_ : rows_).append(row);
    write();
    if (terminal) {
      if (discovering_) {
        discovery_terminal_ = row;
        terminal_ = true;
        write();
        return;
      }
      capture(row);
      phases_.append(QJsonObject{
          {"phase", phase_}, {"terminal", row}, {"cumulative_invalidations", invalidations_}});
      write();
    }
  }
  bool eventFilter(QObject *watched, QEvent *event) override
  {
    if (!closed_ && phase_ && watched == viewport_ && event->type() == QEvent::Paint) {
      const auto ticket = ++ticket_;
      const auto state = hooks_.state();
      const int phase = phase_, frames = state["render_frames"].toInt();
      const auto revision = state["document_revision"].toVariant().toULongLong();
      const auto draw = hooks_.host.eevee_completion_snapshot().host_draw;
      QTimer::singleShot(0, this, [this, ticket, phase, frames, revision, draw] {
        try {
          post_paint(ticket, phase, frames, revision, draw);
        }
        catch (const std::exception &e) {
          fail(e.what());
        }
      });
    }
    return false;
  }
  void choose_import()
  {
    auto &button = get<QAbstractButton>("importMaterialLayerParameters");
    pd149_test::require(button.isVisible() && button.isEnabled(),
                        "Surface actual Import button unavailable");
    QTimer::singleShot(0, this, [this] {
      try {
        auto *dialog = window_->findChild<QFileDialog *>("materialParameterImportDialog");
        pd149_test::require(dialog && dialog->fileMode() == QFileDialog::Directory,
                            "Surface actual Import directory chooser missing");
        dialog->setDirectory(package_);
        dialog->selectFile(package_);
        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
      }
      catch (const std::exception &e) {
        fail(e.what());
      }
    });
    button.click();
  }
  void compare()
  {
    using pd149_test::require;
    QJsonArray comparisons;
    bool equality = true;
    std::array<bool, 2> signal{};
    for (int phase : {2, 3, 4}) {
      const auto &a = images_[0], &b = images_[phase - 1];
      require(a.size() == b.size() && roi_.size() == a.width() * a.height(),
              "Surface capture/ROI shape differs");
      int maximum = 0, components = 0;
      QJsonObject worst;
      for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
          const int tile = int(static_cast<unsigned char>(roi_[y * a.width() + x]));
          if (!tile)
            continue;
          const auto *p = a.constScanLine(y) + x * 4, *q = b.constScanLine(y) + x * 4;
          require(p[3] == 255 && q[3] == 255, "Surface ROI alpha must be opaque");
          for (int c = 0; c < 3; ++c) {
            const int delta = std::abs(int(p[c]) - int(q[c]));
            ++components;
            if (delta > maximum) {
              maximum = delta;
              worst = {{"xy", QJsonArray{x, y}},
                       {"tile", 1000 + tile},
                       {"component", c},
                       {"actual", p[c]},
                       {"reference", q[c]}};
            }
            if (phase == 2 && delta)
              signal[tile - 1] = true;
          }
        }
      comparisons.append(QJsonObject{{"phase", phase},
                                     {"components", components},
                                     {"maximum_rgb8_delta", maximum},
                                     {"worst", worst},
                                     {"strict_equality_required", phase != 2}});
      if (phase != 2)
        equality &= maximum == 0;
    }
    QSaveFile file(output_ + "/comparison.json");
    require(file.open(QIODevice::WriteOnly) &&
                file.write(QJsonDocument(QJsonObject{{"comparisons", comparisons},
                                                     {"different_color_signal",
                                                      QJsonArray{signal[0], signal[1]}},
                                                     {"strict_equality", equality}})
                               .toJson()) > 0 &&
                file.commit(),
            "Surface comparison evidence write failed");
    write();
    require(signal[0] && signal[1],
            "Surface deliberately different Color produced no signal on both tiles");
    require(equality, "Surface same-backend strict interior ROI differs");
    unchanged();
  }
  void compare_noop()
  {
    using pd149_test::require;
    unchanged();
    const auto &a = images_[0], &b = images_[1];
    require(a.size() == b.size() && roi_.size() == a.width() * a.height(),
            "No-op capture/ROI shape differs");
    int maximum = 0, components = 0;
    QJsonObject worst;
    for (int y = 0; y < a.height(); ++y)
      for (int x = 0; x < a.width(); ++x) {
        const int tile = int(static_cast<unsigned char>(roi_[y * a.width() + x]));
        if (!tile)
          continue;
        const auto *p = a.constScanLine(y) + x * 4, *q = b.constScanLine(y) + x * 4;
        require(p[3] == 255 && q[3] == 255, "No-op ROI alpha must be opaque");
        for (int c = 0; c < 3; ++c) {
          const int delta = std::abs(int(p[c]) - int(q[c]));
          ++components;
          if (delta > maximum) {
            maximum = delta;
            worst = {{"xy", QJsonArray{x, y}},
                     {"tile", 1000 + tile},
                     {"component", c},
                     {"first", p[c]},
                     {"second", q[c]}};
          }
        }
      }
    QSaveFile file(output_ + "/comparison.json");
    require(file.open(QIODevice::WriteOnly) &&
                file.write(QJsonDocument(QJsonObject{{"control", "original-graph-noop"},
                                                     {"components", components},
                                                     {"maximum_rgb8_delta", maximum},
                                                     {"worst", worst},
                                                     {"strict_equality", maximum == 0}})
                               .toJson()) > 0 &&
                file.commit(),
            "No-op comparison evidence write failed");
    write();
    require(maximum == 0, "No-op original-graph strict interior ROI differs");
  }
  void advance()
  {
    using pd149_test::require;
    if (closed_ || busy_)
      return;
    const QScopedValueRollback<bool> guard(busy_, true);
    require(clock_.elapsed() < 300000 && (!phase_ || terminal_ || phase_clock_.elapsed() < 90000),
            "Surface readiness deadline; no completion inferred");
    if (stage_ == 0) {
      stage_ = 1;
      get<QAction>("newProject").trigger();
      return;
    }
    if (stage_ == 1) {
      auto &dialog = get<QDialog>("newProjectDialog");
      require(dialog.isVisible(), "Surface real New dialog missing");
      get<QComboBox>("projectTemplate").setCurrentIndex(0);
      get<QLineEdit>("meshPath").setText(mesh_);
      get<QCheckBox>("preserveUvTiles").setChecked(true);
      auto &r = get<QComboBox>("documentResolution");
      r.setCurrentIndex(r.findData(128));
      stage_ = 2;
      get<QAbstractButton>("createProject").click();
      return;
    }
    if (stage_ == 2) {
      const auto state = hooks_.state();
      if (!hooks_.document() || !hooks_.material() || state["composite_pending"].toBool())
        return;
      uid_ = hooks_.material();
      require(!set().material_layering.allocated() && hooks_.begin_fragment_view &&
                  hooks_.scene_point,
              "Surface fresh canonical target/adapters missing");
      const auto color = hooks_.host.color_state();
      std::array<double, 9> matrix{};
      require(G_MAIN, "Surface fixture working context absent");
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
          matrix[r * 3 + c] = G_MAIN->colorspace.scene_linear_to_xyz[c][r];
      std::atomic_bool cancel = false;
      fixture_ =
          pd149_surface_test::write_surface_fixture(
              package_, QString::fromUtf8(G_MAIN->colorspace.scene_linear_name), matrix, cancel)
              .evidence;
      restore_view_ = hooks_.begin_fragment_view();
      history_ = state["undo_count"].toInt();
      arm(1);
      stage_ = 3;
      choose_import();
      return;
    }
    if (stage_ == 3) {
      require(hooks_.state()["parameter_import_error"].toString().isEmpty(),
              "Surface actual Import failed");
      if (!terminal_)
        return;
      unchanged();
      if (discovering_) {
        discovering_ = false;
        arm(1);
        recreate_cold_viewport();
        invalidate();
        return;
      }
      if (noop_) {
        arm(2);
        expected_.original = baseline_.original;
        expected_.evaluated = baseline_.original;
        hooks_.host.with_gpu([&] {
          auto &m = material();
          BKE_ntree_update_after_single_tree_change(*G_MAIN, *m.nodetree);
          DEG_id_tag_update_ex(G_MAIN, &m.id, blender::ID_RECALC_SHADING);
          DEG_id_tag_update_ex(G_MAIN, &m.nodetree->id, blender::ID_RECALC_NTREE_OUTPUT);
        });
        invalidate();
        stage_ = 60;
        return;
      }
      auto wrong = pd149_surface_test::reference();
      wrong.color = {1, 0, 1};
      arm(2);
      constants_expected(wrong);
      hooks_.host.with_gpu(
          [&] { reference_ = std::make_unique<Reference>(*G_MAIN, material(), wrong); });
      if (cold_)
        recreate_cold_viewport();
      invalidate();
      stage_ = 4;
      return;
    }
    if (stage_ == 4) {
      if (!terminal_)
        return;
      unchanged();
      const auto intended = pd149_surface_test::reference();
      arm(3);
      constants_expected(intended);
      hooks_.host.with_gpu([&] {
        reference_->restore();
        reference_.reset();
        invalidate();
        reference_ = std::make_unique<Reference>(*G_MAIN, material(), intended);
      });
      if (cold_)
        recreate_cold_viewport();
      invalidate();
      stage_ = 5;
      return;
    }
    if (stage_ == 5) {
      if (!terminal_)
        return;
      unchanged();
      arm(4);
      expected_.original = baseline_.original;
      expected_.evaluated = baseline_.original;
      hooks_.host.with_gpu([&] {
        reference_->restore();
        reference_.reset();
      });
      if (cold_)
        recreate_cold_viewport();
      invalidate();
      stage_ = 6;
      return;
    }
    if (stage_ == 60 && terminal_) {
      compare_noop();
      write(true);
      retire();
      std::puts("PD149_SURFACE_NOOP PASS");
      QCoreApplication::exit(0);
      return;
    }
    if (stage_ == 6 && terminal_) {
      compare();
      write(true);
      retire();
      std::puts(cold_ ? "PD149_SURFACE_COLD PASS" : "PD149_SURFACE PASS");
      QCoreApplication::exit(0);
    }
  }
  void fail(const char *error) noexcept
  {
    std::fprintf(stderr,
                 "%s FAILED stage=%d phase=%d error=%s\n",
                 cold_ ? "PD149_SURFACE_COLD" :
                 noop_ ? "PD149_SURFACE_NOOP" :
                         "PD149_SURFACE",
                 stage_,
                 phase_,
                 error);
    try {
      write(false, QString::fromUtf8(error));
    }
    catch (...) {
      std::fputs("PD149_SURFACE_EVIDENCE_FAILED\n", stderr);
    }
    retire();
    QCoreApplication::exit(1);
  }

 public:
  SurfaceArtist(QWidget *window,
                QWidget *viewport,
                QString output,
                QString mesh,
                MaterialLayeringArtistHooks hooks)
      : QObject(window),
        window_(window),
        viewport_(viewport),
        output_(std::move(output)),
        mesh_(std::move(mesh)),
        package_(output_ + "/surface.btsparams"),
        hooks_(std::move(hooks))
  {
    pd149_test::require(!(cold_ && noop_), "Surface controls must be isolated");
    pd149_test::require(dynamic_cast<ViewportCapture *>(viewport_),
                        "Surface requires existing private framebuffer capture");
    viewport_->installEventFilter(this);
    connect(&timer_, &QTimer::timeout, this, [this] {
      try {
        advance();
      }
      catch (const std::exception &e) {
        fail(e.what());
      }
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { retire(); });
    clock_.start();
    timer_.start(33);
  }
  void retire() noexcept
  {
    if (closed_)
      return;
    closed_ = true;
    timer_.stop();
    viewport_->removeEventFilter(this);
    try {
      if (reference_)
        hooks_.host.with_gpu([&] {
          reference_->restore();
          reference_.reset();
        });
      if (restore_view_) {
        auto restore = std::move(restore_view_);
        restore_view_ = {};
        restore();
      }
    }
    catch (...) {
      qFatal("Surface reference/view did not retire before Host shutdown");
    }
  }
  ~SurfaceArtist() override
  {
    retire();
  }
};
