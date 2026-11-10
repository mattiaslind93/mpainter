// SPDX-License-Identifier: GPL-3.0-or-later
#include "NumericLocale.hh"
#include "InlineNameDelegate.hh"
#include "TextureSetRowSelection.hh"
#include "ShaderController.hh"
#include "MaterialLayeringAuthoring.hh"
#include "ColorPicker.hh"
#include "OpenPbrPanel.hh"
#include <QCheckBox>
#include <QComboBox>
#include "TextureDocument.hh"
#include "WorkspaceShell.hh"
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
namespace bts {
namespace {
class InstanceNameEdit final : public QLineEdit {
 public:
  using QLineEdit::QLineEdit;

 protected:
  void keyPressEvent(QKeyEvent *event) override
  {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
      Q_EMIT editingFinished();
      event->accept();
      return;
    }
    QLineEdit::keyPressEvent(event);
  }
};
}  // namespace
ShaderController::ShaderController(WorkspaceShell *shell, ShaderHooks hooks)
    : QObject(shell), shell_(shell), hooks_(std::move(hooks))
{
  dialog_ = shell->findChild<QDialog *>("shaderSettings");
  dialog_->setWindowTitle("Shader settings");
  dialog_->resize(400, 700);
  auto *layout = qobject_cast<QVBoxLayout *>(dialog_->layout());
  auto *head = new QWidget(dialog_);
  auto *header = new QVBoxLayout(head);
  auto *tools = new QHBoxLayout;
  auto *dock = new QPushButton("Dock", head);
  dock->setObjectName("shaderDock");
  auto *close = new QPushButton("Close", head);
  tools->addWidget(dock);
  tools->addWidget(close);
  header->addLayout(tools);
  auto *history_row = new QHBoxLayout;
  undo_ = new QPushButton("Undo", head);
  redo_ = new QPushButton("Redo", head);
  undo_->setObjectName("shaderUndo");
  redo_->setObjectName("shaderRedo");
  history_row->addWidget(undo_);
  history_row->addWidget(redo_);
  header->addLayout(history_row);
  workflow_ = new QComboBox(head);
  workflow_->setObjectName("shaderWorkflow");
  workflow_->addItem("Principled metallic roughness", 0);
  workflow_->addItem("OpenPBR 1.1 — Principled compatibility", 1);
  workflow_->setToolTip(
      "Portable subset using standard Blender nodes. Unsupported OpenPBR lobes are reported; this is not BSDF conformance.");
  header->addWidget(workflow_);
  connect(workflow_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (syncing_)
      return;
    change(
        [&](auto &state) {
          auto p = state.for_set(selected_).parameters;
          if (index)
            p.openpbr.emplace();
          else
            p.openpbr.reset();
          state.parameters(state.assignments.at(selected_), p);
        },
        true);
  });
  auto *form = new QFormLayout;
  name_ = new InstanceNameEdit(head);
  name_->setObjectName("shaderInstanceName");
  form->addRow("Instance name", name_);
  header->addLayout(form);
  layout->insertWidget(0, head);
  auto *content = dialog_->findChild<QWidget *>("shaderSettingsContent");
  auto *parameters = new QWidget(content);
  auto *fields = new QFormLayout(parameters);
  fields->addRow(new QLabel("Common", parameters));
  const auto number = [&](const char *id, double high) {
    auto *spin = new bts::NumericSpinBox(parameters);
    spin->setObjectName(id);
    spin->setLocale(QLocale::c());
    spin->setRange(0, high);
    spin->setDecimals(3);
    spin->setSingleStep(.05);
    return spin;
  };
  normal_ = number("shaderNormalIntensity", 4);
  ao_ = number("shaderAOIntensity", 1);
  fields->addRow("Normal intensity", normal_);
  fields->addRow("AO intensity", ao_);
  auto *restore = new QPushButton("Restore shader defaults", parameters);
  restore->setObjectName("shaderRestoreDefaults");
  fields->addRow(restore);
  qobject_cast<QVBoxLayout *>(content->layout())->insertWidget(0, parameters);
  auto *sss_title = new QLabel("Subsurface Scattering Parameters", parameters);
  sss_title->setObjectName("shaderLegacySubsurfaceTitle");
  fields->addRow(sss_title);
  subsurface_enabled_ = new QCheckBox("Enable", parameters);
  subsurface_enabled_->setObjectName("shaderSubsurfaceEnabled");
  fields->addRow(subsurface_enabled_);
  subsurface_fields_ = new QWidget(parameters);
  auto *sss_form = new QFormLayout(subsurface_fields_);
  subsurface_type_ = new QComboBox(subsurface_fields_);
  subsurface_type_->setObjectName("shaderSubsurfaceType");
  subsurface_type_->addItems({"Skin", "Translucent"});
  subsurface_type_->setToolTip(
      "Cycles: random-walk skin / volume. EEVEE: screen-space diffusion.");
  sss_form->addRow("Scattering Type", subsurface_type_);
  subsurface_scale_ = number("shaderSubsurfaceScale", 100);
  sss_form->addRow("Scale (scene units)", subsurface_scale_);
  subsurface_color_ = new ColorPicker(subsurface_fields_);
  subsurface_color_->setObjectName("shaderSubsurfaceColor");
  subsurface_color_->findChild<QPushButton *>("colorEyedropper")->hide();
  subsurface_color_->setToolTip(
      "Wavelength radii tint for native Principled subsurface scattering. Surface albedo remains unchanged.");
  sss_form->addRow("Color", subsurface_color_);
  fields->addRow(subsurface_fields_);
  openpbr_ = new OpenPbrPanel(parameters);
  fields->addRow(openpbr_);
  openpbr_->changed = [this](const OpenPbrParameters &profile, bool immediate) {
    if (syncing_)
      return;
    change(
        [&](auto &state) {
          auto value = state.for_set(selected_).parameters;
          value.openpbr = profile;
          state.parameters(state.assignments.at(selected_), value);
        },
        immediate);
  };
  openpbr_->finished = [this] { finish(); };
  openpbr_->error = [this](const QString &message) { hooks_.log(message); };
  const auto subsurface_edit = [this](bool immediate) {
    if (syncing_)
      return;
    auto value = selected() ? selected()->shader_parameters : ShaderParameters{};
    value.subsurface.enabled = subsurface_enabled_->isChecked();
    value.subsurface.type = subsurface_type_->currentIndex();
    value.subsurface.scale = float(subsurface_scale_->value());
    value.subsurface.color = subsurface_color_->color();
    subsurface_fields_->setEnabled(value.subsurface.enabled);
    change([&](auto &state) { state.parameters(state.assignments.at(selected_), value); },
           immediate);
  };
  connect(subsurface_enabled_, &QCheckBox::toggled, this, [subsurface_edit] {
    subsurface_edit(true);
  });
  connect(subsurface_type_, &QComboBox::currentIndexChanged, this, [subsurface_edit] {
    subsurface_edit(true);
  });
  connect(subsurface_scale_, &QDoubleSpinBox::valueChanged, this, [subsurface_edit] {
    subsurface_edit(false);
  });
  connect(subsurface_scale_, &QDoubleSpinBox::editingFinished, this, [this] { finish(); });
  subsurface_color_->changed = [subsurface_edit](const auto &) { subsurface_edit(false); };
  subsurface_color_->finished = [this] { finish(); };
  frame_.setSingleShot(true);
  frame_.setInterval(16);
  idle_.setSingleShot(true);
  idle_.setInterval(180);
  connect(&frame_, &QTimer::timeout, this, [this] { apply(); });
  connect(&idle_, &QTimer::timeout, this, [this] { finish(); });
  connect(undo_, &QPushButton::clicked, this, [this] { history(false); });
  connect(redo_, &QPushButton::clicked, this, [this] { history(true); });
  connect(close, &QPushButton::clicked, dialog_, &QDialog::accept);
  connect(dock, &QPushButton::clicked, this, [this, dock] {
    if (dialog_->isWindow()) {
      dialog_->hide();
      dialog_->setParent(shell_->inspector_splitter(), Qt::Widget);
      shell_->inspector_splitter()->addWidget(dialog_);
      dock->setText("Undock");
      dialog_->show();
    }
    else {
      dialog_->hide();
      dialog_->setParent(shell_, Qt::Dialog);
      dock->setText("Dock");
      dialog_->show();
    }
  });
  connect(name_, &QLineEdit::editingFinished, this, [this] {
    if (syncing_)
      return;
    const auto text = name_->text().trimmed();
    change([&](auto &state) { state.rename(state.assignments.at(selected_), text); }, true);
  });
  const auto parameter = [this] {
    if (syncing_)
      return;
    auto value = selected() ? selected()->shader_parameters : ShaderParameters{};
    value.normal_intensity = float(normal_->value());
    value.ao_intensity = float(ao_->value());
    change([&](auto &state) { state.parameters(state.assignments.at(selected_), value); }, false);
  };
  connect(normal_, &QDoubleSpinBox::valueChanged, this, parameter);
  connect(ao_, &QDoubleSpinBox::valueChanged, this, parameter);
  connect(normal_, &QDoubleSpinBox::editingFinished, this, [this] { finish(); });
  connect(ao_, &QDoubleSpinBox::editingFinished, this, [this] { finish(); });
  connect(restore, &QPushButton::clicked, this, [this] {
    change(
        [&](auto &state) {
          ShaderParameters defaults;
          if (state.for_set(selected_).parameters.openpbr)
            defaults.openpbr.emplace();
          state.parameters(state.assignments.at(selected_), defaults);
        },
        true);
  });
  auto *list = shell_->texture_sets();
  list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(list, &QListWidget::customContextMenuRequested, this, [this, list](QPoint point) {
    menu(list->row(list->itemAt(point)));
  });
  for (auto *button : dialog_->findChildren<QPushButton *>()) {
    button->setAutoDefault(false);
    button->setDefault(false);
  }
  sync();
}
const TextureSet *ShaderController::selected() const
{
  const auto *doc = hooks_.document();
  if (!doc)
    return nullptr;
  for (const auto &set : doc->texture_sets())
    if (set.material_session_uid == hooks_.material())
      return &set;
  return nullptr;
}
void ShaderController::reset_document()
{
  frame_.stop();
  idle_.stop();
  before_.reset();
  undo_history_.clear();
  redo_history_.clear();
  project_ = QUuid();
  selected_ = QUuid();
}
void ShaderController::apply()
{
  if (auto *doc = hooks_.document()) {
    for (const auto &set : doc->texture_sets())
      if (!set.disabled && set.uniform_editable)
        hooks_.apply(set);
    hooks_.repaint();
  }
}
void ShaderController::finish()
{
  idle_.stop();
  if (frame_.isActive()) {
    frame_.stop();
    apply();
  }
  if (before_) {
    if (auto *doc = hooks_.document(); doc && *before_ != doc->shader_instances()) {
      undo_history_.push_back(std::move(*before_));
      if (undo_history_.size() > 128)
        undo_history_.erase(undo_history_.begin());
      redo_history_.clear();
    }
    before_.reset();
  }
  undo_->setEnabled(!undo_history_.empty());
  redo_->setEnabled(!redo_history_.empty());
}
void ShaderController::change(const std::function<void(ShaderInstances &)> &edit, bool immediate)
{
  auto *doc = hooks_.document();
  const auto *set = selected();
  if (!doc || !set)
    return;
  try {
    const auto project = doc->id(), target = set->id;
    const auto source = doc->shader_instances();
    auto state = source;
    edit(state);  // Evaluate the actual operation once, against its original input.
    if (state == source) {
      if (immediate)
        sync();
      return;
    }
    require_material_layering_workflow(*doc, state);
    if (!before_) {
      hooks_.end_input();
      doc = hooks_.document();  // May publish a new TextureDocument.
      const auto *current = selected();
      if (!doc || doc->id() != project || !current || current->id != target ||
          selected_ != target || doc->shader_instances() != source)
        throw std::runtime_error("Shader target changed while finishing input; repeat the edit");
      require_material_layering_workflow(
          *doc, state);  // The mode can change without shader-state changes.
      before_ = source;
    }
    doc->set_shader_instances(std::move(state));
    hooks_.dirty();
    if (!frame_.isActive())
      frame_.start();
    if (immediate) {
      finish();
      sync();
    }
    else
      idle_.start();
  }
  catch (const std::exception &e) {
    hooks_.log(QString::fromUtf8(e.what()));
    sync();
  }
}
void ShaderController::history(bool redo)
{
  finish();
  auto *doc = hooks_.document();
  auto &from = redo ? redo_history_ : undo_history_, &to = redo ? undo_history_ : redo_history_;
  if (!doc || from.empty())
    return;
  try {
    const auto project = doc->id();
    const auto source = doc->shader_instances();
    const auto selected = selected_;
    const auto count = from.size(), other_count = to.size();
    const auto entry = from.back();
    auto state = entry;
    state.reconcile([&] {
      std::set<QUuid> ids;
      for (const auto &set : doc->texture_sets())
        ids.insert(set.id);
      return ids;
    }());
    require_material_layering_workflow(*doc, state);
    hooks_.end_input();
    doc = hooks_.document();
    if (!doc || doc->id() != project || selected_ != selected ||
        doc->shader_instances() != source || from.size() != count || to.size() != other_count ||
        from.back() != entry)
      throw std::runtime_error(
          "Shader history target changed while finishing input; repeat the action");
    require_material_layering_workflow(*doc, state);
    to.push_back(source);  // Allocate history before changing the live shader state.
    try {
      doc->set_shader_instances(std::move(state));
    }
    catch (...) {
      to.pop_back();
      throw;
    }
    from.pop_back();
    hooks_.dirty();
    apply();
    sync();
  }
  catch (const std::exception &e) {
    hooks_.log(QString::fromUtf8(e.what()));
    sync();
  }
}
// Rendering reparents the settings scroll outside dialog_; resolve the title
// through its stable parameter owner alongside the retained subsurface controls.
void ShaderController::sync()
{
  auto *doc = hooks_.document();
  if (doc && project_ != doc->id()) {
    reset_document();
    project_ = doc->id();
  }
  const auto *set = selected();
  const auto id = set ? set->id : QUuid{};
  if (id != selected_)
    finish();
  selected_ = id;
  syncing_ = true;
  const bool enabled = set && !set->disabled && set->uniform_editable;
  name_->setEnabled(bool(set));
  normal_->setEnabled(enabled);
  ao_->setEnabled(enabled);
  if (set) {
    const auto &instance = doc->shader_instances().for_set(id);
    name_->setText(instance.name);
    normal_->setValue(instance.parameters.normal_intensity);
    ao_->setValue(instance.parameters.ao_intensity);
  }
  else
    name_->clear();
  const auto scattering = set ? set->shader_parameters.subsurface : SubsurfaceParameters{};
  const bool compat = set && set->shader_parameters.openpbr.has_value();
  workflow_->setEnabled(enabled);
  workflow_->setCurrentIndex(compat ? 1 : 0);
  openpbr_->setVisible(compat);
  subsurface_fields_->parentWidget()
      ->findChild<QLabel *>("shaderLegacySubsurfaceTitle", Qt::FindDirectChildrenOnly)
      ->setVisible(!compat);
  if (compat)
    openpbr_->synchronize(*set->shader_parameters.openpbr, enabled);
  subsurface_enabled_->setVisible(!compat);
  subsurface_fields_->setVisible(!compat);
  subsurface_enabled_->setEnabled(enabled);
  subsurface_enabled_->setChecked(scattering.enabled);
  subsurface_type_->setCurrentIndex(scattering.type);
  subsurface_scale_->setValue(scattering.scale);
  subsurface_color_->set_color(scattering.color);
  subsurface_fields_->setEnabled(enabled && scattering.enabled);
  syncing_ = false;
  undo_->setEnabled(!undo_history_.empty());
  redo_->setEnabled(!redo_history_.empty());
  rows();
}
void ShaderController::rows()
{
  auto *doc = hooks_.document();
  auto *list = shell_->texture_sets();
  for (int i = 0; i < list->count(); ++i) {
    auto *item = list->item(i);
    const auto uid = item->data(Qt::UserRole).toUInt();
    const TextureSet *set = nullptr;
    if (doc)
      for (const auto &candidate : doc->texture_sets())
        if (candidate.material_session_uid == uid)
          set = &candidate;
    if (!set)
      continue;
    const auto &instance = doc->shader_instances().for_set(set->id);
    auto *row = list->itemWidget(item);
    if (!row) {
      row = new QWidget(list);
      row->setAutoFillBackground(true);
      auto *line = new QHBoxLayout(row);
      line->setContentsMargins(4, 0, 4, 0);
      auto *loaded = new QCheckBox(row);
      loaded->setObjectName("textureSetLoaded");
      loaded->setToolTip("Uncheck to hide and unload; check to reload");
      line->addWidget(loaded);
      connect(loaded, &QCheckBox::toggled, this, [item](bool checked) {
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
      });
      auto *label = new InlineNameField(row);
      label->setObjectName("textureSetName");
      new TextureSetRowSelection(list, label);
      auto *shader = new QPushButton(row);
      shader->setObjectName("textureSetShader");
      shader->setFlat(true);
      line->addWidget(label, 1);
      line->addWidget(shader);
      list->setItemWidget(item, row);
      connect(shader, &QPushButton::clicked, this, [this, item] {
        menu(shell_->texture_sets()->row(item));
      });
    }
    auto *label = static_cast<InlineNameField *>(row->findChild<QLineEdit *>("textureSetName"));
    {
      auto *loaded = row->findChild<QCheckBox *>("textureSetLoaded");
      const QSignalBlocker block(loaded);
      loaded->setChecked(set->loaded);
      loaded->setEnabled(!set->disabled);
    }
    label->setEnabled(set->loaded);
    row->findChild<QPushButton *>("textureSetShader")->setEnabled(set->loaded);
    label->synchronize(item->text());
    // Items/widgets survive a same-size project replacement. Bind the current
    // native identity every sync; queued name commits retain their original ID.
    label->renamed = [this, uid](const QString &name) {
      if (auto *action = shell_->findChild<QAction *>("renameTextureSet")) {
        action->setData(QVariantMap{{"uid", uid}, {"name", name}});
        action->trigger();
      }
    };
    row->findChild<QPushButton *>("textureSetShader")->setText(instance.name);
    // QListWidget does not derive item height from its persistent row widget.
    // Include the themed button/checkbox minimums (also at non-default DPI).
    row->ensurePolished();
    item->setSizeHint(QSize(0, row->sizeHint().expandedTo(row->minimumSizeHint()).height() + 2));
    item->setData(Qt::UserRole + 30, instance.name);
    item->setData(Qt::UserRole + 31, ShaderInstances::key(instance.id));
  }
}
void ShaderController::menu(int row)
{
  auto *list = shell_->texture_sets();
  if (row < 0 || row >= list->count())
    return;
  list->setCurrentRow(row);
  sync();
  const auto *set = selected();
  auto *doc = hooks_.document();
  if (!set || !doc)
    return;
  finish();
  QMenu menu(shell_);
  menu.setObjectName("shaderInstanceMenu");
  const auto state = doc->shader_instances();
  for (const auto &[id, instance] : state.instances) {
    auto *action = menu.addAction(instance.name);
    action->setCheckable(true);
    action->setChecked(id == state.assignments.at(selected_));
    connect(action, &QAction::triggered, this, [this, id] {
      change([&](auto &s) { s.assign(selected_, id); }, true);
    });
  }
  menu.addSeparator();
  auto *create = menu.addAction("New shader instance…");
  create->setObjectName("newShaderInstance");
  connect(create, &QAction::triggered, this, [this] {
    bool ok = false;
    const auto name = QInputDialog::getText(shell_,
                                            "New shader instance",
                                            "Instance name",
                                            QLineEdit::Normal,
                                            "Independent shader",
                                            &ok);
    if (ok)
      change(
          [&](auto &s) {
            const auto id = s.create(s.assignments.at(selected_), name.trimmed());
            s.assign(selected_, id);
          },
          true);
  });
  auto *remove = menu.addMenu("Delete unused instance");
  for (const auto &[id, instance] : state.instances)
    if (std::none_of(state.assignments.begin(),
                     state.assignments.end(),
                     [&](const auto &entry) { return entry.second == id; }))
    {
      auto *action = remove->addAction(instance.name);
      connect(action, &QAction::triggered, this, [this, id] {
        change([&](auto &s) { s.remove(id); }, true);
      });
    }
  remove->setEnabled(!remove->actions().isEmpty());
  menu.exec(QCursor::pos());
}
}  // namespace bts
