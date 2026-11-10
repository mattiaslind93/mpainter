// SPDX-License-Identifier: GPL-3.0-or-later
const auto generator_consumer = [&] {
  select_content_anchor_demo(
      anchor_demo_consumer,
      anchor_demo_fill,
      document->set(snapshot.materials[0].session_id).layers.back().mask.has_value());
  update_inspection(document->set(snapshot.materials[0].session_id).selected_mask ?
                        bts::InspectionMode::Mask :
                        bts::InspectionMode::Material);
};
const auto source = [&](int slot) {
  auto *field = workspace->findChild<QComboBox *>(QString("generatorAnchorSource%1").arg(slot));
  const int index = field->findData(anchor_demo_point.toString());
  if (index < 1)
    qFatal("Generator anchor source missing");
  field->setCurrentIndex(index);
};
const auto source_channel = [&](int value) {
  auto *field = workspace->findChild<QComboBox *>("generatorAnchorChannel0");
  const int index = field->findData(value);
  if (index < 0)
    qFatal("Generator anchor channel missing");
  field->setCurrentIndex(index);
};
const auto source_edit = [&](double value) {
  select_content_anchor_demo(anchor_demo_source, {}, false);
  workspace->findChild<QDoubleSpinBox *>("materialBump")->setValue(value);
  generator_consumer();
};
switch (sets_stage) {
  case 22:
    select_content_anchor_demo(anchor_demo_consumer, {}, true);
    edit_material([&](bts::TextureSet &set) {
      set.mesh_maps = {};
      auto &layer = bts::TextureDocument::layer(set, anchor_demo_consumer);
      layer.effects.clear();
      layer.mask = bts::LayerMask{};
      layer.mask->background = .15;
      set.selected_mask = true;
      set.selected_effect = QUuid();
    });
    update_inspection(bts::InspectionMode::Mask);
    break;
  case 23:
  case 41:
    workspace->findChild<QAction *>("addMaskGenerator")->trigger();
    workspace
        ->findChild<QAction *>(sets_stage == 23 ? "generatorAmbientOcclusion" :
                                                  "generatorThickness")
        ->trigger();
    anchor_demo_fill = document->set(snapshot.materials[0].session_id).selected_effect;
    break;
  case 24:
  case 42:
    source(0);
    break;
  case 25:
    workspace->findChild<QComboBox *>("generatorAnchorComponent0")->setCurrentIndex(2);
    break;
  case 26:
  case 43:
    source_channel(4);
    break;
  case 27:
    source_edit(.7);
    break;
  case 28:
  case 33:
  case 39:
  case 45:
    workspace->findChild<QAction *>("undoDocument")->trigger();
    generator_consumer();
    break;
  case 29:
  case 46:
    source_channel(0);
    break;
  case 30:
    workspace->findChild<QAction *>("generatorDirt")->trigger();
    break;
  case 31:
    source(1);
    break;
  case 32:
    workspace->findChild<QCheckBox *>("generatorInvert")->click();
    break;
  case 34:
  case 49:
    workspace->findChild<QPushButton *>("generatorGoToAnchor0")->click();
    break;
  case 35:
  case 50: {
    auto *list = workspace->findChild<QListWidget *>("anchorReferences");
    if (list->count() != (sets_stage == 35 ? 2 : 1))
      qFatal("Generator consumer navigation missing");
    list->itemActivated(list->item(0));
    update_inspection(sets_stage == 35 ? bts::InspectionMode::Mask :
                                         bts::InspectionMode::Material);
    break;
  }
  case 36:
  case 47:
    save_document(directory + (sets_stage == 36 ? "/generator-anchors.blend" :
                                                  "/content-generator-anchors.blend"));
    break;
  case 37:
  case 48:
    open_document(directory + (sets_stage == 37 ? "/generator-anchors.blend" :
                                                  "/content-generator-anchors.blend"));
    workspace->texture_sets()->setCurrentRow(0);
    generator_consumer();
    break;
  case 38:
    edit_material([&](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, anchor_demo_source);
      for (auto &effect : layer.effects)
        if (effect.id == anchor_demo_point)
          effect.enabled = false;
    });
    generator_consumer();
    break;
  case 40:
    select_content_anchor_demo(anchor_demo_consumer, {}, false);
    edit_material([&](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, anchor_demo_consumer);
      layer.mask.reset();
      layer.effects.clear();
      layer.values.base_color = {.25, .25, .25, 1};
      set.selected_mask = false;
      set.selected_effect = QUuid();
    });
    update_inspection(bts::InspectionMode::Material);
    break;
  case 44:
    source_edit(.8);
    break;
}
