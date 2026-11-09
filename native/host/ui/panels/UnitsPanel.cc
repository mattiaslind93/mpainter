#include "NumericLocale.hh"
#include "UnitsPanel.hh"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>
namespace bts {
UnitsPanel::UnitsPanel(QLineEdit *path,
                       const UnitSettings &settings,
                       const SourceUnits &source,
                       QWidget *parent,
                       std::string fallback)
    : QWidget(parent), path_(path), fallback_(std::move(fallback)), source_(source)
{
  setObjectName("physicalUnitsPanel");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto *disclosure = new QToolButton(this);
  disclosure->setObjectName("physicalUnitsDisclosure");
  disclosure->setText("Physical size");
  disclosure->setCheckable(true);
  disclosure->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  layout->addWidget(disclosure);
  details_ = new QWidget(this);
  details_->setObjectName("physicalUnitsDetails");
  auto *form = new QFormLayout(details_);
  form->setContentsMargins(12, 0, 0, 0);
  layout->addWidget(details_);
  internal_ = new QCheckBox("Use mesh file's internal unit scale", details_);
  internal_->setObjectName("useInternalUnitScale");
  form->addRow(internal_);
  description_ = new QLabel(details_);
  description_->setObjectName("sourceUnitDescription");
  description_->setWordWrap(true);
  form->addRow(description_);
  custom_row_ = new QWidget(details_);
  auto *row = new QHBoxLayout(custom_row_);
  row->setContentsMargins(0, 0, 0, 0);
  custom_ = new bts::NumericSpinBox(custom_row_);
  custom_->setObjectName("customUnitScale");
  custom_->setDecimals(12);
  custom_->setRange(1e-12, 1e12);
  custom_->setSingleStep(.1);
  unit_ = new QComboBox(custom_row_);
  unit_->setObjectName("customUnitName");
  unit_->addItem("Millimeters", .1);
  unit_->addItem("Centimeters", 1.);
  unit_->addItem("Meters", 100.);
  unit_->addItem("Inches", 2.54);
  unit_->addItem("Feet", 30.48);
  row->addWidget(custom_);
  row->addWidget(unit_);
  form->addRow("Custom unit scale", custom_row_);
  custom_row_->setToolTip(
      "Physical dimensions per original file unit. Changing this keeps geometry and camera fixed.");
  automatic_ = new QCheckBox("Switch suitable fill materials to Physical size when assigning",
                             details_);
  automatic_->setObjectName("autoPhysicalMaterials");
  form->addRow(automatic_);
  connect(disclosure, &QToolButton::toggled, this, [this, disclosure](bool open) {
    details_->setVisible(open);
    disclosure->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
  });
  connect(internal_, &QCheckBox::toggled, this, [this](bool value) {
    custom_row_->setEnabled(!value);
  });
  connect(path_, &QLineEdit::textChanged, this, [this] { describe_source(); });
  set_options(settings);
  disclosure->setChecked(!settings.internal || settings.auto_physical);
  details_->setVisible(disclosure->isChecked());
  disclosure->setArrowType(disclosure->isChecked() ? Qt::DownArrow : Qt::RightArrow);
  describe_source();
}
UnitSettings UnitsPanel::options() const
{
  return {internal_->isChecked(),
          custom_->value() * unit_->currentData().toDouble(),
          automatic_->isChecked()};
}
void UnitsPanel::set_options(const UnitSettings &settings)
{
  internal_->setChecked(settings.internal);
  unit_->setCurrentIndex(1);
  custom_->setValue(settings.custom_cm_per_file_unit);
  automatic_->setChecked(settings.auto_physical);
  custom_row_->setEnabled(!settings.internal);
}
void UnitsPanel::describe_source()
{
  const auto path = path_->text().isEmpty() ? fallback_ : path_->text().toStdString();
  if (path.empty()) {
    description_->setText(
        "OBJ defaults to centimeters; other formats retain their declared units.");
    return;
  }
  try {
    const auto value = inspect_source_units(path);
    description_->setText(
        QString("Source: %1 cm per file unit").arg(value.cm_per_file_unit, 0, 'g', 8));
  }
  catch (...) {
    description_->setText(source_.exact ?
                              QString("Stored source: %1 cm per file unit")
                                  .arg(source_.cm_per_file_unit, 0, 'g', 8) :
                              "Legacy source metadata: reimport the mesh to override units.");
  }
}
}  // namespace bts
