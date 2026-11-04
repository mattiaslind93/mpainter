#include "Theme.hh"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <cassert>
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  bts::theme::apply(app);
  assert(!app.styleSheet().contains('@'));
  assert(app.palette().color(QPalette::Accent) == QColor(bts::theme::Accent));
  assert(app.palette().color(QPalette::Disabled, QPalette::Text) == QColor(bts::theme::Disabled));
  // Parentless dialogs and scroll contents must inherit the same dark palette.
  QDialog dialog; dialog.resize(600, 400);
  QScrollArea scroll(&dialog); scroll.setGeometry(10, 10, 200, 100);
  auto *body = new QWidget; scroll.setWidget(body); scroll.setWidgetResizable(true);
  QPushButton button("Create", &dialog); button.move(230, 20);
  QListWidget list(&dialog); list.setGeometry(230, 80, 200, 200);
  list.addItems({"Studio Environment", "Sphere"}); list.setCurrentRow(0);
  dialog.show(); app.processEvents();
  assert(dialog.palette().color(QPalette::Window) == QColor(bts::theme::Surface));
  assert(body->palette().color(QPalette::Window) == QColor(bts::theme::Surface));
  assert(dialog.grab().toImage().pixelColor(590, 390) == QColor(bts::theme::Surface));
  assert(list.viewport()->grab().toImage().pixelColor(10, 160) == QColor(bts::theme::Canvas));
  QCheckBox check("Import cameras", &dialog); check.setGeometry(20, 250, 150, 30);
  check.show(); app.processEvents();
  const auto unchecked = check.grab().toImage();
  check.setChecked(true); app.processEvents();
  assert(check.grab().toImage() != unchecked); // Native checked mark remains visible.
  QMenu menu; menu.addAction("New project"); menu.ensurePolished();
  assert(menu.palette().color(QPalette::Window) == QColor(bts::theme::Canvas));
}
