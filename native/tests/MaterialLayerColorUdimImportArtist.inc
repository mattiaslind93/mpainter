// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit Color-v2 product Import mode; all file/paint/reference work stays
// in the original Color actor. No test work on its default diagnostic route.
bool product_import_ = qEnvironmentVariableIsSet("BTS_PD149_COLOR_PACKAGE_IMPORT");
void choose_product_import(bool cancel = false)
{
  auto &button = get<QAbstractButton>("importMaterialLayerParameters");
  visible(button);
  QTimer::singleShot(0, window_, [this, cancel] {
    auto *dialog = window_->findChild<QFileDialog *>("materialParameterImportDialog");
    if (!dialog || dialog->fileMode() != QFileDialog::Directory) {
      fail("Color Import requires actual folder chooser");
      return;
    }
    if (cancel) {
      dialog->reject();
      return;
    }
    dialog->setDirectory(package_);
    dialog->selectFile(package_);
    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
  });
  button.click();
}
void import_unchanged(bool check_resources = true)
{
  require(metadata() == reconstruction_before_ &&
              hooks_.document()->shader_instances().to_json() == old_library_ &&
              catalog() == catalog_before_ &&
              (!check_resources || resources() == resources_before_) &&
              hooks_.state()["undo_count"].toInt() == history_,
          "Color Import Cancel/stale changed document/library/catalog/history/native resources");
}
void begin_product_import()
{
  require(!set().material_layering.allocated() && !QFileInfo(source_).exists(),
          "Color Import needs fresh New and hidden originals");
  reconstruction_before_ = metadata();
  old_library_ = hooks_.document()->shader_instances().to_json();
  catalog_before_ = catalog();
  resources_before_ = resources();
  history_ = hooks_.state()["undo_count"].toInt();
  hooks_.host.with_gpu([&] {
    GpuTap tap(material(), Output::Color, 128, 128);
    for (int tile : {1001, 1002})
      ordinary_colors_[tile - 1001] = tap.sample(tile);
  });
  QFile manifest(package_ + "/parameters.json");
  require(manifest.open(QIODevice::ReadOnly), "Color Import manifest metadata missing");
  rebuilt_.parameters = read_material_layer_parameters_json(
      QJsonDocument::fromJson(manifest.readAll()).object());
  choose_product_import(true);
  import_unchanged();
  require(!hooks_.state()["parameter_import_pending"].toBool(),
          "Color Import folder Cancel started worker");
  proof_["product_import"] = QJsonObject{{"folder_cancel_unchanged", true}};
  stage_ = 400;
}
void advance_product_import()
{
  const auto state = hooks_.state();
  if (!state["parameter_import_pending"].toBool() &&
      window_->findChild<QProgressDialog *>("materialParameterImportProgress"))
    return;
  auto evidence = proof_["product_import"].toObject();
  switch (stage_) {
    case 400: {
      auto *surface = native_principled_surface(material());
      require(surface, "Color Import canonical Principled missing");
      auto &alpha = *static_cast<bNodeSocketValueFloat *>(
          pd149_test::input(*surface, "Alpha").default_value);
      const float saved = alpha.value;
      const auto restore = qScopeGuard([&] { alpha.value = saved; });
      alpha.value = .25f;
      choose_product_import();
      require(!hooks_.state()["parameter_import_pending"].toBool() &&
                  get<QLabel>("materialLayeringScope").text().contains("Alpha"),
              "Color Import native gate failed before worker");
      import_unchanged();
      evidence["native_surface_before_worker"] = true;
      stage_ = 401;
      break;
    }
    case 401:
      choose_product_import();
      require(hooks_.state()["parameter_import_pending"].toBool(),
              "Color Import did not start actual worker");
      QMetaObject::invokeMethod(&get<QProgressDialog>("materialParameterImportProgress"),
                                "canceled",
                                Qt::DirectConnection);
      stage_ = 402;
      break;
    case 402:
      if (state["parameter_import_pending"].toBool())
        return;
      require(state["parameter_import_error"].toString().isEmpty(),
              "Color Import worker Cancel delivered error");
      import_unchanged();
      evidence["worker_cancel_retired_unchanged"] = true;
      {
        QPointer<QAbstractButton> edit = &get<QAbstractButton>("addPaintLayer");
        visible(*edit);
        choose_product_import();
        require(bool(edit), "Color stale Import edit button was retired");
        edit->click();
      }
      stage_ = 403;
      break;
    case 403:
      if (state["parameter_import_pending"].toBool())
        return;
      require(!set().material_layering.allocated() && catalog() == catalog_before_ &&
                  state["undo_count"].toInt() == history_ + 1,
              "Stale Color Import published after ordinary edit");
      action("undoDocument");
      stage_ = 404;
      break;
    // The deliberate ordinary layer edit may retire GPU caches; its Undo
    // still restores document/library/catalog/history exactly.
    case 404:
      import_unchanged(false);
      evidence["stale_result_rejected"] = true;
      choose_product_import();
      stage_ = 405;
      break;
    case 405:
      if (state["parameter_import_pending"].toBool())
        return;
      require(state["parameter_import_error"].toString().isEmpty() &&
                  set().material_layering.allocated() &&
                  state["undo_count"].toInt() == history_ + 1,
              "Color product Import did not publish one full-document Undo");
      for (int input = 0; input < 4; ++input)
        require(layer_material_json(set().material_layering.materials[input]) ==
                    layer_material_json(rebuilt_.parameters.materials[input]),
                "Color Import changed portable input/source/SRT");
      for (const auto &original : rebuilt_.parameters.originals) {
        int count = 0;
        for (const auto &row : state["project_resources"].toArray()) {
          const auto entry = row.toObject();
          if (QUuid(entry["id"].toString()) == original.asset)
            ++count;
        }
        require(count == 1, "Color Import original catalog row not published once");
      }
      reconstruction_after_ = metadata();
      new_library_ = hooks_.document()->shader_instances().to_json();
      require(new_library_ != old_library_, "Color Import omitted fresh shader assignment");
      tags(*set().material_layering.materials[0].textures[0]);
      evidence["actual_import_button_and_directory"] = true;
      evidence["one_history_commit"] = true;
      evidence["catalog_originals_once"] = true;
      proof_["reconstruction"] = QJsonObject{{"native_target_before_decode", true},
                                             {"stale_publication_rejected", true}};
      stage_ = 280;
      break;
    default:
      throw std::logic_error("Unknown Color product Import stage");
  }
  proof_["product_import"] = evidence;
  frame_ = hooks_.state()["render_frames"].toInt() + 2;
  viewport_->update();
}
