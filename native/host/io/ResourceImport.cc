// SPDX-License-Identifier: GPL-3.0-or-later
#include "LongOperationCancel.hh"
#include "Theme.hh"
#include "BrushPreset.hh"
#include "ResourceImport.hh"
#include "ResourceImportUsage.hh"
#include "SpsmAssetDispatch.hh"
#include "SpsmImportUi.hh"
#include "SpsmBitmapOptions.hh"
#include "SpsmAoAliasOption.hh"
#include "FillTextureIO.hh"
#include "ImageSequence.hh"
#include "BitmapMask.hh"
#include "Performance.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include <atomic>
#include <future>
#include <mutex>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include "FileDialogs.hh"
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
namespace bts {
namespace {
class ImportDialog final : public QDialog {
 public:
  using QDialog::QDialog;
  std::function<void()> cancelling;
  void reject() override
  {
    if (cancelling)
      cancelling();
    else
      QDialog::reject();
  }
  void finish_cancel()
  {
    cancelling = {};
    QDialog::reject();
  }
};
}  // namespace
std::optional<StudioAsset> choose_resource_import(QWidget *parent,
                                                  bool project_available,
                                                  const QString &initial_path)
{
  ImportDialog dialog(parent);
  dialog.setObjectName("resourceImportDialog");
  dialog.setWindowTitle("Import resources");
  dialog.resize(560, 545);
  auto *layout = new QVBoxLayout(&dialog);
  // Policy controls can exceed a small laptop's height. Keep progress and the
  // cancel/import buttons visible while only the resource form scrolls.
  auto *scroll = new QScrollArea(&dialog);
  scroll->setObjectName("resourceImportScroll");
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto *body = new QWidget;
  auto *form = new QFormLayout(body);
  form->setSizeConstraint(QLayout::SetMinimumSize);
  form->setRowWrapPolicy(QFormLayout::WrapLongRows);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  scroll->setWidget(body);
  layout->addWidget(scroll, 1);
  auto *file_row = new QWidget(&dialog);
  auto *file_layout = new QHBoxLayout(file_row);
  file_layout->setContentsMargins(0, 0, 0, 0);
  auto *path = new QLineEdit(file_row);
  path->setObjectName("resourceImportPath");
  file_layout->addWidget(path);
  auto *browse = new QPushButton("Browse…", file_row);
  file_layout->addWidget(browse);
  form->addRow("Resource", file_row);
  QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
    const auto chosen = FileDialog::getOpenFileName(
        &dialog,
        "Import resource",
        {},
        "Resources (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.hdr *.tga *.bmp *.btsmaterial *.btsmask *.btsbrush *.spsm *.spmsk)");
    if (!chosen.isEmpty())
      path->setText(chosen);
  });
  auto *name = new QLineEdit(&dialog);
  name->setObjectName("resourceImportName");
  name->setPlaceholderText("Use filename");
  form->addRow("Name", name);
  auto *usage = new QComboBox(&dialog);
  usage->setObjectName("resourceImportUsage");
  usage->addItems(
      {"Alpha", "Texture", "Environment", "Material preset", "Mask generator", "Brush"});
  form->addRow("Usage", usage);
  usage->setCurrentIndex(1);
  bool usage_chosen = false;
  QObject::connect(usage, &QComboBox::activated, &dialog, [&] { usage_chosen = true; });
  QObject::connect(path, &QLineEdit::textChanged, &dialog, [&] {
    if (usage_chosen)
      return;
    const auto ext = QFileInfo(path->text()).suffix().toLower();
    const int preset_usage = resource_preset_usage(path->text());
    usage->setCurrentIndex(preset_usage >= 0 ? preset_usage : 1);
  });
  auto *spsm_space = new QComboBox(&dialog);
  spsm_space->setObjectName("spsmUniformInputSpace");
  spsm_space->addItems(fill_texture_color_spaces());
  spsm_space->setCurrentText("Linear Rec.709");
  spsm_space->setToolTip(
      "Source uniform RGB interpretation is an assumption. Scalar channels are raw. Verify this choice against the source material.");
  auto *spsm_space_label = new QLabel("SPSM uniform color input", &dialog);
  form->addRow(spsm_space_label, spsm_space);
  spsm_space->hide();
  spsm_space_label->hide();
  QObject::connect(path, &QLineEdit::textChanged, &dialog, [=] {
    const bool visible = (QFileInfo(path->text()).suffix().compare("spsm", Qt::CaseInsensitive) ==
                              0 ||
                          QFileInfo(path->text()).suffix().compare("spmsk", Qt::CaseInsensitive) ==
                              0);
    spsm_space->setVisible(visible);
    spsm_space_label->setVisible(visible);
  });
  auto *spsm_maps = new SpsmAoAliasOption(&dialog);
  form->addRow(spsm_maps);
  spsm_maps->hide();
  QObject::connect(path, &QLineEdit::textChanged, &dialog, [=] {
    spsm_maps->setVisible(
        (QFileInfo(path->text()).suffix().compare("spsm", Qt::CaseInsensitive) == 0 ||
         QFileInfo(path->text()).suffix().compare("spmsk", Qt::CaseInsensitive) == 0));
  });
  auto *spsm_bitmaps = new SpsmBitmapOptions(&dialog);
  form->addRow(spsm_bitmaps);
  spsm_bitmaps->hide();
  QObject::connect(path, &QLineEdit::textChanged, &dialog, [=] {
    spsm_bitmaps->setVisible(
        (QFileInfo(path->text()).suffix().compare("spsm", Qt::CaseInsensitive) == 0 ||
         QFileInfo(path->text()).suffix().compare("spmsk", Qt::CaseInsensitive) == 0));
  });
  auto *scope = new QComboBox(&dialog);
  scope->setObjectName("resourceImportScope");
  scope->addItems({"Current session", "Current project", "User shelf"});
  scope->setCurrentIndex(2);
  if (!project_available)
    static_cast<QStandardItemModel *>(scope->model())->item(1)->setEnabled(false);
  form->addRow("Import to", scope);
  auto *hint = new QLabel(
      "Choose one .1001 / _1001 / -1001 image to discover its UV tiles. User shelf copies textures into the asset library; project resources are packed on Save.",
      &dialog);
  hint->setWordWrap(true);
  form->addRow(hint);
  auto *status = new QLabel(&dialog);
  status->setObjectName("resourceImportStatus");
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *progress_bar = new QProgressBar(&dialog);
  progress_bar->setObjectName("resourceImportProgress");
  progress_bar->hide();
  layout->addWidget(progress_bar);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText("Import");
  layout->addWidget(buttons);
  std::optional<StudioAsset> result;
  std::future<StudioAsset> job;
  auto cancel = std::make_shared<std::atomic_bool>(false);
  struct Progress {
    std::mutex mutex;
    int index = 0, total = 0;
    QString file;
  };
  auto progress = std::make_shared<Progress>();
  QTimer poll;
  poll.setInterval(16);
  const auto controls = [&](bool enabled) {
    for (QWidget *widget : QList<QWidget *>{file_row,
                                            name,
                                            usage,
                                            scope,
                                            spsm_space,
                                            spsm_bitmaps,
                                            spsm_maps,
                                            buttons->button(QDialogButtonBox::Ok)})
      widget->setEnabled(enabled);
  };
  LongOperationCancel escape([&] { return job.valid(); }, [&] { dialog.reject(); });
  dialog.cancelling = [&] {
    if (job.valid()) {
      cancel->store(true);
      status->setText("Cancelling…");
    }
    else
      dialog.finish_cancel();
  };
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(&poll, &QTimer::timeout, &dialog, [&] {
    if (!job.valid())
      return;
    if (!cancel->load()) {
      std::lock_guard lock(progress->mutex);
      if (progress->total > 0) {
        status->setText(QString("Loading %1/%2 (%3)…")
                            .arg(std::min(progress->index + 1, progress->total))
                            .arg(progress->total)
                            .arg(progress->file));
        progress_bar->setRange(0, progress->total);
        progress_bar->setValue(progress->index);
      }
      else
        status->setText("Preparing source and import report…");
    }
    if (job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return;
    try {
      auto asset = job.get();
      poll.stop();
      if (cancel->load()) {
        dialog.finish_cancel();
        return;
      }
      if (!confirm_spsm_source(&dialog, asset)) {
        status->setText("Import cancelled; no resource was installed");
        progress_bar->hide();
        controls(true);
        return;
      }
      result = std::move(asset);
      dialog.accept();
    }
    catch (const std::exception &error) {
      poll.stop();
      if (cancel->load()) {
        dialog.finish_cancel();
        return;
      }
      status->setStyleSheet(bts::theme::qss("color:@Error@"));
      status->setText(QString::fromUtf8(error.what()));
      progress_bar->hide();
      controls(true);
    }
  });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    if (job.valid())
      return;
    const auto source_path = path->text(), custom_name = name->text().trimmed(),
               spsm_input = spsm_space->currentText();
    const auto spsm_policy_tags = spsm_bitmaps->policy_tags() + spsm_maps->policy_tags();
    const int kind = usage->currentIndex();
    if (!resource_preset_usage_matches(source_path, kind)) {
      status->setText(
          resource_preset_usage(source_path) == 5 ? "Choose Brush for this preset file" :
          resource_preset_usage(source_path) == 4 ? "Choose Mask generator for this preset file" :
                                                    "Choose Material preset for this preset file");
      return;
    }
    const auto destination = AssetScope(scope->currentIndex() + 1);
    if (destination == AssetScope::Project && !project_available) {
      status->setText("Create a project before importing project resources");
      return;
    }
    cancel->store(false);
    controls(false);
    status->setStyleSheet({});
    status->setText("Loading…");
    progress_bar->setRange(0, 0);
    progress_bar->show();
    job = std::async(
        std::launch::async,
        [source_path,
         custom_name,
         spsm_input,
         spsm_policy_tags,
         kind,
         destination,
         cancel,
         progress] {
          const auto started = performance::Clock::now();
          StudioAsset asset;
          asset.usages = {QStringList{
              "alpha", "texture", "environment", "smartmaterial", "smartmask", "brush"}[kind]};
          asset.scope = destination;
          if (kind >= 3) {
            QFile input(source_path);
            if (!input.open(QIODevice::ReadOnly) || input.size() <= 0)
              throw std::runtime_error("Cannot read preset file");
            asset.bytes = input.readAll();
            asset.path = QFileInfo(source_path).absoluteFilePath();
            asset.filename = QFileInfo(source_path).fileName();
            asset.name = custom_name.isEmpty() ? QFileInfo(source_path).completeBaseName() :
                                                 custom_name;
            if (is_spsm_archive_asset(asset)) {
              asset.search_terms.append("spsm:policy:uniform-input=" + spsm_input);
              asset.search_terms.append(spsm_policy_tags);
            }
            hydrate_asset_presets(asset);
            if (kind == 5 && custom_name.isEmpty())
              asset.name = read_brush_preset(asset.bytes).name;
          }
          else {
            const auto imported = kind == 1 ? reference_image_resource(
                                                  source_path,
                                                  true,
                                                  cancel.get(),
                                                  [&](int index, int total, const QString &file) {
                                                    std::lock_guard lock(progress->mutex);
                                                    progress->index = index;
                                                    progress->total = total;
                                                    progress->file = file;
                                                  }) :
                                              read_image_resource(source_path, kind != 2);
            asset.filename = imported.filename;
            asset.path = imported.path;
            asset.name = custom_name.isEmpty() ? imported.name : custom_name;
            asset.bytes = imported.bytes;
            asset.texture_files = imported.files;
          }
          if (kind == 1) {
            for (const auto &file : asset.texture_files) {
              if (cancel->load())
                throw std::runtime_error("Import cancelled");
              using namespace blender;
              const auto encoded = file.path.toUtf8();
              auto *header = IMB_load_image_from_filepath(
                  encoded.constData(), ImBufFlags::Test | ImBufFlags::NoColorspaceConvert);
              if (!header)
                throw std::runtime_error(
                    (file.filename + ": corrupt or unsupported image").toStdString());
              const bool valid = header->x > 0 && header->y > 0;
              IMB_freeImBuf(header);
              if (!valid)
                throw std::runtime_error(
                    (file.filename + ": invalid image dimensions").toStdString());
            }
          }
          if (kind == 0) {
            BrushAlpha alpha;
            alpha.kind = AlphaKind::Bitmap;
            alpha.bitmap = read_bitmap_alpha_bytes(asset.bytes, asset.name);
            asset.alpha = alpha;
          }
          if (cancel->load())
            throw std::runtime_error("Import cancelled");
          asset_metadata_read(asset_metadata(asset));
          std::printf(
              "BTS_RESOURCE_IMPORT_MS wall_ms=%.3f alpha_decodes=%d file_references=%zu encoded_bytes_in_memory=%lld\n",
              performance::ms(started),
              kind == 0 ? 1 : 0,
              asset.texture_files.size(),
              static_cast<long long>(asset.bytes.size()));
          return asset;
        });
    poll.start();
  });
  if (!initial_path.isEmpty())
    path->setText(initial_path);
  return dialog.exec() == QDialog::Accepted ? result : std::nullopt;
}
}  // namespace bts
