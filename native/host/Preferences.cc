#include "Theme.hh"
#include "NumericLocale.hh"
#include "Preferences.hh"
#include "BlendLibraryPreferences.hh"
#include <QListWidget>
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <atomic>
#include <algorithm>
#include <limits>
namespace bts {
bool load_trackball_navigation()
{
  QSettings settings("mPainter", "Preferences");
  return settings.value("Viewport/trackball", false).toBool();
}
void save_trackball_navigation(bool enabled)
{
  QSettings settings("mPainter", "Preferences");
  settings.setValue("Viewport/trackball", enabled);
  settings.sync();
}
ReferenceImagePreferences load_reference_image_preferences()
{
  QSettings settings("mPainter", "Preferences");
  ReferenceImagePreferences result;
  result.enabled = settings.value("Viewport/referenceImageEnabled", false).toBool();
  result.path = settings.value("Viewport/referenceImagePath").toString();
  result.center_x = settings.value("Viewport/referenceImageCenterX", 0.5).toDouble();
  result.center_y = settings.value("Viewport/referenceImageCenterY", 0.5).toDouble();
  result.vertical_extent = std::clamp(
      settings.value("Viewport/referenceImageVerticalExtent", 1.15).toDouble(), 0.01, 10000.0);
  return result;
}
void save_reference_image_preferences(const ReferenceImagePreferences &preferences)
{
  QSettings settings("mPainter", "Preferences");
  settings.setValue("Viewport/referenceImageEnabled", preferences.enabled);
  settings.setValue("Viewport/referenceImagePath", preferences.path);
  settings.setValue("Viewport/referenceImageCenterX", preferences.center_x);
  settings.setValue("Viewport/referenceImageCenterY", preferences.center_y);
  settings.setValue("Viewport/referenceImageVerticalExtent", preferences.vertical_extent);
  settings.sync();
}
ViewportClipping load_viewport_clipping()
{
  QSettings settings("mPainter", "Preferences");
  ViewportClipping value{settings.value("Viewport/clipStart", .01).toDouble(),
                         settings.value("Viewport/clipEnd", 1000.).toDouble()};
  return value.valid() ? value : ViewportClipping{};
}
void show_preferences(QWidget *parent,
                      const QString &shelf,
                      bool project_open,
                      ViewportClipping current,
                      std::function<void(ViewportClipping)> preview)
{
  QDialog dialog(parent);
  dialog.setObjectName("shelfPreferencesDialog");
  dialog.setWindowTitle("Preferences");
  dialog.resize(600, 340);
  auto *layout = new QVBoxLayout(&dialog);
  auto *tabs = new QTabWidget(&dialog);
  tabs->setObjectName("preferencesSections");
  layout->addWidget(tabs);
  auto *shelf_page = new QWidget(tabs);
  auto *shelf_layout = new QFormLayout(shelf_page);
  tabs->addTab(shelf_page, "Shelf");
  shelf_layout->addRow(new QLabel("Shelf Locations", shelf_page));
  if (project_open)
    shelf_layout->addRow(
        new QLabel("Shelf locations cannot be changed while a project is open.", shelf_page));
  for (const auto &location :
       {std::pair{QString("User shelf"), shelf},
        std::pair{QString("Built-in"), QString("Application generators and material presets")}})
  {
    auto *path = new QLineEdit(location.second, shelf_page);
    path->setReadOnly(true);
    shelf_layout->addRow(location.first, path);
  }
  auto *library_page = new QWidget(tabs);
  auto *library_layout = new QVBoxLayout(library_page);
  tabs->addTab(library_page, "Material Libraries");
  auto *help = new QLabel(
      "Only paths you add here are indexed. Your Blender default Asset Library is never changed. Open them with File → Browse Blender Asset Library…",
      library_page);
  help->setWordWrap(true);
  library_layout->addWidget(help);
  auto *libraries = new QListWidget(library_page);
  libraries->setObjectName("materialLibraryPaths");
  libraries->addItems(material_library_paths());
  library_layout->addWidget(libraries);
  for (int row = 0; row < libraries->count(); ++row) {
    auto *item = libraries->item(row);
    item->setToolTip(item->text());
    if (!QFileInfo(item->text()).exists()) {
      item->setForeground(QColor(bts::theme::Muted));
      item->setToolTip("Library is offline or missing: " + item->text());
    }
  }
  auto *library_row = new QHBoxLayout;
  auto *library_path = new QLineEdit(library_page);
  library_path->setObjectName("materialLibraryPath");
  library_path->setPlaceholderText("Absolute folder or .blend path");
  auto *library_add = new QPushButton("Add", library_page);
  library_add->setObjectName("addMaterialLibrary");
  auto *library_remove = new QPushButton("Remove", library_page);
  library_remove->setObjectName("removeMaterialLibrary");
  library_row->addWidget(library_path, 1);
  library_row->addWidget(library_add);
  library_row->addWidget(library_remove);
  library_layout->addLayout(library_row);
  auto *library_error = new QLabel(library_page);
  library_error->setObjectName("materialLibraryError");
  library_error->setStyleSheet(bts::theme::qss("color: @Error@"));
  library_error->setWordWrap(true);
  library_layout->addWidget(library_error);
  QObject::connect(library_add, &QPushButton::clicked, &dialog, [&] {
    try {
      const auto path = validated_material_library_path(library_path->text());
      if (libraries->findItems(path, Qt::MatchExactly).isEmpty()) {
        libraries->addItem(path);
      }
      library_path->clear();
      library_error->clear();
    }
    catch (const std::exception &error) {
      library_error->setText(QString::fromUtf8(error.what()));
    }
  });
  QObject::connect(library_remove, &QPushButton::clicked, &dialog, [&] {
    delete libraries->takeItem(libraries->currentRow());
  });
  auto *viewport_page = new QWidget(tabs);
  auto *form = new QFormLayout(viewport_page);
  tabs->addTab(viewport_page, "Viewport");
  const auto number = [&](const char *name, double value) {
    auto *spin = new bts::NumericSpinBox(viewport_page);
    spin->setObjectName(name);
    spin->setLocale(QLocale::c());
    spin->setDecimals(6);
    spin->setRange(ViewportClipping::minimum_near, ViewportClipping::maximum_plane);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    return spin;
  };
  auto *clip_start = number("viewportClipStart", current.near_plane),
       *clip_end = number("viewportClipEnd", current.far_plane);
  clip_start->setSingleStep(.01);
  clip_end->setSingleStep(100);
  form->addRow("Clip Start", clip_start);
  form->addRow("Clip End", clip_end);
  auto *hint = new QLabel(
      "Free orbit view only. Imported cameras retain their own clipping until you navigate.\nOrthographic depth spans ±Clip End around the view.",
      viewport_page);
  hint->setWordWrap(true);
  form->addRow(hint);
  auto *error = new QLabel(viewport_page);
  error->setObjectName("viewportClipError");
  error->setStyleSheet(bts::theme::qss("color:@Error@"));
  error->setWordWrap(true);
  form->addRow(error);
  auto *restore = new QPushButton("Restore viewport defaults", viewport_page);
  restore->setObjectName("restoreViewportClipping");
  form->addRow(restore);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  const auto update = [&] {
    ViewportClipping value{clip_start->value(), clip_end->value()};
    const bool valid = value.valid();
    buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    error->setText(valid ? QString{} : "Clip Start must be positive and smaller than Clip End.");
    if (valid)
      preview(value);
  };
  QObject::connect(clip_start, &QDoubleSpinBox::valueChanged, &dialog, update);
  QObject::connect(clip_end, &QDoubleSpinBox::valueChanged, &dialog, update);
  QObject::connect(restore, &QPushButton::clicked, &dialog, [&] {
    clip_end->setValue(1000);
    clip_start->setValue(.01);
  });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted) {
    QStringList paths;
    for (int row = 0; row < libraries->count(); ++row)
      paths.append(libraries->item(row)->text());
    save_material_library_paths(paths);
    QSettings settings("mPainter", "Preferences");
    settings.setValue("Viewport/clipStart", clip_start->value());
    settings.setValue("Viewport/clipEnd", clip_end->value());
    settings.sync();
  }
  else {
    preview(current);
  }
}
}  // namespace bts
