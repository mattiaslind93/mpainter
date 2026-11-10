// SPDX-License-Identifier: GPL-3.0-or-later
#include "FolderPropertiesPanel.hh"
#include "MaskGeneratorPanel.hh"
#include "LayerEffectRowState.hh"
#include "MaskEffectContext.hh"
#include "ImportedEffectPanel.hh"
#include <QApplication>
#include <QDir>
#include <QVBoxLayout>
#include <QListWidget>
#include <cassert>
#include <cstdio>
using namespace bts;
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  app.setStyle("Fusion");
  QWidget window;
  auto *layout = new QVBoxLayout(&window);
  TextureSet set;
  set.layers.clear();
  TextureLayer child, group;
  child.name = "Child";
  child.depth = 1;
  group.kind = LayerKind::Folder;
  group.name = "Surface treatment";
  group.opacity[0] = .42f;
  set.layers = {child, group};
  set.selected_layer = child.id;
  auto *properties = new FolderPropertiesPanel(&window);
  layout->addWidget(properties);
  properties->synchronize(&set, &set.layers.back(), 0);
  assert(properties->findChild<QDoubleSpinBox *>("folderOpacity")->value() == 42);
  assert(properties->findChild<QLabel *>("folderPropertiesName")->text() == "Surface treatment");
  bool folder_opacity = false;
  properties->opacity_changed = [&](QUuid target, int channel, double value) {
    assert(target == group.id && channel == 0 && value == .73);
    folder_opacity = true;
  };
  properties->findChild<QDoubleSpinBox *>("folderOpacity")->setValue(73);
  assert(folder_opacity);
  MaskEffect generator;
  generator.kind = MaskEffectKind::Generator;
  generator.generator = 8;
  generator.bias = .2f;
  generator.grunge_amount = .37f;
  generator.grunge_scale = 32;
  generator.ao_influence = .21f;
  set.layers[0].mask = LayerMask{};
  set.layers[0].mask->effects.push_back(generator);
  set.selected_mask = true;
  set.selected_effect = generator.id;
  MaskGeneratorPanel panel(nullptr);
  int publishes = 0;
  MaskEffect published;
  panel.changed = [&](const auto &value) {
    published = value;
    ++publishes;
  };
  panel.synchronize(&set, &generator);
  assert(publishes == 0);
  assert(panel.findChild<QLabel *>("generatorParametersSection") &&
         panel.findChild<QLabel *>("generatorGrungeSection") &&
         panel.findChild<QLabel *>("generatorMapInfluencesSection"));
  auto *level = panel.findChild<QDoubleSpinBox *>("generatorLevel");
  assert(std::abs(level->value() - .3) < .001);
  for (const char *name : {"generatorLevel",
                           "generatorContrast",
                           "generatorGrungeAmount",
                           "generatorGrungeScale",
                           "generatorAoInfluence",
                           "generatorCurvatureInfluence",
                           "generatorPositionInfluence",
                           "generatorNormalInfluence",
                           "generatorThicknessInfluence"})
    assert(panel.findChild<QSlider *>(QString(name) + "Slider"));
  auto *amount = panel.findChild<QSlider *>("generatorGrungeAmountSlider");
  assert(amount->value() == 370);
  amount->setValue(640);
  assert(publishes == 1 && std::abs(published.grunge_amount - .64f) < .001f &&
         published.generator == 8);
  auto *scale = panel.findChild<QSlider *>("generatorGrungeScaleSlider");
  scale->setValue(1000);
  assert(published.grunge_scale == 4096);
  assert(panel.findChild<QComboBox *>("generatorChannel")->findData(3) < 0);
  LayerEffectRowState state(&window);
  state.synchronize(&set);
  assert(std::get<MaskEffect>(state.values.at(generator.id)).generator == 8);
  set.layers.clear();
  assert(std::get<MaskEffect>(state.values.at(generator.id)).grunge_scale == 32);
  state.synchronize(nullptr);
  assert(state.values.empty());
  // Two isolated Group levels: row metadata and Levels context reach the
  // exact UUID, and the Group editor activates actual descendant UUIDs.
  MaskEffect prefix;
  prefix.kind = MaskEffectKind::Fill;
  prefix.fill = .27f;
  MaskEffect levels;
  levels.kind = MaskEffectKind::Levels;
  MaskEffect later = prefix;
  later.id = QUuid::createUuid();
  later.fill = .9f;
  MaskEffect inner;
  inner.kind = MaskEffectKind::Group;
  inner.group_effects = std::make_shared<const std::vector<MaskEffect>>(
      std::vector{prefix, levels, later});
  MaskEffect outer;
  outer.kind = MaskEffectKind::Group;
  outer.group_effects = std::make_shared<const std::vector<MaskEffect>>(std::vector{inner});
  MaskEffect outside = later;
  outside.id = QUuid::createUuid();
  LayerMask mask;
  mask.background = .8f;
  mask.effects = {outer, outside};
  auto input = mask_effect_input(mask, levels.id);
  assert(input && input->background == 0 && input->effects.size() == 1 &&
         input->effects[0].id == prefix.id);
  assert(!mask_effect_input(mask, QUuid::createUuid()));
  auto root_input = mask_effect_input(mask, outside.id);
  assert(root_input && root_input->background == .8f && root_input->effects.size() == 1);
  set.layers = {TextureLayer{}};
  set.layers[0].mask = mask;
  state.synchronize(&set);
  assert(state.values.contains(levels.id));
  const auto entries = mask_effect_entries(mask.effects, true);
  assert(entries.size() == 6 && entries[1].effect->id == outer.id &&
         entries[3].effect->id == later.id && entries[4].effect->id == levels.id &&
         entries[4].depth == 2);
  // A mask-thumbnail selection clears the effect target even when its parent
  // is unchanged. The stale child row must not remain current after reopening.
  QListWidget rows;
  auto *parent = new QListWidgetItem("Mask parent", &rows);
  parent->setData(Qt::UserRole, set.layers[0].id.toString());
  parent->setData(Qt::UserRole + 8, true);
  auto *effect_row = new QListWidgetItem("Nested Levels", &rows);
  effect_row->setData(Qt::UserRole, set.layers[0].id.toString());
  effect_row->setData(Qt::UserRole + 10, levels.id.toString());
  effect_row->setData(Qt::UserRole + 12, true);
  rows.setCurrentItem(effect_row);
  set.selected_layer = set.layers[0].id;
  set.selected_mask = true;
  set.selected_effect = levels.id;
  assert(LayerEffectRowState::current_target_matches(rows.currentIndex(), set));
  set.selected_effect = QUuid();
  assert(!LayerEffectRowState::current_target_matches(rows.currentIndex(), set));
  int changes = 0;
  QObject::connect(&rows, &QListWidget::currentItemChanged, &rows, [&] { ++changes; });
  {
    const QSignalBlocker blocker(&rows);
    rows.setCurrentItem(parent);
  }
  assert(changes == 0 && LayerEffectRowState::current_target_matches(rows.currentIndex(), set));
  rows.setCurrentItem(effect_row);
  assert(changes == 1);
  set.selected_effect = levels.id;
  set.selected_mask = false;
  assert(!LayerEffectRowState::current_target_matches(rows.currentIndex(), set));
  effect_row->setData(Qt::UserRole + 12, false);
  assert(LayerEffectRowState::current_target_matches(rows.currentIndex(), set));
  ImportedEffectPanel imported(nullptr);
  QUuid activated;
  imported.child_selected = [&](const QUuid &id) { activated = id; };
  imported.synchronize(nullptr, &outer);
  auto *group_blend = imported.findChild<QComboBox *>("orderedEffectBlend");
  auto *group_model = qobject_cast<QStandardItemModel *>(group_blend->model());
  assert(group_model &&
         !group_model->item(group_blend->findData(int(LayerBlend::Passthrough)))->isEnabled());
  auto *tree = imported.findChild<QTreeWidget *>("maskGroupChildren");
  assert(tree->topLevelItemCount() == 1);
  auto *levels_item = tree->topLevelItem(0)->child(1);
  assert(QUuid(levels_item->data(0, Qt::UserRole).toString()) == levels.id);
  QMetaObject::invokeMethod(tree,
                            "itemActivated",
                            Qt::DirectConnection,
                            Q_ARG(QTreeWidgetItem *, levels_item),
                            Q_ARG(int, 0));
  assert(activated == levels.id);
  if (argc > 1) {
    QDir().mkpath(argv[1]);
    panel.resize(450, 1100);
    panel.show();
    app.processEvents();
    assert(panel.grab().save(QString(argv[1]) + "/painter-generator-controls.png"));
  }
  {
    MaskGeneratorPanel painter(nullptr);
    MaskEffect source;
    source.kind = MaskEffectKind::Generator;
    source.generator = 8;
    source.response = 1;
    source.contrast = 5.2f;
    source.bias = .08f;
    MaskEffect changed;
    int edits = 0;
    painter.changed = [&](const auto &e) {
      changed = e;
      ++edits;
    };
    painter.synchronize(&set, &source);
    auto *contrast = painter.findChild<QDoubleSpinBox *>("generatorContrast");
    auto *balance = painter.findChild<QDoubleSpinBox *>("generatorLevel");
    assert(edits == 0 && contrast->maximum() == 1 && std::abs(contrast->value() - .6) < 1e-4 &&
           std::abs(balance->value() - .58) < 1e-4);
    contrast->setValue(.2);
    assert(edits == 1 && std::abs(changed.contrast - 2.4f) < 1e-4f);
    balance->setValue(.8);
    assert(edits == 2 && std::abs(changed.bias - .3f) < 1e-4f);
    source.response = 0;
    source.contrast = 3;
    painter.synchronize(&set, &source);
    assert(edits == 2 && contrast->maximum() == 8 && contrast->value() == 3);
  }
  std::puts(
      "Painter document UI: folder properties, generator parameters/sliders, hidden Normal and copied effect metadata PASS");
}
