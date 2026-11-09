#pragma once
#include "AssetCatalog.hh"
#include "BuiltinBrushPresets.hh"
#include <QInputDialog>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <future>
namespace bts {
// Snapshot preparation and disk encoding run on a worker; only validated opaque
// tokens and immutable COW settings are published on the UI thread.
class BrushPresetUi final : public QObject {
  QWidget *owner_;
  AssetCatalog &catalog_;
  std::function<BrushSettings()> current_;
  std::function<void(const BrushSettings &)> apply_;
  std::function<void()> refresh_;
  std::function<void(QString)> status_;
  QUuid active_;
  std::uint64_t serial_ = 0;
  const StudioAsset &asset(const QUuid &id) const
  {
    for (const auto &a : catalog_.assets())
      if (a.id == id)
        return a;
    throw std::invalid_argument("Brush preset is no longer available");
  }
  QString name(const QString &initial)
  {
    QInputDialog dialog(owner_);
    dialog.setObjectName("brushPresetNameDialog");
    dialog.setWindowTitle("Save brush preset");
    dialog.setLabelText("Preset name");
    dialog.setTextValue(initial);
    if (dialog.exec() != QDialog::Accepted)
      return {};
    const auto text = dialog.textValue().trimmed();
    if (text.isEmpty() || text.contains(QChar(0)))
      throw std::invalid_argument("Enter a brush preset name");
    return text;
  }
  void publish(QString label, QUuid id = {})
  {
    const auto settings = current_();
    QString title;
    StudioAsset source;
    QString expected;
    std::uint64_t revision = 0;
    if (id.isNull()) {
      title = name("My Brush");
      if (title.isEmpty())
        return;
      source.name = title;
      source.id = QUuid::createUuid();
      source.filename = "brush-" + source.id.toString(QUuid::WithoutBraces) + ".btsbrush";
      source.scope = AssetScope::User;
      source.usages = {"brush"};
    }
    else {
      source = asset(id);
      if (source.scope != AssetScope::User || !source.brush_preset)
        throw std::invalid_argument("Save a User copy before updating a built-in brush");
      expected = source.hash();
      revision = source.revision;
      title = source.name;
    }
    const auto ticket = ++serial_;
    const auto directory = catalog_.directory();
    status_(label + "…");
    owner_->setProperty("brushPresetSaving", true);
    auto job = std::make_shared<std::future<PreparedUserAsset>>(std::async(
        std::launch::async, [source = std::move(source), settings, title, directory]() mutable {
          source.bytes = write_brush_preset({title, settings});
          source.path.clear();
          source.brush_preset.reset();
          return AssetCatalog::prepare_user(std::move(source), directory);
        }));
    auto *timer = new QTimer(this);
    timer->setInterval(16);
    connect(timer, &QTimer::timeout, this, [this, timer, job, id, expected, revision, ticket] {
      if (job->wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
      timer->stop();
      timer->deleteLater();
      try {
        auto prepared = job->get();
        if (ticket != serial_)
          return;
        if (id.isNull())
          catalog_.add_prepared(std::move(prepared));
        else if (!catalog_.replace_prepared(std::move(prepared), expected, revision))
          throw std::runtime_error("Brush changed while saving; the newer resource was retained");
        refresh_();
        status_("Brush preset saved");
      }
      catch (const std::exception &e) {
        status_(QString::fromUtf8(e.what()));
      }
      if (ticket == serial_)
        owner_->setProperty("brushPresetSaving", false);
    });
    timer->start();
  }

 public:
  BrushPresetUi(QWidget *owner,
                AssetCatalog &catalog,
                std::function<BrushSettings()> current,
                std::function<void(const BrushSettings &)> apply,
                std::function<void()> refresh,
                std::function<void(QString)> status)
      : QObject(owner),
        owner_(owner),
        catalog_(catalog),
        current_(std::move(current)),
        apply_(std::move(apply)),
        refresh_(std::move(refresh)),
        status_(std::move(status))
  {
  }
  void save()
  {
    try {
      publish("Saving brush preset");
    }
    catch (const std::exception &e) {
      status_(QString::fromUtf8(e.what()));
    }
  }
  void action(const QString &operation, const QUuid &id)
  {
    try {
      if (operation == "create") {
        save();
        return;
      }
      const auto a = asset(id);
      if (operation == "update") {
        publish("Updating brush preset", id);
        return;
      }
      if (operation == "rename") {
        const auto title = name(a.name);
        if (!title.isEmpty()) {
          catalog_.rename(id, title);
          refresh_();
        }
        return;
      }
      if (operation == "delete") {
        if (a.scope != AssetScope::User)
          throw std::invalid_argument("Only User brush presets can be deleted");
        QMessageBox dialog(
            QMessageBox::Question,
            "Delete brush preset",
            QString("Delete “%1” from the User shelf? Existing projects retain their copies.")
                .arg(a.name),
            QMessageBox::Yes | QMessageBox::Cancel,
            owner_);
        dialog.setObjectName("deleteBrushPresetDialog");
        if (dialog.exec() != QMessageBox::Yes)
          return;
        if (!catalog_.remove_user(id, a.hash(), a.revision))
          throw std::runtime_error("Brush changed before deletion");
        if (active_ == id)
          active_ = QUuid();
        refresh_();
        return;
      }
    }
    catch (const std::exception &e) {
      status_(QString::fromUtf8(e.what()));
    }
  }
  bool select(const StudioAsset &a)
  {
    if (!a.usages.contains("brush"))
      return false;
    try {
      if (!a.brush_preset)
        throw std::invalid_argument("Brush preset is not prepared");
      apply_(a.scope == AssetScope::Builtin ? apply_stock_brush(current_(), *a.brush_preset) :
                                              *a.brush_preset);
      active_ = a.id;
      status_("Brush: " + a.name);
    }
    catch (const std::exception &e) {
      status_(QString::fromUtf8(e.what()));
    }
    return true;
  }
};
}  // namespace bts
