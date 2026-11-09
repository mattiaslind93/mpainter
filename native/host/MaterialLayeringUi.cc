#include "MaterialLayeringUi.hh"
#include "MaterialLayerUdim.hh"
#include "FileDialogs.hh"
#include <QBoxLayout>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QProgressDialog>
#include <QTimer>
namespace bts {
MaterialLayeringUi::MaterialLayeringUi(QWidget &owner,
                                       QBoxLayout &layers,
                                       MaterialLayeringHooks hooks)
    : QObject(&owner), owner_(owner), hooks_(std::move(hooks))
{
  bar_ = new QWidget(layers.parentWidget());
  bar_->setObjectName("materialLayeringControls");
  auto *layout = new QVBoxLayout(bar_);
  layout->setContentsMargins(4, 2, 4, 2);
  auto *line = new QHBoxLayout;
  stack_ = new QComboBox(bar_);
  stack_->setObjectName("materialOpacitySubstack");
  stack_->addItem("Main (retained)", -1);
  for (int i = 0; i < 3; ++i)
    stack_->addItem(QString("Opacity %1").arg(i + 1), i);
  line->addWidget(stack_, 1);
  settings_ = new QToolButton(bar_);
  settings_->setObjectName("editUniformMaterialLayering");
  settings_->setText("Blend materials…");
  line->addWidget(settings_);
  layout->addLayout(line);
  export_button_ = new QToolButton(bar_);
  export_button_->setObjectName("exportMaterialLayerParameters");
  export_button_->setText("Export parameters…");
  export_button_->setToolTip(
      "Export applied materials and three evaluated opacity maps. Paint/effect history is not included. Opacity outside 0..1 and custom surface inputs are unsupported.");
  layout->addWidget(export_button_);
  import_button_ = new QToolButton(bar_);
  import_button_->setObjectName("importMaterialLayerParameters");
  import_button_->setText("Import parameters…");
  import_button_->setToolTip(
      "Import parameter packages into the selected canonical material: version 1 or version 2 with linear ACEScg/Rec.709 Color UDIMs. Existing four-material stacks must be unallocated.");
  layout->addWidget(import_button_);
  note_ = new QLabel(bar_);
  note_->setWordWrap(true);
  note_->setObjectName("materialLayeringScope");
  layout->addWidget(note_);
  layers.insertWidget(1, bar_);
  export_timer_ = new QTimer(this);
  export_timer_->setInterval(16);
  connect(export_timer_, &QTimer::timeout, this, [this] { poll_export(); });
  connect(export_button_, &QToolButton::clicked, this, [this] { export_parameters(); });
  import_timer_ = new QTimer(this);
  import_timer_->setInterval(16);
  connect(import_timer_, &QTimer::timeout, this, [this] { poll_import(); });
  connect(import_button_, &QToolButton::clicked, this, [this] { import_parameters(); });
  connect(settings_, &QToolButton::clicked, this, [this] { open(); });
  connect(stack_, &QComboBox::currentIndexChanged, this, [this](int index) {
    try {
      if (index < 0 || !hooks_.current)
        return;
      auto target = hooks_.current();
      if (!target || !target->texture || !hooks_.select)
        throw std::runtime_error("Opacity selection target is unavailable");
      hooks_.select(*target, stack_->itemData(index).toInt());
      synchronize();
    }
    catch (const std::exception &e) {
      fail(QString::fromUtf8(e.what()));
      synchronize();
    }
  });
  synchronize();
}
MaterialLayeringUi::~MaterialLayeringUi()
{
  import_timer_->stop();
  if (hooks_.cancel_parameter_import)
    hooks_.cancel_parameter_import();
  if (import_progress_) {
    import_progress_->blockSignals(true);
    delete import_progress_.data();
  }
  export_timer_->stop();
  if (export_ && hooks_.cancel_parameter_export)
    hooks_.cancel_parameter_export();
  if (export_ && export_->progress) {
    export_->progress->blockSignals(true);
    delete export_->progress.data();
  }
  clear_buffer();
  if (dialog_) {
    QObject::disconnect(dialog_.data(), nullptr, this, nullptr);
    dialog_->retire_callbacks();
    delete dialog_.data();
  }
  const auto block = [](auto &&self, QObject *object) noexcept -> void {
    object->blockSignals(true);
    for (QObject *child : object->children())
      self(self, child);
  };
  block(block, bar_);
  delete bar_;
}
void MaterialLayeringUi::fail(const QString &error)
{
  note_->setText(error);
  note_->show();
  if (dialog_)
    dialog_->show_error(error);
  if (hooks_.error)
    hooks_.error(error);
}
void MaterialLayeringUi::synchronize()
{
  try {
    const QSignalBlocker block(stack_);
    const auto target = hooks_.current ? hooks_.current() : std::nullopt;
    const bool valid = target && target->texture;
    const bool on = valid && target->texture->material_layering.enabled;
    const bool importing = import_progress_ || (hooks_.parameter_import_status &&
                                                hooks_.parameter_import_status().pending);
    stack_->setEnabled(on && target->unavailable.isEmpty() && !importing);
    settings_->setEnabled(valid && !target->texture->disabled && !export_ && !importing);
    export_button_->setEnabled(valid && target->unavailable.isEmpty() &&
                               target->texture->material_layering.allocated() && !dialog_ &&
                               !export_ && !importing && bool(hooks_.begin_parameter_export) &&
                               bool(hooks_.poll_parameter_export));
    import_button_->setEnabled(valid && target->unavailable.isEmpty() &&
                               !target->texture->material_layering.allocated() && !dialog_ &&
                               !export_ && !importing && bool(hooks_.begin_parameter_import) &&
                               bool(hooks_.parameter_import_status) &&
                               bool(hooks_.cancel_parameter_import));
    stack_->setCurrentIndex(on ? target->texture->material_layering.active_mask + 1 : 0);
    note_->setText(
        !valid ?
            "Create an editable project to use four materials." :
        !target->unavailable.isEmpty() ?
            target->unavailable :
        on ?
            (target->texture->material_layering.active_mask < 0 ?
                 "Main is retained read-only while four-material mode is enabled." :
                 "Paint/Fill writes only this opacity stack; inputs change on explicit Apply.") :
            "Four materials: constants and repeating Color/Metal/Rough/Bump/AO maps; Normal stays internal.");
    bar_->setToolTip(note_->text());
    settings_->setToolTip(note_->text());
    note_->setVisible(valid && !target->unavailable.isEmpty());
  }
  catch (const std::exception &e) {
    stack_->setEnabled(false);
    settings_->setEnabled(false);
    export_button_->setEnabled(false);
    import_button_->setEnabled(false);
    fail(QString::fromUtf8(e.what()));
  }
}
void MaterialLayeringUi::export_parameters()
{
  try {
    if (dialog_ || export_ || import_progress_ ||
        (hooks_.parameter_import_status && hooks_.parameter_import_status().pending))
      throw std::runtime_error("Finish the buffered editor or previous parameter transfer first");
    auto target = hooks_.current ? hooks_.current() : std::nullopt;
    if (!target || !target->texture || !target->unavailable.isEmpty())
      throw std::runtime_error("No compatible material target for parameter export");
    if (!hooks_.begin_parameter_export || !hooks_.poll_parameter_export ||
        !hooks_.cancel_parameter_export)
      throw std::logic_error("Parameter export is unavailable");
    auto expected = *target;
    expected.texture = nullptr;
    target.reset();
    auto *dialog = FileDialog::borrow(&owner_, "Create parameter package folder", {}, {});
    dialog->setObjectName("materialParameterExportDialog");
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    dialog->selectFile("materials.btsparams");
    if (dialog->exec() != QDialog::Accepted || dialog->selectedFiles().isEmpty())
      return;
    const auto directory = dialog->selectedFiles().first();
    target = hooks_.current ? hooks_.current() : std::nullopt;
    if (!target || !target->texture || target->project != expected.project ||
        target->set != expected.set || target->uid != expected.uid ||
        target->revision != expected.revision || !target->unavailable.isEmpty())
      throw std::runtime_error(
          "The material target changed while choosing the export folder; reopen Export parameters");
    auto *progress = new QProgressDialog(
        "Capturing and writing parameter package…", "Cancel", 0, 0, &owner_);
    progress->setObjectName("materialParameterExportProgress");
    progress->setWindowModality(Qt::NonModal);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    auto saved = *target;
    saved.texture = nullptr;
    export_ = Export{std::move(saved), directory, {}, false, progress};
    try {
      hooks_.begin_parameter_export(*target, directory);
    }
    catch (...) {
      export_.reset();
      delete progress;
      throw;
    }
    connect(progress, &QProgressDialog::canceled, this, [this] {
      if (export_) {
        export_->cancelled = true;
        hooks_.cancel_parameter_export();
      }
    });
    progress->show();
    export_timer_->start();
    synchronize();
  }
  catch (const std::exception &e) {
    fail(QString::fromUtf8(e.what()));
  }
}
void MaterialLayeringUi::poll_export()
{
  if (!export_) {
    export_timer_->stop();
    return;
  }
  bool current = false, ready = false;
  try {
    const auto target = hooks_.current ? hooks_.current() : std::nullopt;
    const auto &saved = export_->target;
    current = target && target->texture && target->project == saved.project &&
              target->set == saved.set && target->uid == saved.uid &&
              target->revision == saved.revision && target->unavailable.isEmpty();
  }
  catch (const std::exception &e) {
    export_->error = QString::fromUtf8(e.what());
  }
  if (!current) {
    export_->cancelled = true;
    hooks_.cancel_parameter_export();
  }
  try {
    ready = hooks_.poll_parameter_export(current);
  }
  catch (const std::exception &e) {
    export_->error = QString::fromUtf8(e.what());
    ready = true;
  }
  if (!ready)
    return;
  export_timer_->stop();
  const bool cancelled = export_->cancelled;
  const auto directory = export_->directory, error = export_->error;
  if (export_->progress) {
    export_->progress->blockSignals(true);
    export_->progress->close();
    export_->progress->deleteLater();
  }
  export_.reset();
  synchronize();
  if (!error.isEmpty())
    fail(error);
  else
    note_->setText(cancelled ? "Parameter export cancelled; project unchanged." :
                               "Parameter package saved: " + directory);
}
void MaterialLayeringUi::import_parameters()
{
  try {
    if (dialog_ || export_ || import_progress_ ||
        (hooks_.parameter_import_status && hooks_.parameter_import_status().pending))
      throw std::runtime_error("Finish the buffered editor or previous parameter transfer first");
    auto target = hooks_.current ? hooks_.current() : std::nullopt;
    if (!target || !target->texture || !target->unavailable.isEmpty() ||
        target->texture->material_layering.allocated())
      throw std::runtime_error(
          "Choose an editable canonical material without allocated four-material stacks");
    if (!hooks_.begin_parameter_import || !hooks_.parameter_import_status ||
        !hooks_.cancel_parameter_import)
      throw std::logic_error("Parameter Import is unavailable");
    auto expected = *target;
    expected.texture = nullptr;
    target.reset();
    auto *dialog = FileDialog::borrow(&owner_, "Import material parameters", {}, {});
    dialog->setObjectName("materialParameterImportDialog");
    dialog->setAcceptMode(QFileDialog::AcceptOpen);
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly, true);
    if (dialog->exec() != QDialog::Accepted || dialog->selectedFiles().isEmpty())
      return;
    const auto directory = dialog->selectedFiles().first();
    target = hooks_.current ? hooks_.current() : std::nullopt;
    if (!target || !target->texture || target->project != expected.project ||
        target->set != expected.set || target->uid != expected.uid ||
        target->revision != expected.revision || !target->unavailable.isEmpty() ||
        target->texture->material_layering.allocated())
      throw std::runtime_error(
          "The material target changed while choosing the package folder; reopen Import parameters");
    auto *progress = new QProgressDialog("Reading parameter package…", "Cancel", 0, 0, &owner_);
    progress->setObjectName("materialParameterImportProgress");
    progress->setWindowModality(Qt::NonModal);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    try {
      hooks_.begin_parameter_import(*target, directory);
    }
    catch (...) {
      delete progress;
      throw;
    }
    import_progress_ = progress;
    connect(
        progress, &QProgressDialog::canceled, this, [this] { hooks_.cancel_parameter_import(); });
    progress->show();
    import_timer_->start();
    synchronize();
  }
  catch (const std::exception &error) {
    fail(QString::fromUtf8(error.what()));
  }
}
void MaterialLayeringUi::poll_import()
{
  if (!import_progress_) {
    import_timer_->stop();
    return;
  }
  try {
    const auto status = hooks_.parameter_import_status();
    import_progress_->setLabelText(status.text);
    if (status.pending)
      return;
    import_progress_->blockSignals(true);
    import_progress_->close();
    import_progress_->deleteLater();
    import_progress_.clear();
    import_timer_->stop();
    synchronize();
    if (!status.error.isEmpty())
      fail(status.error);
    else
      note_->setText(status.text);
  }
  catch (const std::exception &error) {
    hooks_.cancel_parameter_import();
    fail(QString::fromUtf8(error.what()));
  }
}
MaterialLayeringTarget MaterialLayeringUi::current_target() const
{
  const auto current = hooks_.current ? hooks_.current() : std::nullopt;
  if (!target_ || !current || !current->texture || current->project != target_->project ||
      current->set != target_->set || current->uid != target_->uid ||
      current->revision != target_->revision)
    throw std::runtime_error("The material target changed; reopen the buffered editor");
  if (!current->unavailable.isEmpty())
    throw std::runtime_error(current->unavailable.toStdString());
  return *current;
}
TextureLoadTarget MaterialLayeringUi::load_target(int input, int channel) const
{
  if (input < 0 || input >= 4 ||
      (channel != 0 && channel != 1 && channel != 2 && channel != 4 && channel != 5))
    throw std::invalid_argument("Unsupported material map target");
  const auto current = current_target();
  TextureLoadTarget result;
  result.document = current.project;
  result.set = current.set;
  result.channel = channel;
  result.buffer = buffer_;
  result.material_input = input;
  result.revision = current.revision;
  return result;
}
bool MaterialLayeringUi::load_target_current(const TextureLoadTarget &target) const
{
  try {
    return dialog_ && target.buffer == buffer_ && !buffer_.isNull() &&
           target.material_input >= 0 && target.material_input < 4 &&
           current_target().revision == target.revision && target.document == target_->project &&
           target.set == target_->set &&
           (target.channel == 0 || target.channel == 1 || target.channel == 2 ||
            target.channel == 4 || target.channel == 5);
  }
  catch (...) {
    return false;
  }
}
void MaterialLayeringUi::texture_loaded(LoadedTexture loaded)
{
  if (!load_target_current(loaded.target))
    throw std::runtime_error("Buffered material texture target changed");
  loaded.texture->validate();
  if (loaded.texture->raw != (loaded.target.channel != 0))
    throw std::invalid_argument(
        "Color maps require Color interpretation; scalar maps require raw data");
  require_material_layer_udim_originals(*loaded.texture, loaded.asset.texture_files);
  if (loaded.target.channel == 0) {
    if (!hooks_.validate_source)
      throw std::logic_error("Material source processor admission is unavailable");
    hooks_.validate_source(*loaded.texture);
  }
  // Catalog publication belongs to Apply. These originals are private buffer values.
  auto next = sources_;
  const auto found = std::find_if(
      next.begin(), next.end(), [&](const auto &asset) { return asset.id == loaded.asset.id; });
  if (found == next.end())
    next.push_back(std::move(loaded.asset));
  else if (found->hash() != loaded.asset.hash())
    throw std::invalid_argument("Buffered source asset changed");
  dialog_->install_texture(
      loaded.target.material_input, loaded.target.channel, std::move(loaded.texture));
  sources_.swap(next);
}
void MaterialLayeringUi::texture_status(const TextureLoadTarget &target,
                                        const TextureLoadStatus &status)
{
  if (dialog_ && target.buffer == buffer_)
    dialog_->load_status(
        target.material_input, target.channel, status.text, status.loading, status.error);
}
bool MaterialLayeringUi::consume_shelf(const StudioAsset &asset)
{
  if (!shelf_) {
    if (shelf_cancelled_ && asset.usages.contains("texture")) {
      shelf_cancelled_ = false;
      fail("Material map selection was cancelled; choose a source slot again");
      return true;
    }
    return false;
  }
  try {
    auto request = std::move(*shelf_);
    shelf_.reset();
    if (!load_target_current(request.target))
      throw std::runtime_error("Material Shelf target changed; choose a source slot again");
    if (!asset.usages.contains("texture"))
      throw std::invalid_argument("Choose a Shelf resource with Usage Texture");
    if (!hooks_.load_asset)
      throw std::logic_error("Material Shelf loading is unavailable");
    hooks_.load_asset(request.target, asset, request.space);
  }
  catch (const std::exception &e) {
    fail(QString::fromUtf8(e.what()));
  }
  return true;
}
void MaterialLayeringUi::clear_buffer()
{
  shelf_cancelled_ |= bool(shelf_);
  shelf_.reset();
  if (target_ && hooks_.cancel_load)
    for (int input = 0; input < 4; ++input)
      for (int channel : {0, 1, 2, 4, 5}) {
        TextureLoadTarget target;
        target.document = target_->project;
        target.set = target_->set;
        target.buffer = buffer_;
        target.material_input = input;
        target.channel = channel;
        hooks_.cancel_load(target);
      }
  if (preparing_ && hooks_.release)
    hooks_.release();
  preparing_ = false;
  target_.reset();
  buffer_ = QUuid();
  sources_.clear();
}
void MaterialLayeringUi::open()
{
  try {
    if (import_progress_ ||
        (hooks_.parameter_import_status && hooks_.parameter_import_status().pending))
      throw std::runtime_error("Finish parameter Import before opening the material editor");
    if (dialog_) {
      dialog_->raise();
      return;
    }
    if (!hooks_.current)
      throw std::logic_error("Four-material editor has no current target");
    auto target = hooks_.current();
    if (!target || !target->texture)
      throw std::runtime_error("Four-material target is unavailable");
    if (!target->unavailable.isEmpty())
      throw std::runtime_error(target->unavailable.toStdString());
    if (hooks_.validate)
      hooks_.validate(*target);
    auto *panel = new MaterialLayeringPanel(&owner_);
    dialog_ = panel;
    panel->setAttribute(Qt::WA_DeleteOnClose);
    connect(panel, &QObject::destroyed, this, [this, panel] {
      if (!dialog_ || dialog_.data() == panel) {
        dialog_.clear();
        synchronize();
      }
    });
    panel->load(*target->texture, target->working_space);
    target_ = *target;
    target_->texture = nullptr;
    buffer_ = QUuid::createUuid();
    shelf_cancelled_ = false;
    panel->completed = [owner = QPointer<MaterialLayeringUi>(this)] {
      if (owner)
        owner->clear_buffer();
    };
    panel->source_requested = [this](int input, int channel, bool file, const std::string &space) {
      try {
        const auto target = load_target(input, channel);
        if (hooks_.cancel_load)
          hooks_.cancel_load(target);
        shelf_.reset();
        shelf_cancelled_ = false;
        if (!file) {
          shelf_ = Shelf{target, space};
          dialog_->show_error("Choose a texture from the Shelf for this input and channel");
          if (hooks_.shelf)
            hooks_.shelf();
          return;
        }
        auto *dialog = FileDialog::borrow(
            &owner_,
            "Choose material image or scalar UDIM sequence",
            {},
            "Images (*.exr *.png *.tif *.tiff *.jpg *.jpeg *.bmp *.tga *.hdr)");
        dialog->setObjectName("materialInputFileDialog");
        dialog->setFileMode(QFileDialog::ExistingFile);
        dialog->setAcceptMode(QFileDialog::AcceptOpen);
        if (dialog->exec() != QDialog::Accepted || dialog->selectedFiles().isEmpty())
          return;
        if (!load_target_current(target))
          throw std::runtime_error("Material target changed during file selection");
        if (!hooks_.load_file)
          throw std::logic_error("Material file loading is unavailable");
        const auto file = dialog->selectedFiles().first();
        FileDialog::remember_folder_async(QFileInfo(file).absolutePath());
        hooks_.load_file(target, file, space);
      }
      catch (const std::exception &e) {
        fail(QString::fromUtf8(e.what()));
      }
    };
    panel->source_cleared = [this](int input, int channel) {
      try {
        auto target = load_target(input, channel);
        if (hooks_.cancel_load)
          hooks_.cancel_load(target);
        if (shelf_ && shelf_->target.same_slot(target))
          shelf_.reset();
      }
      catch (const std::exception &e) {
        fail(QString::fromUtf8(e.what()));
      }
    };
    panel->apply_inputs_requested = [this](bool enabled, const auto &inputs) {
      const auto current = current_target();
      preparing_ = true;
      if (hooks_.apply_inputs) {
        if (!hooks_.apply_inputs(current, enabled, inputs, sources_))
          return false;
      }
      else {
        if (!hooks_.apply)
          throw std::logic_error("Four-material editor has no transaction callback");
        std::array<MaterialValues, 4> values;
        for (int i = 0; i < 4; ++i)
          values[i] = inputs[i].values;
        hooks_.apply(current, enabled, values);
      }
      synchronize();
      return true;
    };
    panel->show();
    synchronize();
  }
  catch (const std::exception &e) {
    clear_buffer();
    fail(QString::fromUtf8(e.what()));
  }
}
}  // namespace bts
