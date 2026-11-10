// SPDX-License-Identifier: GPL-3.0-or-later
#include "InlineNameDelegate.hh"
#include "RenameCommand.hh"
#include <QApplication>
#include <QListWidget>
#include <QKeyEvent>
#include <cassert>
#include <cstdio>
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  QListWidget list;
  auto *item = new QListWidgetItem("Initial", &list);
  item->setFlags(item->flags() | Qt::ItemIsEditable);
  auto *delegate = new bts::InlineNameDelegate(&list);
  list.setItemDelegate(delegate);
  QString committed;
  delegate->renamed = [&](const auto &, const auto &name) { committed = name; };
  list.show();
  list.editItem(item);
  app.processEvents();
  auto *editor = list.findChild<QLineEdit *>("inlineNameEditor");
  assert(editor);
  editor->setText("  Renamed  ");
  delegate->setModelData(editor, list.model(), list.indexFromItem(item));
  assert(committed == "Renamed" && item->text() == "Initial");
  committed.clear();
  editor->setText("   ");
  delegate->setModelData(editor, list.model(), list.indexFromItem(item));
  assert(committed.isEmpty());
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(editor, &escape);
  app.processEvents();
  assert(committed.isEmpty());

  // Exercise the persistent name field used by real Texture Set rows, with
  // actual key/focus delivery (the delegate-only test cannot cover it).
  QListWidget sets;
  auto *set_item = new QListWidgetItem("Material", &sets);
  auto *row = new QWidget(&sets);
  auto *field = new bts::InlineNameField(row);
  field->synchronize("Material");
  sets.setItemWidget(set_item, row);
  sets.setCurrentItem(set_item);
  QString field_commit;
  int commits = 0;
  field->renamed = [&](const QString &name) {
    field_commit = name;
    ++commits;
  };
  sets.show();
  app.processEvents();
  const auto key = [](QWidget *target, int code) {
    QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &event);
  };
  key(&sets, Qt::Key_F2);
  assert(!field->isReadOnly());
  field->setText("  Hero Surface  ");
  key(field, Qt::Key_Return);
  app.processEvents();
  assert(field_commit == "Hero Surface" && commits == 1);
  field->synchronize(field_commit);
  key(field, Qt::Key_F2);
  field->setText("By keypad");
  key(field, Qt::Key_Enter);
  app.processEvents();
  assert(field_commit == "By keypad" && commits == 2);
  field->synchronize(field_commit);
  key(field, Qt::Key_F2);
  field->setText("Focus commit");
  QFocusEvent out(QEvent::FocusOut, Qt::MouseFocusReason);
  QCoreApplication::sendEvent(field, &out);
  app.processEvents();
  assert(field_commit == "Focus commit" && commits == 3);
  field->synchronize(field_commit);
  key(field, Qt::Key_F2);
  field->setText("Cancelled");
  key(field, Qt::Key_Escape);
  app.processEvents();
  assert(commits == 3 && field->text() == "Focus commit");
  key(field, Qt::Key_F2);
  field->setText("   ");
  key(field, Qt::Key_Return);
  app.processEvents();
  assert(commits == 3 && field->text() == "Focus commit");
  key(field, Qt::Key_F2);
  field->setText("Survives refresh");
  key(field, Qt::Key_Return);
  delete field;
  app.processEvents();
  assert(field_commit == "Survives refresh" && commits == 4);
  bts::TextureSet set;
  set.layers.clear();
  bts::TextureLayer layer;
  layer.name = "Paint";
  set.layers.push_back(layer);
  bts::rename_layer_item(set, layer.id, {}, "Stroke");
  assert(set.layers[0].name == "Stroke");
  bts::LevelsEffect effect;
  effect.kind = bts::ContentEffectKind::Anchor;
  effect.anchor_name = "Anchor";
  set.layers[0].effects.push_back(effect);
  bts::rename_layer_item(set, layer.id, effect.id, "Shared AO");
  assert(set.layers[0].effects[0].anchor_name == "Shared AO");
  try {
    bts::rename_layer_item(set, layer.id, {}, " ");
    assert(false);
  }
  catch (const std::invalid_argument &) {
  }
  assert(set.layers[0].name == "Stroke");
  // App labels retain long text; only native datablocks have a byte-sized boundary.
  const QString long_name(70000, 'x');
  list.editItem(item);
  app.processEvents();
  editor = list.findChild<QLineEdit *>("inlineNameEditor");
  assert(editor);
  editor->setText(long_name);
  assert(editor->text() == long_name);
  delegate->setModelData(editor, list.model(), list.indexFromItem(item));
  assert(committed == long_name);
  bts::rename_layer_item(set, layer.id, {}, long_name);
  assert(QString::fromStdString(set.layers[0].name) == long_name);
  set.selected_layer = layer.id;
  bts::TextureDocument::add_mask_anchor(set);
  assert(set.layers[0].effects.back().anchor_name == long_name);
  assert(bts::read_effects(bts::effects_json(set.layers[0].effects)).back().anchor_name ==
         long_name);
  {
    bts::TextureRamQueryScope memory(+[]() -> std::uint64_t { return 0; });
    bool denied = false;
    try {
      bts::checked_name("edited");
    }
    catch (const std::length_error &) {
      denied = true;
    }
    assert(!denied);
  }
  std::puts("Inline naming: trim/empty/Escape, document-only commit and anchor identity PASS");
}
