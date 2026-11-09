// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include "TextureExtent.hh"
#include "UsdTextureBindings.hh"
#include "UsdMeshExport.hh"
#include "MeshExport.hh"
#include "ExportNaming.hh"
#include "TextureExportDialog.hh"
#include "NativeExportGuard.hh"
#ifdef BTS_WITH_MATERIALX
#  include "OpenPbrTexturePackage.hh"
#  include "OpenPbrUsd.hh"
#endif
#include "ExportConfiguration.hh"
#include "ExportNaming.hh"
#include <QTableWidget>
#include <QGroupBox>
#include <QSpinBox>
#include <QHeaderView>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include "FileDialogs.hh"
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QPointer>
#include <QVBoxLayout>
#include <map>
#include <mutex>
#include <thread>
namespace bts {
namespace {
using Choice = ExportConfiguration;
using OutputPreset = ExportOutputPreset;
class ExportDialog : public QDialog {
 public:
  using QDialog::QDialog;
  std::function<bool()> may_close;
  void reject() override
  {
    if (!may_close || may_close())
      QDialog::reject();
  }
};
}  // namespace
bool choose_texture_export(QWidget *parent,
                           TextureDocument &document,
                           const ExportSource &source,
                           const ExportMeshSource &mesh_source,
                           bool displacement_available,
                           const ExportAsyncSource &async_source)
{
  const auto original = document.export_configuration();
  std::vector<QUuid> ids;
  for (const auto &set : document.texture_sets())
    ids.push_back(set.id);
  Choice initial = read_export_configuration(original, ids);
  if (original.isEmpty() &&
      std::any_of(document.texture_sets().begin(),
                  document.texture_sets().end(),
                  [](const auto &set) { return set.shader_parameters.openpbr.has_value(); }))
    initial.naming_convention = ExportNamingConvention::OpenPbr;

  ExportDialog dialog(parent);
  dialog.setObjectName("textureExportDialog");
  dialog.setWindowTitle("Export textures");
  dialog.resize(1000, 700);
  auto *layout = new QVBoxLayout(&dialog);
  auto *tabs = new QTabWidget(&dialog);
  tabs->setObjectName("exportTabs");
  layout->addWidget(tabs, 1);
  auto *settings = new QWidget(tabs);
  auto *body = new QHBoxLayout(settings);
  tabs->addTab(settings, "Settings");
  auto *sets = new QListWidget(settings);
  sets->setObjectName("exportTextureSets");
  sets->setMaximumWidth(250);
  auto *left = new QVBoxLayout;
  body->addLayout(left);
  auto *global = new QPushButton("Global settings", settings);
  global->setObjectName("exportGlobalSettings");
  left->addWidget(global);
  left->addWidget(sets);
  for (const auto &set : document.texture_sets()) {
    auto *item = new QListWidgetItem(QString::fromStdString(set.name), sets);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setData(Qt::UserRole, set.id);
    item->setCheckState(!set.disabled && initial.selected.contains(set.id) ? Qt::Checked :
                                                                             Qt::Unchecked);
    if (set.disabled) {
      item->setFlags(Qt::NoItemFlags);
      item->setToolTip("Unused Texture Set; reassign it before export");
    }
  }
  auto *right = new QFormLayout;
  body->addLayout(right, 1);
  auto *directory = new QLineEdit(initial.directory, settings);
  directory->setObjectName("exportDirectory");
  auto *browse = new QPushButton("Browse…", settings);
  auto *path = new QHBoxLayout;
  path->addWidget(directory);
  path->addWidget(browse);
  right->addRow("Output directory", path);
  QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
    const auto selected = bts::FileDialog::getExistingDirectory(
        &dialog, "Output directory", directory->text());
    if (!selected.isEmpty())
      directory->setText(selected);
  });
  auto custom = initial.presets;
  const auto id_preset = material_id_preset();
  if (std::none_of(
          custom.begin(), custom.end(), [&](const auto &p) { return p.id == id_preset.id; }))
    custom.push_back(id_preset);
  const auto studio = studio_preset();
  if (std::none_of(custom.begin(), custom.end(), [&](const auto &p) { return p.id == studio.id; }))
    custom.insert(custom.begin(), studio);
  const auto converted = converted_specular_glossiness_preset();
  if (std::none_of(
          custom.begin(), custom.end(), [&](const auto &p) { return p.id == converted.id; }))
    custom.push_back(converted);
  const auto usd_preset = usd_metal_rough_preset();
  if (std::none_of(
          custom.begin(), custom.end(), [&](const auto &p) { return p.id == usd_preset.id; }))
    custom.push_back(usd_preset);
  OutputPreset recommended;
  recommended.id = QUuid("6e15bcb2-191a-4e19-b6d1-5db2bea50003");
  recommended.name = "PBR channels (recommended depths)";
  set_default_export_formats(recommended);
  for (int c = 0; c < 9; ++c)
    recommended.naming[c] = "$mesh_$textureSet_" + std::string(export_channel_suffixes[c]);
  if (std::none_of(
          custom.begin(), custom.end(), [&](const auto &p) { return p.id == recommended.id; }))
    custom.push_back(recommended);
  auto *preset = new QComboBox(settings);
  preset->setObjectName("exportPreset");
  preset->addItem("Document channels", QUuid{});
  for (const auto &entry : custom)
    preset->addItem(entry.name, entry.id);
  preset->setCurrentIndex(std::max(0, preset->findData(initial.preset)));
  right->addRow("Output template", preset);
  auto *mesh_displacement = new QCheckBox("Export Mesh With Displacement/Tessellation", settings);
  mesh_displacement->setObjectName("exportUsdDisplacement");
  mesh_displacement->setChecked(initial.mesh_displacement && displacement_available);
  mesh_displacement->setToolTip(
      "Requires an enabled nonzero Displacement preview with channel content; original geometry is retained.");
  right->addRow(mesh_displacement);
  auto *format = new QComboBox(settings);
  format->setObjectName("exportFormat");
  format->addItems({"png", "exr", "jpeg"});
  auto *precision = new QComboBox(settings);
  precision->setObjectName("exportPrecision");
  auto *encoding = new QHBoxLayout;
  encoding->addWidget(format);
  encoding->addWidget(precision, 1);
  auto *based_on_template = new QLabel("Based on output template", settings);
  based_on_template->setObjectName("exportBasedOnTemplate");
  encoding->addWidget(based_on_template);
  right->addRow("File type / precision", encoding);
  auto *convention = new QComboBox(settings);
  convention->setObjectName("exportNamingConvention");
  convention->addItems({"Classic / Painter", "OpenPBR"});
  convention->setCurrentIndex(int(initial.naming_convention));
  convention->setToolTip(
      "Rename standard channel suffixes; custom names stay authored. Bump and Displacement remain scalar and lowercase. No normal map is exported.");
  right->addRow("Naming convention", convention);
  auto *materialx = new QCheckBox("OpenPBR MaterialX / USD package (EXR32 Document channels)",
                                  settings);
  materialx->setObjectName("exportOpenPbrMaterialX");
  materialx->setChecked(initial.openpbr_materialx);
  right->addRow(materialx);
  auto *materialx_note = new QLabel(
      "MaterialX embeds the ASWF 1.1.1 reference and license; a USD material library is included, plus USDC/USDZ scene geometry when available. Bump/Displacement stay signed float32. AO is exported separately; the reference BSDF has no baked-AO input. The viewport remains a Principled approximation.",
      settings);
  materialx_note->setWordWrap(true);
  right->addRow(materialx_note);
  const auto update_precision = [&] {
    const int previous = precision->currentData().toInt();
    precision->clear();
    if (format->currentIndex() == 0) {
      precision->addItem("8 bits", int(ExportFormat::Png8));
      precision->addItem("16 bits", int(ExportFormat::Png16));
    }
    else if (format->currentIndex() == 2) {
      precision->addItem("8 bits", int(ExportFormat::Jpeg8));
    }
    else {
      precision->addItem("16f bits (HALF)", int(ExportFormat::Exr16));
      precision->addItem("32f bits (FLOAT)", int(ExportFormat::Exr32));
    }
    const int match = precision->findData(previous);
    precision->setCurrentIndex(match < 0 ? 0 : match);
  };
  QObject::connect(format, &QComboBox::currentIndexChanged, &dialog, update_precision);
  format->setCurrentIndex(initial.format == ExportFormat::Jpeg8 ? 2 :
                          int(initial.format) >= 2              ? 1 :
                                                                  0);
  update_precision();
  precision->setCurrentIndex(precision->findData(int(initial.format)));
  const auto selected_preset = [&]() -> const OutputPreset * {
    for (const auto &entry : custom)
      if (entry.id == preset->currentData().toUuid())
        return &entry;
    return nullptr;
  };
  const auto update_format_state = [&] {
    const auto *entry = selected_preset();
    const bool usd = entry && entry->kind == ExportPresetKind::UsdMetalRough;
    const bool fixed = usd || (entry && entry->kind == ExportPresetKind::Studio);
    format->setEnabled(!fixed);
    precision->setEnabled(!fixed);
    format->setVisible(!fixed);
    precision->setVisible(!fixed);
    based_on_template->setVisible(fixed);
    mesh_displacement->setVisible(usd);
    mesh_displacement->setEnabled(usd && displacement_available);
  };
  QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, update_format_state);
  update_format_state();
  const auto update_materialx = [&] {
    bool valid = true;
    int checked = 0;
    for (int row = 0; row < sets->count(); ++row)
      if (sets->item(row)->checkState() == Qt::Checked) {
        ++checked;
        const auto id = sets->item(row)->data(Qt::UserRole).toUuid();
        const auto found = std::find_if(document.texture_sets().begin(),
                                        document.texture_sets().end(),
                                        [&](const auto &set) { return set.id == id; });
        if (found == document.texture_sets().end() || !found->shader_parameters.openpbr ||
            !found->uniform_editable || found->disabled)
          valid = false;
      }
    valid = valid && checked > 0 && !selected_preset();
#ifndef BTS_WITH_MATERIALX
    valid = false;
#endif
    materialx->setEnabled(valid);
    materialx->setToolTip(
        valid ?
            "Self-contained MaterialX reference material plus EXR32 maps and OpenPBR-LICENSE.txt" :
            "Choose Document channels and select only editable OpenPBR-compatibility Texture Sets.");
    if (!valid && materialx->isChecked()) {
      const QSignalBlocker block(materialx);
      materialx->setChecked(false);
    }
    if (materialx->isChecked()) {
      format->setCurrentIndex(1);
      precision->setCurrentIndex(precision->findData(int(ExportFormat::Exr32)));
    }
    update_format_state();
    if (materialx->isChecked()) {
      format->setEnabled(false);
      precision->setEnabled(false);
    }
  };
  QObject::connect(materialx, &QCheckBox::toggled, &dialog, [&] { update_materialx(); });
  QObject::connect(sets, &QListWidget::itemChanged, &dialog, [&] { update_materialx(); });
  QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, [&] { update_materialx(); });
  update_materialx();
  int global_size = initial.size;
  auto local_sizes = initial.sizes;
  QUuid editing_set;
  auto *scope = new QLabel("Global settings", settings);
  scope->setObjectName("exportSizeScope");
  right->addRow("Size scope", scope);
  auto *size = new QComboBox(settings);
  size->setObjectName("exportSize");
  size->addItem(QString("Based on each Texture Set size"), 0);
  for (int pixels = 128; pixels <= max_texture_resolution; pixels *= 2)
    size->addItem(QString("%1 × %1").arg(pixels), pixels);
  auto *size_override = new QCheckBox("Enable override", settings);
  size_override->setObjectName("exportSizeOverride");
  auto *sizing = new QHBoxLayout;
  sizing->addWidget(size, 1);
  sizing->addWidget(size_override);
  right->addRow("Size", sizing);
  const auto refresh_size = [&] {
    QSignalBlocker size_block(size), override_block(size_override);
    const bool local = !editing_set.isNull();
    size_override->setVisible(local);
    size_override->setChecked(local && local_sizes.contains(editing_set));
    const int value = local && local_sizes.contains(editing_set) ? local_sizes.at(editing_set) :
                                                                   global_size;
    size->setCurrentIndex(size->findData(value));
    size->setEnabled(!local || size_override->isChecked());
    scope->setText(local ? sets->currentItem()->text() : "Global settings");
    QSignalBlocker list_block(sets);
    for (int row = 0; row < sets->count(); ++row) {
      const auto &set = document.texture_sets().at(row);
      sets->item(row)->setText(QString::fromStdString(set.name) +
                               (local_sizes.contains(set.id) ? " *" : ""));
    }
  };
  refresh_size();
  auto *padding = new QComboBox(settings);
  padding->setObjectName("exportPadding");
  for (int mode = 0; mode < 5; ++mode)
    padding->addItem(export_padding_names[mode], mode);
  padding->setCurrentIndex(int(initial.padding));
  auto *padding_width = new QSpinBox(settings);
  padding_width->setObjectName("exportPaddingWidth");
  padding_width->setRange(0, 256);
  padding_width->setValue(initial.padding_width);
  padding_width->setEnabled(int(initial.padding) >= 2);
  auto *padding_row = new QHBoxLayout;
  padding_row->addWidget(padding, 1);
  padding_row->addWidget(padding_width);
  right->addRow("Padding", padding_row);
  QObject::connect(padding, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
    padding_width->setEnabled(index >= 2);
  });
  auto *color = new QLabel(
      "PNG Base Color: sRGB. EXR Base Color: scene-linear working space.\nOther channels: raw data. PNG clamps values to 0–1; EXR retains signed Bump and Displacement.\nHeight maps: grayscale Bump and grayscale Displacement, one channel each. Bump does not move the surface.\nNormal maps are not exported.\nMixed_AO: complete baked/authored result. Transparent padding adds alpha.",
      settings);
  color->setWordWrap(true);
  right->addRow("Encoding", color);
  auto *templates = new QWidget(tabs);
  auto *template_layout = new QVBoxLayout(templates);
  auto *template_body = new QHBoxLayout;
  template_layout->addLayout(template_body, 1);
  auto *preset_list = new QListWidget(templates);
  preset_list->setObjectName("exportPresets");
  preset_list->setMaximumWidth(260);
  preset_list->addItem("Document channels");
  for (const auto &entry : custom)
    preset_list->addItem(entry.name);
  template_body->addWidget(preset_list);
  auto *editor = new QVBoxLayout;
  template_body->addLayout(editor, 1);
  auto *preset_name = new QLineEdit(templates);
  preset_name->setObjectName("exportPresetName");
  editor->addWidget(preset_name);
  auto *new_packed = new QPushButton("New R+G+B preset", templates);
  new_packed->setObjectName("newPackedExportPreset");
  editor->addWidget(new_packed);
  auto *duplicate = new QPushButton("Duplicate preset", templates);
  duplicate->setObjectName("duplicateExportPreset");
  editor->addWidget(duplicate);
  auto *table = new QTableWidget(9, 3, templates);
  table->setObjectName("exportOutputNames");
  table->setHorizontalHeaderLabels(
      {"Output name (without extension)", "Source", "File type / depth"});
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  editor->addWidget(table, 1);
  const std::array<const char *, 9> channels{"Base_Color",
                                             "Metallic",
                                             "Roughness",
                                             "Normal",
                                             "Bump",
                                             "Ambient_occlusion",
                                             "Converted Normal (project format)",
                                             "Mixed_AO",
                                             "Displacement"};
  for (int row = 0; row < 9; ++row) {
    table->setItem(row, 0, new QTableWidgetItem);
    auto *source_item = new QTableWidgetItem(channels[row]);
    source_item->setFlags(source_item->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, 1, source_item);
  }
  auto *tokens = new QLabel(
      "Tokens: $mesh, $textureSet, $channel, $udim. / creates a subfolder.\nMulti-tile outputs append a tile number if $udim is absent.\nEditing a duplicate does not select it in Settings.",
      templates);
  tokens->setWordWrap(true);
  editor->addWidget(tokens);
  tabs->addTab(templates, "Output Templates");
  auto *packing = new QGroupBox("Independent output components", templates);
  auto *pack_layout = new QFormLayout(packing);
  auto *packed_name = new QLineEdit(packing);
  packed_name->setObjectName("exportPackedName");
  pack_layout->addRow("Output name", packed_name);
  std::array<QComboBox *, 3> packed_sources{};
  for (int c = 0; c < 3; ++c) {
    auto *combo = new QComboBox(packing);
    combo->setObjectName(QString("exportPacked%1").arg(QString("RGB").mid(c, 1)));
    for (int source = 0; source < 8; ++source)
      combo->addItem(packed_source_names[source], source);
    packed_sources[c] = combo;
    pack_layout->addRow(QString("RGB").mid(c, 1), combo);
  }
  editor->addWidget(packing);
  auto *derived_box = new QGroupBox("Derived output maps", templates);
  auto *derived_layout = new QHBoxLayout(derived_box);
  auto *derived_left = new QVBoxLayout;
  derived_layout->addLayout(derived_left, 1);
  auto *derived_table = new QTableWidget(0, 4, derived_box);
  derived_table->setObjectName("exportDerivedOutputs");
  derived_table->setHorizontalHeaderLabels(
      {"Output name", "Source", "Painted alpha", "File type / depth"});
  derived_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  derived_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  derived_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  derived_left->addWidget(derived_table);
  auto *remove_derived = new QPushButton("Remove output", derived_box);
  remove_derived->setObjectName("removeDerivedExportOutput");
  derived_left->addWidget(remove_derived);
  auto *sources = new QListWidget(derived_box);
  sources->setObjectName("convertedExportSources");
  sources->setMaximumWidth(190);
  for (int source = 0; source < int(export_map_labels.size()); ++source) {
    auto *item = new QListWidgetItem(QString::fromUtf8(export_map_labels[source]), sources);
    item->setData(Qt::UserRole, source);
    if (export_map_normal(ExportMapSource(source))) {
      item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
      item->setToolTip(
          "Normal map export can be added later. Bump and Displacement stay one-channel grayscale.");
    }
    else
      item->setForeground(QColor(bts::theme::Warning));
  }
  for (auto *name : {"Unity4 Diffuse", "Unity4 Gloss", "2D View"}) {
    auto *item = new QListWidgetItem(name, sources);
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    item->setToolTip("Legacy shader or view-dependent conversion is not supported yet.");
  }
  sources->setToolTip("Double-click a converted map to add an output to an editable template.");
  derived_layout->addWidget(sources);
  editor->addWidget(derived_box, 1);
  std::function<void()> derived_changed;
  const auto encoding_combo =
      [&](QWidget *parent, std::optional<ExportFormat> value, const QString &name) {
        if (auto *old = parent->findChild<QComboBox *>(name))
          old->setObjectName({});
        auto *combo = new QComboBox(parent);
        combo->setObjectName(name);
        combo->addItem("Inherit global", -1);
        for (int i = 0; i < 5; ++i)
          combo->addItem(export_format_labels[i], i);
        combo->setCurrentIndex(combo->findData(value ? int(*value) : -1));
        return combo;
      };
  const auto combo_format = [](QComboBox *combo) -> std::optional<ExportFormat> {
    const int value = combo->currentData().toInt();
    return value < 0 ? std::nullopt : std::optional(ExportFormat(value));
  };
  auto *packed_format = encoding_combo(packing, {}, "exportPackedFormat");
  pack_layout->addRow("File type / depth", packed_format);

  const auto refresh_editor = [&] {
    const int row = preset_list->currentRow();
    QSignalBlocker name_block(preset_name), table_block(table);
    const bool editable = row > 0;
    const bool usd = editable && custom.at(row - 1).kind == ExportPresetKind::UsdMetalRough;
    const bool studio_row = editable && custom.at(row - 1).kind == ExportPresetKind::Studio;
    remove_derived->setEnabled(editable && !usd && !studio_row);
    sources->setEnabled(editable && !usd && !studio_row);
    for (int c = 0; c < 9; ++c)
      table->setRowHidden(c,
                          c == 3 || c == 6 || (usd && c != 0 && c != 1 && c != 2 && c != 7) ||
                              (studio_row && (c == 5 || c == 7)));
    const bool has_packing = editable && custom.at(row - 1).packed.has_value();
    const QSignalBlocker derived_block(derived_table);
    derived_table->setRowCount(editable ? int(custom.at(row - 1).outputs.size()) : 0);
    derived_box->setEnabled(editable);
    if (editable) {
      const auto &outputs = custom.at(row - 1).outputs;
      for (int n = 0; n < int(outputs.size()); ++n) {
        auto *name = new QTableWidgetItem(QString::fromStdString(outputs[n].name));
        derived_table->setItem(n, 0, name);
        auto *combo = new QComboBox(derived_table);
        for (int source = 0; source < int(export_map_labels.size()); ++source)
          combo->addItem(QString::fromUtf8(export_map_labels[source]), source);
        combo->setCurrentIndex(int(outputs[n].source));
        combo->setEnabled(!usd);
        derived_table->setCellWidget(n, 1, combo);
        const bool categorical = outputs[n].source == ExportMapSource::MaterialId;
        auto *alpha = new QTableWidgetItem;
        alpha->setFlags(usd || categorical ? Qt::NoItemFlags :
                                             Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        alpha->setCheckState(!categorical && outputs[n].alpha ? Qt::Checked : Qt::Unchecked);
        alpha->setToolTip("Export the painted Base Color coverage as alpha.");
        derived_table->setItem(n, 2, alpha);
        auto *depth = encoding_combo(
            derived_table,
            categorical ? std::optional<ExportFormat>(ExportFormat::Png8) : outputs[n].format,
            QString("exportDerivedFormat%1").arg(n));
        depth->setEnabled(!usd && !categorical);
        if (categorical)
          depth->setToolTip("Material ID uses lossless RGB PNG with exact palette values.");
        derived_table->setCellWidget(n, 3, depth);
        QObject::connect(depth, &QComboBox::currentIndexChanged, &dialog, [&, n, depth] {
          const int selected = preset_list->currentRow();
          if (selected < 1)
            return;
          custom.at(selected - 1).outputs.at(n).format = combo_format(depth);
          if (derived_changed)
            derived_changed();
        });
        QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog, [&, n, combo] {
          const int selected = preset_list->currentRow();
          if (selected < 1 || n >= int(custom.at(selected - 1).outputs.size()))
            return;
          custom.at(selected - 1).outputs[n].source = ExportMapSource(
              combo->currentData().toInt());
          if (derived_changed)
            derived_changed();
        });
      }
    }
    packing->setVisible(has_packing);
    table->setVisible(!editable || custom.at(row - 1).standard_outputs);
    if (has_packing) {
      const auto &output = *custom.at(row - 1).packed;
      QSignalBlocker name_block(packed_name);
      packed_name->setText(QString::fromStdString(output.name));
      {
        QSignalBlocker depth_block(packed_format);
        packed_format->setCurrentIndex(
            packed_format->findData(output.format ? int(*output.format) : -1));
      }
      for (int c = 0; c < 3; ++c) {
        QSignalBlocker source_block(packed_sources[c]);
        packed_sources[c]->setCurrentIndex(packed_sources[c]->findData(int(output.source[c])));
      }
    }
    preset_name->setReadOnly(!editable);
    preset_name->setText(editable ? custom.at(row - 1).name : preset->itemText(0));
    for (int c = 0; c < 9; ++c) {
      auto *depth = encoding_combo(table,
                                   editable ? custom.at(row - 1).formats[c] : std::nullopt,
                                   QString("exportChannelFormat%1").arg(c));
      depth->setEnabled(editable && !usd);
      table->setCellWidget(c, 2, depth);
      QObject::connect(depth, &QComboBox::currentIndexChanged, &dialog, [&, c, depth] {
        const int selected = preset_list->currentRow();
        if (selected < 1)
          return;
        custom.at(selected - 1).formats[c] = combo_format(depth);
        if (derived_changed)
          derived_changed();
      });
      auto *item = table->item(c, 0);
      const auto pattern = editable ?
                               QString::fromStdString(custom.at(row - 1).naming[c]) :
                               "$textureSet_" + QString::fromLatin1(export_channel_suffixes[c]);
      const bool modern = convention->currentIndex() == 1;
      item->setText(export_convention_pattern(pattern,
                                              QString::fromLatin1(export_channel_suffixes[c]),
                                              QString::fromLatin1(openpbr_channel_suffixes[c]),
                                              modern));
      table->item(c, 1)->setText(modern ? QString::fromLatin1(openpbr_channel_suffixes[c]) :
                                          QString::fromLatin1(channels[c]));
      item->setFlags(editable ? item->flags() | Qt::ItemIsEditable :
                                item->flags() & ~Qt::ItemIsEditable);
    }
  };
  QObject::connect(
      convention, &QComboBox::currentIndexChanged, &dialog, [&] { refresh_editor(); });
  QObject::connect(preset_list, &QListWidget::currentRowChanged, &dialog, refresh_editor);
  QObject::connect(duplicate, &QPushButton::clicked, &dialog, [&] {
    OutputPreset copy;
    const int row = preset_list->currentRow();
    if (row > 0) {
      copy.naming = custom.at(row - 1).naming;
      copy.standard_outputs = custom.at(row - 1).standard_outputs;
      copy.packed = custom.at(row - 1).packed;
      copy.outputs = custom.at(row - 1).outputs;
      copy.kind = custom.at(row - 1).kind;
      copy.formats = custom.at(row - 1).formats;
    }
    else {
      for (int c = 0; c < 9; ++c)
        copy.naming[c] = "$textureSet_" + std::string(export_channel_suffixes[c]);
      set_default_export_formats(copy);
    }
    copy.name = preset_list->currentItem()->text() + "_copy";
    custom.push_back(copy);
    preset->addItem(copy.name, copy.id);
    preset_list->addItem(copy.name);
    preset_list->setCurrentRow(preset_list->count() - 1);
  });
  QObject::connect(new_packed, &QPushButton::clicked, &dialog, [&] {
    OutputPreset entry;
    entry.name = "New packed preset";
    entry.standard_outputs = false;
    entry.packed = PackedOutput{};
    custom.push_back(entry);
    preset->addItem(entry.name, entry.id);
    preset_list->addItem(entry.name);
    preset_list->setCurrentRow(preset_list->count() - 1);
  });
  preset_list->setCurrentRow(0);

  auto *results = new QWidget(tabs);
  auto *results_layout = new QHBoxLayout(results);
  auto *files = new QListWidget(results);
  files->setObjectName("exportFileList");
  results_layout->addWidget(files, 1);
  auto *console = new QPlainTextEdit(results);
  console->setObjectName("exportConsole");
  console->setReadOnly(true);
  results_layout->addWidget(console, 1);
  tabs->addTab(results, "List of Exports");
  auto *progress = new QProgressBar(&dialog);
  progress->setObjectName("exportProgress");
  progress->hide();
  layout->addWidget(progress);
  auto *footer = new QHBoxLayout;
  layout->addLayout(footer);
  auto *count = new QLabel(&dialog);
  count->setObjectName("exportCount");
  footer->addWidget(count, 1);
  auto *cancel = new QPushButton("Cancel", &dialog);
  cancel->setObjectName("cancelTextureExport");
  auto *save = new QPushButton("Save settings", &dialog);
  save->setObjectName("saveExportSettings");
  auto *run = new QPushButton("Export", &dialog);
  run->setObjectName("runTextureExport");
  for (auto *button : {cancel, save, run})
    footer->addWidget(button);
  const auto choice = [&] {
    Choice result;
    result.directory = directory->text();
    result.format = ExportFormat(precision->currentData().toInt());
    result.preset = preset->currentData().toUuid();
    result.size = global_size;
    result.sizes = local_sizes;
    result.padding = ExportPadding(padding->currentData().toInt());
    result.padding_width = padding_width->value();
    result.presets = custom;
    result.mesh_displacement = mesh_displacement->isChecked();
    result.naming_convention = ExportNamingConvention(convention->currentIndex());
    result.openpbr_materialx = materialx->isChecked();
    for (int row = 0; row < sets->count(); ++row)
      if (sets->item(row)->checkState() == Qt::Checked)
        result.selected.insert(sets->item(row)->data(Qt::UserRole).toUuid());
    return result;
  };
  const auto update_files = [&] {
    try {
      ExportRequest request;
      request.mesh = document.settings().absolute_mesh_path;
      request.format = ExportFormat(precision->currentData().toInt());
      request.naming_convention = ExportNamingConvention(convention->currentIndex());
      request.openpbr_materialx = materialx->isChecked();
      request.padding = ExportPadding(padding->currentData().toInt());
      request.padding_width = padding_width->value();
      for (const auto &entry : custom)
        if (entry.id == preset->currentData().toUuid()) {
          apply_export_preset(request, entry);
        }
      const auto selected = choice().selected;
      for (const auto &set : document.texture_sets())
        if (selected.contains(set.id))
          for (int number : set.tiles) {
            ExportTile tile{set,
                            number,
                            set.width(number),
                            {},
                            {},
                            local_sizes.contains(set.id) ? local_sizes.at(set.id) : global_size};
            tile.height = set.rows(number);
            tile.gpu_capture_planned = bool(async_source) &&
                                       (texture_export_has_adjustment(set) ||
                                        texture_export_needs_native_capture(set, number));
            request.tiles.push_back(std::move(tile));
          }
      files->clear();
      for (const auto &file : export_file_list(request))
        files->addItem(QString::fromStdString(file.name) +
                       QString("  %1×%2").arg(file.resolution).arg(file.height));
      const int textures = files->count();
#ifdef BTS_WITH_MATERIALX
      for (const auto &name : openpbr_texture_package_files(request))
        files->addItem(QString::fromStdString(name));
#endif
      const auto *entry = selected_preset();
      const bool usd = (entry && entry->kind == ExportPresetKind::UsdMetalRough) ||
                       (request.openpbr_materialx && bool(mesh_source));
      if (usd) {
        if (!mesh_source)
          throw std::runtime_error("USD export requires the native mesh source");
        const auto stem = export_token_component(
            QFileInfo(QString::fromStdString(request.mesh)).completeBaseName());
        files->addItem(stem + ".usdc  USD scene");
        files->addItem(stem + ".usdz  USD package");
      }
      count->setText(
          QString("%1 textures selected%2").arg(textures).arg(usd ? " (+ USDC and USDZ)" : ""));
      run->setEnabled(files->count() > 0);
    }
    catch (const std::exception &error) {
      files->clear();
      count->setText(QString::fromUtf8(error.what()));
      run->setEnabled(false);
    }
  };
  derived_changed = update_files;
  QObject::connect(
      derived_table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem *item) {
        const int row = preset_list->currentRow();
        if (row < 1 || item->row() >= int(custom.at(row - 1).outputs.size()))
          return;
        auto &output = custom.at(row - 1).outputs[item->row()];
        if (item->column() == 0)
          output.name = canonical_export_pattern(item->text()).toStdString();
        if (item->column() == 2)
          output.alpha = item->checkState() == Qt::Checked;
        update_files();
      });
  QObject::connect(sources, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem *item) {
    const int row = preset_list->currentRow();
    if (row < 1 || !(item->flags() & Qt::ItemIsEnabled))
      return;
    const int source = item->data(Qt::UserRole).toInt();
    custom.at(row - 1).outputs.push_back(
        {"$mesh_$textureSet_" + std::string(export_map_suffixes[source]),
         ExportMapSource(source),
         false});
    refresh_editor();
    update_files();
  });
  QObject::connect(remove_derived, &QPushButton::clicked, &dialog, [&] {
    const int row = preset_list->currentRow(), output = derived_table->currentRow();
    if (row < 1 || output < 0 || output >= int(custom.at(row - 1).outputs.size()))
      return;
    auto &maps = custom.at(row - 1).outputs;
    maps.erase(maps.begin() + output);
    refresh_editor();
    update_files();
  });
  const auto update_packing = [&] {
    const int row = preset_list->currentRow();
    if (row < 1 || !custom.at(row - 1).packed)
      return;
    auto &output = *custom.at(row - 1).packed;
    output.name = packed_name->text().toStdString();
    output.format = combo_format(packed_format);
    for (int c = 0; c < 3; ++c) {
      output.source[c] = PackedSource(packed_sources[c]->currentData().toInt());
      packed_sources[c]->setStyleSheet(QString("QComboBox { border-bottom: 2px solid %1; }")
                                           .arg(std::array<const char *, 8>{bts::theme::Success,
                                                                            bts::theme::AxisZ,
                                                                            bts::theme::Warning,
                                                                            bts::theme::PlotPurple,
                                                                            bts::theme::PlotPurple,
                                                                            bts::theme::Disabled,
                                                                            bts::theme::Text,
                                                                            bts::theme::PlotPurple}
                                                    .at(int(output.source[c]))));
    }
    update_files();
  };
  QObject::connect(packed_format, &QComboBox::currentIndexChanged, &dialog, update_packing);
  QObject::connect(packed_name, &QLineEdit::textChanged, &dialog, update_packing);
  for (auto *combo : packed_sources)
    QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog, update_packing);
  QObject::connect(global, &QPushButton::clicked, &dialog, [&] {
    sets->setCurrentRow(-1);
    editing_set = QUuid();
    refresh_size();
  });
  QObject::connect(sets, &QListWidget::currentRowChanged, &dialog, [&](int row) {
    editing_set = row < 0 ? QUuid{} : sets->item(row)->data(Qt::UserRole).toUuid();
    refresh_size();
  });
  QObject::connect(size, &QComboBox::currentIndexChanged, &dialog, [&] {
    const int value = size->currentData().toInt();
    if (editing_set.isNull())
      global_size = value;
    else
      local_sizes[editing_set] = value ? value : int(document.settings().resolution);
    refresh_size();
    update_files();
  });
  QObject::connect(size_override, &QCheckBox::toggled, &dialog, [&](bool enabled) {
    if (editing_set.isNull())
      return;
    if (enabled)
      local_sizes[editing_set] = global_size ? global_size : int(document.settings().resolution);
    else
      local_sizes.erase(editing_set);
    refresh_size();
    update_files();
  });
  QObject::connect(padding, &QComboBox::currentIndexChanged, &dialog, update_files);
  QObject::connect(padding_width, &QSpinBox::valueChanged, &dialog, update_files);
  QObject::connect(preset, &QComboBox::currentIndexChanged, &dialog, update_files);
  QObject::connect(convention, &QComboBox::currentIndexChanged, &dialog, update_files);
  QObject::connect(materialx, &QCheckBox::toggled, &dialog, update_files);
  QObject::connect(preset_name, &QLineEdit::editingFinished, &dialog, [&] {
    const int row = preset_list->currentRow();
    if (row < 1)
      return;
    auto &entry = custom.at(row - 1);
    const auto name = preset_name->text().trimmed();
    if (name.isEmpty()) {
      refresh_editor();
      return;
    }
    entry.name = name;
    preset_list->item(row)->setText(name);
    preset->setItemText(preset->findData(entry.id), name);
  });
  QObject::connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem *item) {
    const int row = preset_list->currentRow();
    if (row < 1 || item->column() != 0)
      return;
    custom.at(row - 1).naming[item->row()] = item->text().toStdString();
    update_files();
  });
  QObject::connect(sets, &QListWidget::itemChanged, &dialog, update_files);
  QObject::connect(precision, &QComboBox::currentIndexChanged, &dialog, update_files);
  update_files();
  QObject::connect(save, &QPushButton::clicked, &dialog, [&] {
    try {
      document.set_export_configuration(export_configuration_json(choice()));
      dialog.accept();
    }
    catch (const std::exception &error) {
      console->appendPlainText("Settings failed: " + QString::fromUtf8(error.what()));
      tabs->setCurrentWidget(results);
    }
  });
  auto cancellation = std::make_shared<std::atomic_bool>(false);
  auto &cancelled = *cancellation;
  std::atomic_bool done = false;
  std::mutex lock;
  std::vector<std::string> messages;
  std::string error;
  int completed = 0;
  bool running = false;
  std::shared_ptr<NativeMeshExport> mesh_job;
  std::jthread worker;
  QTimer poll;
  QObject::connect(tabs, &QTabWidget::currentChanged, &dialog, [&](int index) {
    save->setEnabled(!running && index != 1);
  });
  dialog.may_close = [&] {
    if (!running)
      return true;
    cancelled = true;
    cancel->setEnabled(false);
    return false;
  };
  QObject::connect(cancel, &QPushButton::clicked, &dialog, [&] { dialog.reject(); });
  QObject::connect(&poll, &QTimer::timeout, &dialog, [&] {
    std::vector<std::string> lines;
    int current;
    {
      std::lock_guard guard(lock);
      lines.swap(messages);
      current = completed;
    }
    for (const auto &line : lines)
      console->appendPlainText(QString::fromStdString(line));
    progress->setValue(current);
    if (!done)
      return;
    if (worker.joinable())
      worker.join();
    mesh_job.reset();
    running = false;
    poll.stop();
    settings->setEnabled(true);
    run->setEnabled(true);
    save->setEnabled(true);
    cancel->setText("Close");
    cancel->setEnabled(true);
    console->appendPlainText(!error.empty() ? "Export failed: " + QString::fromStdString(error) :
                             cancelled      ? "Export cancelled; completed files retained." :
                                              "Export successfully finished");
    dialog.setProperty("exportFinished", true);
    dialog.setProperty("exportError", QString::fromStdString(error));
  });
  QObject::connect(run, &QPushButton::clicked, &dialog, [&] {
    try {
      const auto selected = choice();
      ExportRequest request;
      request.directory = selected.directory.toStdString();
      request.format = selected.format;
      request.naming_convention = selected.naming_convention;
      request.openpbr_materialx = selected.openpbr_materialx;
      request.mesh = document.settings().absolute_mesh_path;
      request.padding = ExportPadding(padding->currentData().toInt());
      request.padding_width = padding_width->value();
      for (const auto &entry : custom)
        if (entry.id == selected.preset)
          apply_export_preset(request, entry);
      if (!QDir(selected.directory).isAbsolute() || !QDir(selected.directory).exists())
        throw std::invalid_argument("Choose an existing absolute output directory");
      for (const auto &set : document.texture_sets())
        if (selected.selected.contains(set.id))
          for (int number : set.tiles) {
            ExportTile tile{set, number, set.width(number)};
            tile.height = set.rows(number);
            tile.output_resolution = selected.sizes.contains(set.id) ? selected.sizes.at(set.id) :
                                                                       selected.size;
            tile.gpu_capture_planned = bool(async_source) &&
                                       (texture_export_has_adjustment(set) ||
                                        texture_export_needs_native_capture(set, number));
            request.tiles.push_back(std::move(tile));
          }
      // Names/configuration are checked from metadata before any GPU capture.
      const auto planned_files = export_file_list(request);
      if (request.tiles.empty() || planned_files.empty())
        throw std::invalid_argument("Select at least one Texture Set and output");
      const bool needs_gpu = std::any_of(
          request.tiles.begin(), request.tiles.end(), [](const auto &tile) {
            return tile.gpu_capture_planned;
          });
      const auto *entry = selected_preset();
      const bool openpbr_usd = selected.openpbr_materialx && bool(mesh_source);
      const bool usd = (entry && entry->kind == ExportPresetKind::UsdMetalRough) || openpbr_usd;
      mesh_job = usd ? mesh_source(!openpbr_usd && selected.mesh_displacement &&
                                   displacement_available) :
                       nullptr;
      document.set_export_configuration(export_configuration_json(selected));
      FileDialog::remember_folder(selected.directory);
      cancelled = false;
      done = false;
      error.clear();
      completed = 0;
      messages.clear();
      running = true;
      dialog.setProperty("exportFinished", false);
      dialog.setProperty("exportError", QString{});
      run->setEnabled(false);
      save->setEnabled(false);
      settings->setEnabled(false);
      cancel->setText("Cancel Export");
      cancel->setEnabled(true);
      console->clear();
      console->appendPlainText(needs_gpu ? "Capturing GPU material channels…" :
                                           "Preparing material channels…");
      tabs->setCurrentWidget(results);
      progress->setRange(0, int(planned_files.size()) + (usd ? 2 : 0));
      progress->setValue(0);
      progress->show();
      poll.start(100);
      const QPointer<ExportDialog> guard(&dialog);
      const auto delivered = std::make_shared<bool>(false);
      auto failure = [&, guard, delivered](std::string message) {
        if (!guard || *delivered)
          return;
        *delivered = true;
        error = std::move(message);
        done = true;
      };
      auto complete = [&, guard, delivered, planned = request, usd, openpbr_usd](
                          std::vector<ExportTile> captured) mutable {
        if (!guard || *delivered)
          return;
        *delivered = true;
        try {
          if (cancelled) {
            done = true;
            return;
          }
          if (captured.size() != planned.tiles.size())
            throw std::invalid_argument("Export capture returned a different tile set");
          std::set<std::pair<QUuid, int>> seen;
          for (auto &tile : captured) {
            const auto expected = std::find_if(
                planned.tiles.begin(), planned.tiles.end(), [&](const auto &v) {
                  return v.set.id == tile.set.id && v.number == tile.number;
                });
            if (expected == planned.tiles.end() ||
                !seen.emplace(tile.set.id, tile.number).second ||
                tile.resolution != expected->resolution || tile.rows() != expected->rows())
              throw std::invalid_argument("Export capture identity or dimensions changed");
            tile.output_resolution = expected->output_resolution;
            tile.gpu_capture_planned = expected->gpu_capture_planned;
          }
          planned.tiles = std::move(captured);
          worker = std::jthread(
              [&, request = std::move(planned), job = mesh_job, usd, openpbr_usd] {
                try {
                  write_texture_export(
                      request, cancelled, [&](int number, const std::string &name) {
                        std::lock_guard guard(lock);
                        completed = number;
                        messages.push_back("Written " + name);
                      });
                  if (usd && !cancelled) {
#ifdef BTS_WITH_MATERIALX
                    if (openpbr_usd) {
                      job->attach_usd_textures(openpbr_usd_preview_textures(request));
                      job->attach_usd_material_libraries(openpbr_usd_material_libraries(request));
                    }
                    else
#endif
                    {
                      const auto bindings = usd_texture_exports(request);
                      job->attach_usd_textures(bindings);
                    }
                    const QDir output(QString::fromStdString(request.directory));
                    const auto stem = export_token_component(
                        QFileInfo(QString::fromStdString(request.mesh)).completeBaseName());
                    const auto layer = output.filePath(stem + ".usdc"),
                               package = output.filePath(stem + ".usdz");
                    job->write(layer.toStdString(), false);
                    {
                      std::lock_guard guard(lock);
                      ++completed;
                      messages.push_back("Written " + layer.toStdString());
                    }
                    if (!cancelled) {
#ifdef BTS_WITH_MATERIALX
                      if (openpbr_usd) {
                        QStringList manifest;
                        for (const auto &file : export_file_list(request))
                          manifest.append(QString::fromStdString(file.name));
                        for (const auto &file : openpbr_texture_package_files(request))
                          manifest.append(QString::fromStdString(file));
                        package_openpbr_usd_files(layer, package, manifest, cancelled);
                      }
                      else
#endif
                        package_native_usd(layer.toStdString(), package.toStdString());
                      std::lock_guard guard(lock);
                      ++completed;
                      messages.push_back("Written " + package.toStdString());
                    }
                  }
                }
                catch (const std::exception &failure) {
                  error = failure.what();
                }
                done = true;
              });
          poll.start(100);
        }
        catch (const std::exception &failure) {
          error = failure.what();
          done = true;
        }
      };
      if (needs_gpu)
        async_source(request, cancellation, std::move(complete), std::move(failure));
      else {
        std::vector<QUuid> ids(selected.selected.begin(), selected.selected.end());
        complete(source(ids));
      }
    }
    catch (const std::exception &failure) {
      console->appendPlainText("Export failed: " + QString::fromUtf8(failure.what()));
      running = false;
      poll.stop();
      mesh_job.reset();
      settings->setEnabled(true);
      run->setEnabled(true);
      save->setEnabled(true);
      cancel->setEnabled(true);
      cancel->setText("Close");
      dialog.setProperty("exportError", QString::fromUtf8(failure.what()));
      dialog.setProperty("exportFinished", true);
    }
  });
  dialog.exec();
  return document.export_configuration() != original;
}
}  // namespace bts
