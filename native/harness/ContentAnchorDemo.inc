// SPDX-License-Identifier: GPL-3.0-or-later
const auto select_content_anchor_demo = [&](QUuid parent, QUuid effect, bool mask) {
  auto *list = workspace->layer_stack();
  for (int row = 0; row < list->count(); ++row)
    if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == parent &&
        list->item(row)->data(Qt::UserRole + 10).toString().isEmpty())
    {
      list->scrollToItem(list->item(row));
      list->setCurrentRow(row);
      const auto index = list->model()->index(row, 0);
      const auto rect = list->visualRect(index);
      const QPoint p = mask ? bts::layer_mask_rect(index, rect).center() :
                              QPoint(bts::layer_thumbnail_x(index, rect) + 12, rect.center().y());
      send_mouse_event(*list->viewport(),
                       QEvent::MouseButtonPress,
                       p,
                       Qt::LeftButton,
                       Qt::LeftButton,
                       Qt::NoModifier);
      send_mouse_event(*list->viewport(),
                       QEvent::MouseButtonRelease,
                       p,
                       Qt::LeftButton,
                       Qt::NoButton,
                       Qt::NoModifier);
      break;
    }
  if (effect.isNull())
    return;
  for (int row = 0; row < list->count(); ++row)
    if (QUuid(list->item(row)->data(Qt::UserRole).toString()) == parent &&
        QUuid(list->item(row)->data(Qt::UserRole + 10).toString()) == effect)
    {
      list->scrollToItem(list->item(row));
      list->setCurrentRow(row);
      return;
    }
  qFatal("Content anchor demo row missing");
};
const auto consumer = [&] {
  select_content_anchor_demo(anchor_demo_consumer, anchor_demo_fill, true);
  update_inspection(bts::InspectionMode::Mask);
};
const auto channel = [&](int value) {
  auto *field = workspace->findChild<QComboBox *>("anchorSourceChannel");
  const int i = field->findData(value);
  if (i < 0)
    qFatal("Anchor channel unavailable");
  field->setCurrentIndex(i);
};
if (qEnvironmentVariableIsSet("BTS_GENERATOR_ANCHORS") && sets_stage >= 22) {
#include "GeneratorAnchorDemo.inc"
}
else if (qEnvironmentVariableIsSet("BTS_FILL_ANCHORS") && sets_stage >= 22) {
#include "FillAnchorDemo.inc"
}
else
  switch (sets_stage) {
    case 1:
      open_document(parser.value("anchor-reference-demo"));
      workspace->texture_sets()->setCurrentRow(0);
      edit_material([&](bts::TextureSet &set) {
        set.layers.resize(1);
        auto &source = set.layers.front();
        anchor_demo_source = source.id;
        source.kind = bts::LayerKind::Fill;
        source.fill = {};
        source.effects.clear();
        source.visible = false;
        source.opacity.fill(0);
        source.channels.fill(true);
        source.values.base_color = {.2, .4, .6, 1};
        source.values.bump = -.25;
        source.mask = bts::LayerMask{};
        source.mask->background = 0;
        set.precision.fill(bts::ChannelPrecision::RGB32F);
        set.precision[4] = bts::ChannelPrecision::L32F;
        set.displacement.channel = set.displacement.allocated = true;
        source.displacement.value = -.002;
        set.users[0].allocated = set.users[0].present = true;
        set.users[0].name = "Anchor scalar";
        source.users[0].value = 2.25;
        set.selected_layer = source.id;
        set.selected_mask = false;
        set.selected_effect = QUuid();
      });
      viewport->set_layout({bts::ViewLayout::Split, false});
      viewport->focus_uv({.5, .5}, 1.3);
      workspace->layer_settings_tabs()->setCurrentIndex(0);
      workspace->inspector_splitter()->setSizes({100, 340, 410, 0});
      break;
    case 2:
      workspace->findChild<QAction *>("addMaskAnchor")->trigger();
      anchor_demo_point = document->set(snapshot.materials[0].session_id).selected_effect;
      break;
    case 3: {
      auto *name = workspace->findChild<QLineEdit *>("anchorName");
      name->setText("Raw content");
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
      QApplication::sendEvent(name, &event);
      break;
    }
    case 4:
      workspace->findChild<QToolButton *>("addFillLayer")->click();
      anchor_demo_consumer = document->set(snapshot.materials[0].session_id).selected_layer;
      break;
    case 5:
      workspace->findChild<QAction *>("addBlackMask")->trigger();
      update_inspection(bts::InspectionMode::Mask);
      break;
    case 6:
      workspace->findChild<QAction *>("addMaskFillEffect")->trigger();
      anchor_demo_fill = document->set(snapshot.materials[0].session_id).selected_effect;
      break;
    case 7: {
      auto *resource = workspace->findChild<QComboBox *>("maskFillResource");
      const int index = resource->findData(anchor_demo_point.toString());
      if (index < 2)
        qFatal("Content anchor not offered");
      resource->setCurrentIndex(index);
      break;
    }
    case 8:
      workspace->findChild<QComboBox *>("anchorSourceComponent")->setCurrentIndex(2);
      break;
    case 9:
      workspace->findChild<QCheckBox *>("anchorUseSourceCoverage")->click();
      break;
    case 10:
      workspace->findChild<QCheckBox *>("anchorUseSourceCoverage")->click();
      break;
    case 11:
      channel(4);
      break;
    case 12:
      select_content_anchor_demo(anchor_demo_source, {}, false);
      workspace->findChild<QDoubleSpinBox *>("materialBump")->setValue(.35);
      consumer();
      break;
    case 13:
      workspace->findChild<QAction *>("undoDocument")->trigger();
      consumer();
      break;
    case 14:
      channel(14);
      break;
    case 15:
      channel(6);
      break;
    case 16:
      workspace->findChild<QPushButton *>("goToMaskAnchor")->click();
      break;
    case 17: {
      auto *list = workspace->findChild<QListWidget *>("anchorReferences");
      if (list->count() != 1)
        qFatal("Missing content reference navigation");
      list->itemActivated(list->item(0));
      update_inspection(bts::InspectionMode::Mask);
      break;
    }
    case 18:
      save_document(directory + "/content-anchors.blend");
      break;
    case 19:
      open_document(directory + "/content-anchors.blend");
      workspace->texture_sets()->setCurrentRow(0);
      update_inspection(bts::InspectionMode::Mask);
      break;
    case 20:
      channel(4);
      select_content_anchor_demo(anchor_demo_source, {}, false);
      workspace->findChild<QDoubleSpinBox *>("materialBump")->setValue(.6);
      consumer();
      break;
    case 21:
      workspace->findChild<QAction *>("undoDocument")->trigger();
      consumer();
      break;
  }
