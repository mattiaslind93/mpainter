// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include "TextureExtentChoices.hh"
#include "BakeSourceControls.hh"
#include "NumericLocale.hh"
#include "SkewPanel.hh"
#include "BakeQuality.hh"
#include "Performance.hh"
#include "MeshMaps.hh"
#include <QToolButton>
#include <QComboBox>
#include <QGridLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include "FileDialogs.hh"
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
namespace bts {
SkewPanel::SkewPanel(QWidget *parent) : QWidget(parent)
{
  setObjectName("skewPanel");
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(14, 12, 14, 12);
  root->setSpacing(10);
  auto *header = new QHBoxLayout;
  auto *title = new QLabel("BAKING", this);
  title->setObjectName("bakingHeading");
  title->setStyleSheet("font-size:16px;font-weight:600;");
  header->addWidget(title);
  header->addStretch();
  auto *config = new QPushButton("Advanced setup…", this);
  config->setObjectName("configureSkewBaking");
  config->setToolTip("Configure texture-set/tile selection and advanced mesh matching");
  header->addWidget(config);
  root->addLayout(header);
  connect(config, &QPushButton::clicked, this, [this] {
    if (configure)
      configure();
  });
  context_ = new QLabel(this);
  context_->setObjectName("bakingTextureSet");
  context_->setMinimumWidth(0);
  context_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  root->addWidget(context_);
  auto *body = new QHBoxLayout;
  body->setSpacing(16);
  root->addLayout(body, 1);
  auto *map_column = new QWidget(this);
  map_column->setFixedWidth(318);
  auto *map_layout = new QVBoxLayout(map_column);
  map_layout->setContentsMargins(0, 0, 0, 0);
  body->addWidget(map_column);
  auto *scroll = new QScrollArea(this);
  scroll->setObjectName("bakingSettingsScroll");
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *details = new QWidget(scroll);
  auto *layout = new QVBoxLayout(details);
  layout->setContentsMargins(0, 0, 8, 0);
  layout->setSpacing(8);
  scroll->setWidget(details);
  body->addWidget(scroll, 1);
  auto *footer = new QHBoxLayout;
  auto *back = new QPushButton("Return to painting mode", this);
  back->setObjectName("leaveBakingMode");
  footer->addWidget(back);
  selected_count_ = new QLabel(this);
  selected_count_->setObjectName("bakingSelectedCount");
  footer->addWidget(selected_count_);
  footer->addStretch();
  bake_selected_ = new QPushButton("Bake selected", this);
  bake_selected_->setObjectName("bakeSelectedMaps");
  bake_selected_->setStyleSheet(bts::theme::qss(
      "QPushButton{background:@Selection@;color:@Heading@;padding:7px 18px;}QPushButton:disabled{background:@Hover@;color:@Muted@;}"));
  footer->addWidget(bake_selected_);
  root->addLayout(footer);
  connect(back, &QPushButton::clicked, this, [this] {
    if (paint_mode)
      paint_mode();
  });
  connect(bake_selected_, &QPushButton::clicked, this, [this] {
    if (bake_selected)
      bake_selected();
  });
  const auto section = [&](QGroupBox *content, const QString &label, const char *name, bool open) {
    auto *header = new QToolButton(details);
    header->setObjectName(name);
    header->setText(label);
    header->setCheckable(true);
    header->setChecked(open);
    header->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header->setStyleSheet(bts::theme::qss(
        "QToolButton{text-align:left;padding:6px 3px;font-weight:600;border:0;border-bottom:1px solid @Border@;}"));
    content->setTitle({});
    content->setStyleSheet("QGroupBox{border:0;margin:0;padding:4px 0;}");
    layout->addWidget(header);
    layout->addWidget(content);
    content->setVisible(open);
    connect(header, &QToolButton::toggled, content, [content, header](bool on) {
      content->setVisible(on);
      header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    return header;
  };

  auto *maps = new QGroupBox("Mesh maps", map_column);
  maps->setObjectName("bakingMapList");
  auto *grid = new QGridLayout(maps);
  grid->setHorizontalSpacing(3);
  for (int role = 0; role < 7; ++role) {
    auto *name = map_selected_[role] = new QCheckBox(mesh_map_names[role], maps);
    name->setObjectName(QString("bakeSelectedMap%1").arg(role));
    name->setChecked(true);
    name->setMinimumWidth(0);
    name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    name->setToolTip("Include this map in Bake selected; select its quality settings");
    grid->addWidget(name, role, 0);
    const auto button =
        [&](const char *prefix, const char *text, const QString &tip, int column, bool checkable) {
          auto *w = new QToolButton(maps);
          w->setObjectName(QString(prefix) + QString::number(role));
          w->setText(text);
          w->setToolTip(tip);
          w->setCheckable(checkable);
          w->setFixedWidth(26);
          grid->addWidget(w, role, column);
          return w;
        };
    map_preview_[role] = button("bakePreview", "◉", "Preview this mesh map", 1, true);
    map_bake_[role] = button(
        "bakeNow", "↻", "Bake only this map for the current Texture Set", 2, false);
    map_auto_[role] = button(
        "bakeAuto", "A", "Automatically rebake this map; only one map can be active", 3, true);
    map_sync_[role] = button(
        "bakeSync", "⇆", "Copy this map's quality settings to all active Texture Sets", 4, false);
    connect(name, &QCheckBox::clicked, this, [this, role] {
      quality_role_->setCurrentIndex(role);
      emit_recipe();
    });
    connect(map_preview_[role], &QToolButton::clicked, this, [this, role](bool on) {
      if (!updating_ && preview_map) {
        quality_role_->setCurrentIndex(role);
        preview_map(on ? role : -1);
      }
    });
    connect(map_bake_[role], &QToolButton::clicked, this, [this, role] {
      if (!updating_ && bake_map) {
        quality_role_->setCurrentIndex(role);
        bake_map(role);
      }
    });
    connect(map_auto_[role], &QToolButton::clicked, this, [this, role](bool on) {
      if (!updating_ && auto_role_changed) {
        quality_role_->setCurrentIndex(role);
        auto_role_changed(on ? role : -1);
      }
    });
    connect(map_sync_[role], &QToolButton::clicked, this, [this, role] {
      if (!updating_ && sync_map) {
        quality_role_->setCurrentIndex(role);
        sync_map(role);
      }
    });
  }
  map_layout->addWidget(maps);
  map_layout->addStretch();
  const auto check = [&](const char *name, const char *label, QFormLayout *target) {
    auto *w = new QCheckBox(label, this);
    w->setObjectName(name);
    target->addRow(w);
    return w;
  };
  const auto spin = [&](const char *name,
                        double low,
                        double high,
                        int decimals,
                        QFormLayout *target,
                        const char *label) {
    auto *w = new bts::NumericSpinBox(this);
    w->setObjectName(name);
    w->setLocale(QLocale::c());
    w->setRange(low, high);
    w->setDecimals(decimals);
    w->setSingleStep(decimals > 3 ? .001 : .01);
    target->addRow(label, w);
    return w;
  };
  auto *common = new QGroupBox(details);
  common->setObjectName("bakingCommonParameters");
  auto *form = new QFormLayout(common);
  project_ = check("skewUseProjectMesh", "Use Low Poly Mesh as High Poly Mesh", form);
  auto *file_row = new QHBoxLayout;
  high_ = new QLineEdit(common);
  high_->setObjectName("skewHighFiles");
  auto *choose = new QPushButton("Add mesh…", common);
  choose->setObjectName("chooseSkewHighFiles");
  choose->setMaximumWidth(30);
  file_row->addWidget(high_);
  file_row->addWidget(choose);
  form->addRow("High-poly scene", file_row);
  connect(high_, &QLineEdit::editingFinished, this, &SkewPanel::emit_recipe);
  connect(choose, &QPushButton::clicked, this, [this] {
    const auto paths = bts::FileDialog::getOpenFileNames(
        this, "High poly scene", {}, bake_mesh_filter, nullptr, QFileDialog::DontUseNativeDialog);
    if (paths.isEmpty())
      return;
    high_->setText(paths.join(";"));
    project_->setChecked(false);
    emit_recipe();
  });
  source_controls_ = new BakeSourceControls(common, "skew", {});
  form->addRow(source_controls_);
  source_controls_->changed = [this] { emit_recipe(); };
  width_ = new TextureExtentComboBox(32, common);
  height_ = new TextureExtentComboBox(32, common);
  width_->setObjectName("autoBakeWidth");
  height_->setObjectName("autoBakeHeight");
  auto *dimensions = new QHBoxLayout;
  dimensions->addWidget(width_);
  dimensions->addWidget(new QLabel("×", common));
  dimensions->addWidget(height_);
  form->addRow("Output size", dimensions);
  locked_ = check("autoBakeSizeLocked", "Link dimensions", form);
  ignore_backface_ = check("bakeIgnoreBackface", "Ignore Backface", form);
  ignore_backface_->setToolTip(
      "Reject backfaces on marked objects; an empty suffix in Advanced setup applies to all objects");
  connect(ignore_backface_, &QCheckBox::toggled, this, [this] { emit_recipe(); });
  quality_preset_ = new QComboBox(common);
  quality_preset_->setObjectName("bakeQualityPreset");
  quality_preset_->addItems({"Draft", "Final", "Custom"});
  form->addRow("Quality preset", quality_preset_);
  denoise_ = check("bakeDenoise", "Denoise AO / Thickness (OIDN)", form);
  connect(denoise_, &QCheckBox::toggled, this, [this] { emit_recipe(); });
  connect(quality_preset_, &QComboBox::activated, this, [this](int index) {
    if (updating_ || index == int(BakePreset::Custom))
      return;
    int rays = common_rays_->value(), sampling = common_samples_->currentData().toInt();
    bool denoise = denoise_->isChecked();
    apply_bake_preset(BakePreset(index), rays, sampling, denoise);
    // A common preset also replaces existing per-map ray/AA overrides.
    auto sets = recipe_["map_options"].toObject();
    for (auto sit = sets.begin(); sit != sets.end(); ++sit) {
      auto maps = sit.value().toObject();
      for (auto mit = maps.begin(); mit != maps.end(); ++mit) {
        auto q = mit.value().toObject();
        q["rays"] = rays;
        q["sampling"] = sampling;
        mit.value() = q;
      }
      sit.value() = maps;
    }
    if (recipe_.contains("map_options"))
      recipe_["map_options"] = sets;
    recipe_["sampling"] = sampling;
    updating_ = true;
    common_rays_->setValue(rays);
    common_samples_->setCurrentIndex(common_samples_->findData(sampling));
    denoise_->setChecked(denoise);
    updating_ = false;
    emit_recipe();
  });
  common_samples_ = new QComboBox(common);
  common_samples_->setObjectName("bakeCommonSampling");
  common_samples_->addItem("1x1 (None)", 1);
  for (int n : {2, 4, 8})
    common_samples_->addItem(QString("%1x%1").arg(n), n);
  form->addRow("Subsampling", common_samples_);
  common_rays_ = new QSpinBox(common);
  common_rays_->setObjectName("bakeCommonRays");
  common_rays_->setRange(1, 512);
  common_rays_->setValue(64);
  common_rays_preset_ = new QComboBox(common);
  common_rays_preset_->setObjectName("bakeCommonRaysPreset");
  for (int p : {16, 32, 64, 128, 256, 512})
    common_rays_preset_->addItem(QString::number(p), p);
  common_rays_preset_->addItem("Custom", -1);
  common_rays_preset_->setCurrentIndex(common_rays_preset_->findData(64));
  auto *common_rays_row = new QHBoxLayout;
  common_rays_row->addWidget(common_rays_);
  common_rays_row->addWidget(common_rays_preset_);
  form->addRow("Baking samples", common_rays_row);
  connect(common_rays_preset_, &QComboBox::currentIndexChanged, this, [this] {
    if (updating_)
      return;
    const int v = common_rays_preset_->currentData().toInt();
    if (v > 0 && common_rays_->value() != v)
      common_rays_->setValue(v);
  });
  connect(common_rays_, &QSpinBox::valueChanged, this, [this](int v) {
    const int idx = common_rays_preset_->findData(v);
    const int target = idx >= 0 ? idx : common_rays_preset_->findData(-1);
    if (common_rays_preset_->currentIndex() != target) {
      const bool p = updating_;
      updating_ = true;
      common_rays_preset_->setCurrentIndex(target);
      updating_ = p;
    }
    if (updating_)
      return;
    recipe_["rays"] = v;
    if (recipe_.contains("map_options")) {
      auto sets = recipe_["map_options"].toObject();
      for (auto sit = sets.begin(); sit != sets.end(); ++sit) {
        auto maps = sit.value().toObject();
        for (auto mit = maps.begin(); mit != maps.end(); ++mit) {
          auto q = mit.value().toObject();
          if (q.contains("rays")) {
            q["rays"] = v;
            mit.value() = q;
          }
        }
        sit.value() = maps;
      }
      recipe_["map_options"] = sets;
    }
    updating_ = true;
    rays_->setValue(v);
    updating_ = false;
    emit_recipe();
  });
  connect(common_samples_, &QComboBox::currentIndexChanged, this, [this] {
    if (updating_)
      return;
    const int s = common_samples_->currentData().toInt();
    recipe_["sampling"] = s;
    if (recipe_.contains("map_options")) {
      auto sets = recipe_["map_options"].toObject();
      for (auto sit = sets.begin(); sit != sets.end(); ++sit) {
        auto maps = sit.value().toObject();
        for (auto mit = maps.begin(); mit != maps.end(); ++mit) {
          auto q = mit.value().toObject();
          if (q.contains("sampling")) {
            q["sampling"] = s;
            mit.value() = q;
          }
        }
        sit.value() = maps;
      }
      recipe_["map_options"] = sets;
    }
    updating_ = true;
    samples_->setCurrentIndex(samples_->findData(s));
    updating_ = false;
    emit_recipe();
  });
  connect(locked_, &QCheckBox::toggled, this, [this](bool on) {
    if (updating_)
      return;
    if (on) {
      updating_ = true;
      refresh_texture_extent_choices(*height_, 32, width_->currentData().toInt());
      updating_ = false;
    }
    emit_recipe();
  });
  connect(width_, &QComboBox::currentIndexChanged, this, [this] {
    if (updating_)
      return;
    if (locked_->isChecked()) {
      updating_ = true;
      refresh_texture_extent_choices(*height_, 32, width_->currentData().toInt());
      updating_ = false;
    }
    emit_recipe();
  });
  connect(height_, &QComboBox::currentIndexChanged, this, [this] { emit_recipe(); });
  section(common, "Common Parameters", "bakingCommonSection", true);
  auto *cage_group = new QGroupBox(details);
  auto *cage_form = new QFormLayout(cage_group);
  cage_ = check("skewUseCage", "Use custom cage", cage_form);
  cage_->setToolTip("Custom cages disable painted skew correction");
  cage_path_ = new QLineEdit(cage_group);
  cage_path_->setObjectName("skewCageFile");
  auto *cage_row = new QHBoxLayout;
  cage_row->addWidget(cage_path_);
  auto *browse_cage = new QPushButton("…", cage_group);
  browse_cage->setObjectName("chooseSkewCageFile");
  browse_cage->setMaximumWidth(30);
  cage_row->addWidget(browse_cage);
  cage_form->addRow("Cage mesh file", cage_row);
  connect(cage_path_, &QLineEdit::editingFinished, this, &SkewPanel::emit_recipe);
  connect(browse_cage, &QPushButton::clicked, this, [this] {
    const auto path = bts::FileDialog::getOpenFileName(this, "Cage mesh", {}, bake_mesh_filter);
    if (!path.isEmpty()) {
      cage_path_->setText(path);
      emit_recipe();
    }
  });
  front_ = spin("skewFrontalDistance", 0, 10000, 5, cage_form, "Frontal distance");
  rear_ = spin("skewRearDistance", 0, 10000, 5, cage_form, "Rear distance");
  average_ = check("skewAverageNormals", "Average normals", cage_form);
  relative_ = check("skewRelativeDistances", "Distances relative to mesh bounds", cage_form);
  for (auto *w : {project_, cage_, average_, relative_})
    connect(w, &QCheckBox::toggled, this, [this] { emit_recipe(); });
  for (auto *w : {front_, rear_})
    connect(w, &QDoubleSpinBox::valueChanged, this, [this] { emit_recipe(); });
  section(cage_group, "Cage", "bakingCageSection", false);
  auto *quality = new QGroupBox("Mesh map quality — current Texture Set", this);
  auto *qf = new QFormLayout(quality);
  quality_role_ = new QComboBox(quality);
  quality_role_->setObjectName("bakeQualityRole");
  for (const auto *name : mesh_map_names)
    quality_role_->addItem(name);
  qf->addRow("Map", quality_role_);
  dilation_ = new QSpinBox(quality);
  dilation_->setObjectName("bakeMapDilation");
  dilation_->setRange(0, 64);
  qf->addRow("Dilation", dilation_);
  rays_ = new QSpinBox(quality);
  rays_->setObjectName("bakeMapRays");
  rays_->setRange(1, 512);
  qf->addRow("AO / thickness rays", rays_);
  samples_ = new QComboBox(quality);
  samples_->setObjectName("bakeMapSampling");
  for (int n : {1, 2, 4, 8})
    samples_->addItem(QString::number(n) + " ×", n);
  qf->addRow("Anti-aliasing", samples_);
  diffusion_ = new QCheckBox("Dilation diffusion", quality);
  diffusion_->setObjectName("bakeMapDiffusion");
  qf->addRow(diffusion_);
  section(quality, "Mesh map settings", "bakingQualitySection", true);
  connect(quality_role_, &QComboBox::currentIndexChanged, this, [this] {
    if (!updating_)
      refresh_quality();
  });
  for (auto *w : {dilation_, rays_})
    connect(w, &QSpinBox::valueChanged, this, [this] { emit_quality(); });
  connect(samples_, &QComboBox::currentIndexChanged, this, [this] { emit_quality(); });
  connect(diffusion_, &QCheckBox::toggled, this, [this] { emit_quality(); });
  auto *correction = new QGroupBox("Skew correction", this);
  auto *fields = new QFormLayout(correction);
  reason_ = new QLabel(this);
  reason_->setObjectName("skewEligibility");
  reason_->setWordWrap(true);
  fields->addRow(reason_);
  paint_ = new QPushButton("Paint skew correction", this);
  paint_->setObjectName("paintSkewCorrection");
  paint_->setCheckable(true);
  fields->addRow(paint_);
  connect(paint_, &QPushButton::clicked, this, [this] {
    if (paint_skew)
      paint_skew();
  });
  enabled_ = check("skewEnabled", "Enable correction", fields);
  value_ = spin("skewPaintValue", 0, 1, 3, fields, "Paint value (X to invert)");
  value_->setValue(1);
  connect(value_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
    if (!updating_ && value_changed)
      value_changed(float(v));
  });
  edge_ = check("skewEdgeProtection", "Edge protection (geometry hard edges)", fields);
  distance_ = spin("skewEdgeDistance", 0, 1, 6, fields, "Edge distance / mesh size");
  contrast_ = spin("skewEdgeContrast", -1, 1, 3, fields, "Edge contrast");
  skew_section_ = section(correction, "Skew correction", "bakingSkewSection", false);
  auto *visual = new QGroupBox("Skew vectors", this);
  auto *vf = new QFormLayout(visual);
  vectors_ = check("skewVectors", "Show vectors", vf);
  length_ = spin("skewVectorLength", .00001, 10, 5, vf, "Length / mesh size");
  density_ = new QSpinBox(visual);
  density_->setObjectName("skewVectorDensity");
  density_->setRange(1, 128);
  vf->addRow("UV density", density_);
  opacity_ = spin("skewVectorOpacity", 0, 1, 3, vf, "Opacity");
  auto *legend = new QLabel("Red: averaged normal · Green: face normal", visual);
  legend->setWordWrap(true);
  vf->addRow(legend);
  section(visual, "Skew vectors", "bakingVectorsSection", false);
  for (auto *w : {enabled_, edge_, vectors_})
    connect(w, &QCheckBox::toggled, this, [this] { emit_settings(); });
  for (auto *w : {distance_, contrast_, length_, opacity_})
    connect(w, &QDoubleSpinBox::valueChanged, this, [this] { emit_settings(); });
  connect(density_, &QSpinBox::valueChanged, this, [this] { emit_settings(); });
  automatic_ = new QCheckBox("Auto-rebake Normal", this);
  automatic_->setObjectName("skewAutoRebake");
  automatic_->hide();
  connect(automatic_, &QCheckBox::toggled, this, [this](bool v) {
    if (!updating_ && auto_changed)
      auto_changed(v);
  });
  rebake_ = new QPushButton("Rebake Normal", this);
  rebake_->setObjectName("rebakeSkewNormal");
  fields->addRow(rebake_);
  connect(rebake_, &QPushButton::clicked, this, [this] {
    if (rebake)
      rebake();
  });
  status_ = new QLabel("Ready", this);
  status_->setObjectName("skewBakeStatus");
  status_->setWordWrap(true);
  status_->setMinimumWidth(0);
  status_->setMaximumHeight(44);
  root->insertWidget(root->count() - 1, status_);
  layout->addStretch();
  for (auto *form : details->findChildren<QFormLayout *>()) {
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setVerticalSpacing(7);
    for (int row = 0; row < form->rowCount(); ++row)
      if (auto *item = form->itemAt(row, QFormLayout::LabelRole); item && item->widget())
        item->widget()->setFixedWidth(170);
  }
  for (auto *button : findChildren<QPushButton *>())
    button->setAutoDefault(false);
  for (auto *box : details->findChildren<QComboBox *>()) {
    box->setMinimumWidth(0);
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  }
}
void SkewPanel::emit_recipe()
{
  if (updating_)
    return;
  auto r = recipe_;
  store_bake_source_options(r, source_controls_->options());
  QJsonArray paths;
  for (const auto &path : high_->text().split(';', Qt::SkipEmptyParts))
    paths.append(path.trimmed());
  r["high_files"] = paths;
  QJsonArray selected;
  for (auto *map : map_selected_)
    selected.append(map->isChecked());
  r["enabled"] = selected;
  r["ignore_backface"] = ignore_backface_->isChecked();
  r["denoise"] = denoise_->isChecked();
  r["project_mesh"] = project_->isChecked();
  r["use_cage"] = cage_->isChecked();
  r["average"] = average_->isChecked();
  r["relative"] = relative_->isChecked();
  r["cage_file"] = cage_path_->text().trimmed();
  r["resolution"] = width_->currentData().toInt();
  r["height"] = height_->currentData().toInt();
  r["locked"] = locked_->isChecked();
  r["rays"] = common_rays_->value();
  if (r.contains("sampling") || common_samples_->currentData().toInt() != 1)
    r["sampling"] = common_samples_->currentData().toInt();
  r["frontal"] = front_->value();
  r["rear"] = rear_->value();
  if (recipe_changed)
    recipe_changed(r);
}
void SkewPanel::emit_settings()
{
  if (updating_)
    return;
  auto s = settings_;
  s.enabled = enabled_->isChecked();
  s.edge_protection = edge_->isChecked();
  s.edge_distance = distance_->value();
  s.edge_contrast = contrast_->value();
  s.vectors = vectors_->isChecked();
  s.vector_length = length_->value();
  s.vector_density = density_->value();
  s.vector_opacity = opacity_->value();
  if (settings_changed)
    settings_changed(s);
}
void SkewPanel::synchronize(const QJsonObject &recipe,
                            const SkewSettings &s,
                            bool editing,
                            bool auto_normal,
                            QUuid set,
                            int auto_role,
                            int preview,
                            int set_count,
                            const QString &set_name)
{
  performance::Scope timing("bake_panel_sync");
  updating_ = true;
  ignore_backface_->setChecked(recipe["ignore_backface"].toBool(false));
  denoise_->setChecked(recipe["denoise"].toBool(true));
  source_controls_->synchronize(recipe);
  context_->setText("Current Texture Set: " + set_name);
  context_->setToolTip(context_->text());
  if (editing && !previous_editing_)
    skew_section_->setChecked(true);
  previous_editing_ = editing;
  recipe_ = recipe;
  settings_ = s;
  set_ = set;
  QStringList paths;
  for (const auto &p : recipe["high_files"].toArray())
    paths.append(p.toString());
  if (!high_->hasFocus())
    high_->setText(paths.join(';'));
  project_->setChecked(recipe["project_mesh"].toBool(paths.isEmpty()));
  cage_->setChecked(recipe["use_cage"].toBool(false));
  average_->setChecked(recipe["average"].toBool(true));
  relative_->setChecked(recipe["relative"].toBool(true));
  front_->setValue(recipe["frontal"].toDouble(.01));
  rear_->setValue(recipe["rear"].toDouble(.01));
  enabled_->setChecked(s.enabled);
  edge_->setChecked(s.edge_protection);
  distance_->setValue(s.edge_distance);
  contrast_->setValue(s.edge_contrast);
  vectors_->setChecked(s.vectors);
  length_->setValue(s.vector_length);
  density_->setValue(s.vector_density);
  opacity_->setValue(s.vector_opacity);
  automatic_->setChecked(auto_normal);
  paint_->setChecked(editing);
  if (!cage_path_->hasFocus())
    cage_path_->setText(recipe["cage_file"].toString());
  refresh_texture_extent_choices(*width_, 32, recipe["resolution"].toInt(512));
  refresh_texture_extent_choices(
      *height_, 32, recipe["height"].toInt(recipe["resolution"].toInt(512)));
  locked_->setChecked(recipe["locked"].toBool(true));
  height_->setEnabled(!locked_->isChecked());
  if (auto_role < 0 && auto_normal)
    auto_role = 0;
  QString general;
  if (!project_->isChecked()) {
    if (paths.isEmpty())
      general = "Choose a high-poly scene or use the project mesh.";
    else
      for (const auto &path : paths)
        if (!QFileInfo(path).isFile()) {
          general = "High-poly scene file is missing: " + path;
          break;
        }
  }
  if (general.isEmpty() && cage_->isChecked() && !QFileInfo(cage_path_->text()).isFile())
    general = "Choose a valid cage mesh file or disable Use custom cage.";
  if (general.isEmpty() && (!project_->isChecked() || cage_->isChecked()) && !cage_->isChecked() &&
      front_->value() + rear_->value() <= 0)
    general = "Frontal/rear distances must span a nonzero interval.";
  for (int role = 0; role < 7; ++role) {
    map_selected_[role]->setChecked(!recipe.contains("enabled") ||
                                    recipe["enabled"].toArray().at(role).toBool());
    map_auto_[role]->setChecked(auto_role == role);
    map_preview_[role]->setChecked(preview == role);
    map_auto_[role]->setEnabled(general.isEmpty() || auto_role == role);
    map_bake_[role]->setEnabled(general.isEmpty());
    map_sync_[role]->setEnabled(set_count > 1);
    if (!general.isEmpty()) {
      map_auto_[role]->setToolTip(general);
      map_bake_[role]->setToolTip(general);
    }
    else {
      map_auto_[role]->setToolTip("Automatically rebake this map; only one map can be active");
      map_bake_[role]->setToolTip("Bake only this map for the current Texture Set");
    }
  }

  int selected = 0;
  for (auto *map : map_selected_)
    selected += map->isChecked();
  selected_count_->setText(selected == 1 ? QString("1 map selected") :
                                           QString("%1 maps selected").arg(selected));
  bake_selected_->setEnabled(general.isEmpty() && selected > 0);
  bake_selected_->setToolTip(
      selected == 0     ? QString("Select at least one mesh map") :
      general.isEmpty() ? QString("Bake the selected maps for the current Texture Set") :
                          general);
  front_->setEnabled(!cage_->isChecked());
  rear_->setEnabled(!cage_->isChecked());
  average_->setEnabled(!cage_->isChecked());
  relative_->setEnabled(!cage_->isChecked());
  QString reason;
  if (project_->isChecked())
    reason = "Skew painting requires a separate high-poly scene.";
  else if (paths.isEmpty())
    reason = "Choose a high-poly mesh scene.";
  else if (cage_->isChecked())
    reason = "Skew painting requires a distance-based cage.";
  else if (!average_->isChecked())
    reason = "Enable Average normals to paint skew correction.";
  else
    for (const auto &path : paths)
      if (!QFileInfo(path).isFile()) {
        reason = "High-poly scene file is missing: " + path;
        break;
      }
  if (reason.isEmpty() && front_->value() + rear_->value() <= 0)
    reason = "Frontal/rear distances must span a nonzero interval.";
  paint_->setEnabled(reason.isEmpty());
  paint_->setToolTip(
      reason.isEmpty() ?
          QString("Paint in the viewport. Ctrl+Shift+B returns to baking settings.") :
          reason);
  rebake_->setEnabled(general.isEmpty());
  reason_->setText(
      reason.isEmpty() ?
          "Brush, Eraser and Polygon Fill edit a separate per-Texture Set skew field." :
          reason);
  updating_ = false;
  refresh_quality();
}
void SkewPanel::refresh_quality()
{
  const bool prior = updating_;
  updating_ = true;
  const auto q = set_.isNull() ?
                     QJsonObject{
                         {"dilation", 32}, {"rays", 256}, {"sampling", 1}, {"diffusion", false}} :
                     bake_quality(recipe_, set_, quality_role_->currentIndex());
  dilation_->setValue(q["dilation"].toInt());
  rays_->setValue(q["rays"].toInt());
  samples_->setCurrentIndex(samples_->findData(q["sampling"].toInt()));
  diffusion_->setChecked(q["diffusion"].toBool());
  rays_->setEnabled(true);
  const int common_rays_val = recipe_.contains("rays") ? recipe_["rays"].toInt(256) :
                                                         q["rays"].toInt(256);
  common_rays_->setValue(common_rays_val);
  const int preset_idx = common_rays_preset_->findData(common_rays_val);
  common_rays_preset_->setCurrentIndex(preset_idx >= 0 ? preset_idx :
                                                         common_rays_preset_->findData(-1));
  const int common_sampling_val = recipe_.contains("sampling") ? recipe_["sampling"].toInt(1) :
                                                                 q["sampling"].toInt(1);
  common_samples_->setCurrentIndex(std::max(0, common_samples_->findData(common_sampling_val)));
  quality_preset_->setCurrentIndex(int(detect_bake_preset(
      common_rays_->value(), common_samples_->currentData().toInt(), denoise_->isChecked())));
  updating_ = prior;
}
void SkewPanel::emit_quality()
{
  if (updating_ || set_.isNull() || !quality_changed)
    return;
  quality_changed(quality_role_->currentIndex(),
                  {{"dilation", dilation_->value()},
                   {"rays", rays_->value()},
                   {"sampling", samples_->currentData().toInt()},
                   {"diffusion", diffusion_->isChecked()}});
}
void SkewPanel::progress(bool running, const QString &message)
{
  status_->setText((running ? QString("Baking… ") : QString{}) + message);
}
}  // namespace bts
