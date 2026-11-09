#include "Theme.hh"
#pragma once
#include "MaterialId.hh"
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QColor>
namespace bts {
inline std::optional<MaterialIdColor> choose_material_id(QWidget *parent, MaterialIdColor initial)
{
  QDialog dialog(parent);
  dialog.setObjectName("materialIdDialog");
  dialog.setWindowTitle("Material ID color");
  auto *layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(
      "Choose an ID color, or enter an RGB hex value.\nA folder color applies to its visible children.",
      &dialog));
  auto *hex = new QLineEdit(QColor(initial[0], initial[1], initial[2]).name(), &dialog);
  hex->setObjectName("materialIdHex");
  auto *grid = new QGridLayout;
  for (int i = 0; i < int(material_id_palette.size()); ++i) {
    const auto c = material_id_palette[i];
    const auto name = QColor(c[0], c[1], c[2]).name();
    auto *button = new QPushButton(&dialog);
    button->setObjectName(QString("materialIdColor%1").arg(i));
    button->setFixedSize(32, 32);
    button->setStyleSheet(bts::theme::qss("background:" + name + ";border:1px solid @Muted@;"));
    button->setToolTip(name);
    grid->addWidget(button, i / 10, i % 10);
    QObject::connect(button, &QPushButton::clicked, &dialog, [=] { hex->setText(name); });
  }
  layout->addLayout(grid);
  layout->addWidget(hex);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(hex, &QLineEdit::textChanged, &dialog, [=](const QString &value) {
    buttons->button(QDialogButtonBox::Ok)
        ->setEnabled(value.size() == 7 && value.startsWith('#') && QColor(value).isValid());
  });
  if (dialog.exec() != QDialog::Accepted)
    return {};
  const QColor color(hex->text());
  return MaterialIdColor{color.red(), color.green(), color.blue()};
}
}  // namespace bts
