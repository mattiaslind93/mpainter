#include "SpsmImportUi.hh"
#include "SpsmAssetDispatch.hh"
#include "SpsmSource.hh"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <chrono>
#include <future>
#include <algorithm>
#include <optional>

namespace bts {
namespace {
class ApplicationPoll final : public QTimer {
 public:
  using QTimer::QTimer;
  std::function<void()> cancel;
};
}
void finish_spsm_applications(QWidget *parent)
{
  if (!parent) return;
  for (auto *timer : parent->findChildren<QTimer *>("spsmApplicationPoll")) {
    auto *poll = static_cast<ApplicationPoll *>(timer);
    poll->stop();
    if (poll->cancel) poll->cancel();
    // Disconnect/delete releases the future on the owner while Blender is
    // still alive. Never deliver a material edit during shutdown.
    delete poll;
  }
}
bool confirm_spsm_source(QWidget *parent, StudioAsset &asset)
{
  if (!is_spsm_archive_asset(asset)) return true;
  const auto prepared = prepared_spsm_source(asset);
  if ((!asset.material_preset&&!asset.smart_mask) || !prepared)
    throw std::runtime_error("SPSM source has not finished preparing");
  if (!spsm_confirmation_required(qEnvironmentVariableIntValue("BTS_SPSM_DEMO")==1,
      qEnvironmentVariableIntValue("BTS_SPSM_LEGACY_REPORT_DEMO")==1,asset.search_terms.contains("spsm:partial-accepted"))) return true;
  QDialog dialog(parent);
  dialog.setObjectName("spsmImportReport");
  dialog.setWindowTitle("SPSM import report");
  dialog.resize(760, 520);
  auto *layout = new QVBoxLayout(&dialog);
  auto *summary = new QLabel(spsm_source_partial(asset) ?
      "Native semantic import: unsupported subactions are reported independently. Review the report before importing. The complete original SPSM is preserved." :
      "Review the source interpretation and supported layers. The complete original SPSM is preserved.", &dialog);
  summary->setWordWrap(true);
  const auto &layers = prepared->prepared.material.set.layers;
  const auto visible = std::count_if(layers.begin(), layers.end(), [](const auto &layer) {
    return layer.kind != LayerKind::Folder && layer.visible;
  });
  auto *counts = new QLabel(QString("Enabled layers: %1. Parent visibility and masks can hide these layers. Original hierarchy entries retained: %2. Source appearance has not been verified against Painter.").arg(visible).arg(layers.size()), &dialog);
  counts->setObjectName("spsmSupportedCounts");counts->setWordWrap(true);
  layout->addWidget(summary);
  layout->addWidget(counts);
  auto *report = new QPlainTextEdit(spsm_source_report(asset), &dialog);
  report->setObjectName("spsmImportReportText");
  report->setReadOnly(true);
  report->setLineWrapMode(QPlainTextEdit::NoWrap);
  layout->addWidget(report);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(spsm_source_partial(asset) ? "Import supported layers" : "Import material");
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return false;
  asset.search_terms.append("spsm:partial-accepted");
  return true;
}

void show_spsm_source_report(QWidget *parent,const StudioAsset &asset)
{
  if(!is_spsm_archive_asset(asset))return;
  if(!prepared_spsm_source(asset)){
    prepare_spsm_application(parent,asset,[]{return true;},
      [parent](StudioAsset ready){show_spsm_source_report(parent,ready);},
      [](QString error){qWarning("SPSM inspection: %s",qPrintable(error));});return;
  }
  QDialog dialog(parent);dialog.setObjectName("spsmInspectReport");
  dialog.setWindowTitle("SPSM import report");dialog.resize(760,520);
  auto *layout=new QVBoxLayout(&dialog);
  auto *report=new QPlainTextEdit(spsm_source_report(asset),&dialog);
  report->setReadOnly(true);report->setLineWrapMode(QPlainTextEdit::NoWrap);layout->addWidget(report);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);layout->addWidget(buttons);
  QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);dialog.exec();
}

void prepare_spsm_application(QWidget *parent, StudioAsset source,
                              std::function<bool()> valid,
                              std::function<void(StudioAsset)> ready,
                              std::function<void(QString)> error,
                              std::function<bool(const StudioAsset &)> prepare,
                              std::function<void()> release)
{
  // The future's last owner waits only during application shutdown, never while
  // handling a stroke/property event. All parsing/IO/color transforms run here.
  auto job = std::make_shared<std::future<StudioAsset>>(std::async(std::launch::async,
      [source = std::move(source)]() mutable {
        hydrate_asset_presets(source);
        if (!source.material_preset&&!source.smart_mask) throw std::runtime_error("SPSM/SPMSK preparation was deferred on a worker");
        return source;
      }));
  struct Pending { std::optional<StudioAsset> source; bool approved = false; bool finished = false; };
  auto pending = std::make_shared<Pending>();
  auto *poll = new ApplicationPoll(parent);
  poll->setObjectName("spsmApplicationPoll");
  poll->cancel = [pending, release] {
    if (pending->finished) return;
    pending->finished = true;
    if (release) release();
  };
  poll->setInterval(16);
  QObject::connect(poll, &QTimer::timeout, parent,
      [job, pending, poll, parent, valid = std::move(valid), ready = std::move(ready),
       error = std::move(error), prepare = std::move(prepare), release = std::move(release)] {
    if (pending->finished) return;
    const auto finish = [&] {
      if (pending->finished) return;
      pending->finished = true; poll->stop(); poll->deleteLater(); if (release) release();
    };
    if (!pending->source) {
      if (job->wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
      try { pending->source = job->get(); }
      catch (const std::exception &exception) {
        finish(); error(QString::fromUtf8(exception.what())); return;
      }
    }
    // Avoid nested editing during another modal interaction. The report can
    // itself run an event loop: mark it approved before returning to polling.
    if (QApplication::activeModalWidget()) return;
    try {
      if (!valid()) {
        finish(); error("SPSM loading cancelled: project, texture set or selected layer changed"); return;
      }
      if (!pending->approved) {
        if (!confirm_spsm_source(parent, *pending->source)) {
          finish(); error("SPSM import cancelled; document unchanged"); return;
        }
        pending->approved = true;
        if (!valid()) {
          finish(); error("SPSM loading cancelled: destination changed while reviewing the source"); return;
        }
      }
      // prepare_texture_resources performs bounded GPU staging packets. Keep
      // the document untouched while uploads span frames, including on cancel.
      if (prepare && !prepare(*pending->source)) return;
      if (!valid()) {
        finish(); error("SPSM loading cancelled: destination changed during texture preparation"); return;
      }
      auto source = std::move(*pending->source);
      poll->stop(); ready(std::move(source)); finish();
    }
    catch (const std::exception &exception) { finish(); error(QString::fromUtf8(exception.what())); }
  });
  poll->start();
}
}
