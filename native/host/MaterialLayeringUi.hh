#pragma once
#include "MaterialLayeringPanel.hh"
#include "TextureLoadController.hh"
#include "MaterialLayerParameterImportJob.hh"
#include <QObject>
#include <QPointer>
class QBoxLayout;
class QComboBox;
class QLabel;
class QToolButton;
class QTimer;
class QProgressDialog;
namespace bts {
struct MaterialLayeringTarget {
  QUuid project, set;
  std::uint32_t uid = 0;
  std::uint64_t revision = 0;
  const TextureSet *texture = nullptr;  // Owner-call only; never retained by the UI.
  QString working_space, unavailable;
};
struct MaterialLayeringHooks {
  std::function<std::optional<MaterialLayeringTarget>()> current;
  std::function<void(const MaterialLayeringTarget &)> validate;
  std::function<void(const MaterialLayeringTarget &, bool, const std::array<MaterialValues, 4> &)>
      apply;
  std::function<bool(const MaterialLayeringTarget &,
                     bool,
                     const std::array<LayerMaterialInput, 4> &,
                     const std::vector<StudioAsset> &)>
      apply_inputs;
  std::function<void(const TextureLoadTarget &, const QString &, const std::string &)> load_file;
  std::function<void(const TextureLoadTarget &, const StudioAsset &, const std::string &)>
      load_asset;
  std::function<void(const TextureLoadTarget &)> cancel_load;
  std::function<void(const FillTexture &)> validate_source;
  std::function<void()> shelf, release;
  std::function<void(const MaterialLayeringTarget &, int)> select;
  std::function<void(const QString &)> error;
  std::function<void(const MaterialLayeringTarget &, const QString &)> begin_parameter_export;
  std::function<bool(bool current)> poll_parameter_export;
  std::function<void()> cancel_parameter_export;
  std::function<void(const MaterialLayeringTarget &, const QString &)> begin_parameter_import;
  std::function<MaterialParameterImportStatus()> parameter_import_status;
  std::function<void()> cancel_parameter_import;
};
class MaterialLayeringUi final : public QObject {
 public:
  MaterialLayeringUi(QWidget &owner, QBoxLayout &layers, MaterialLayeringHooks);
  ~MaterialLayeringUi() override;
  void synchronize();
  bool load_target_current(const TextureLoadTarget &) const;
  void texture_loaded(LoadedTexture);
  void texture_status(const TextureLoadTarget &, const TextureLoadStatus &);
  bool consume_shelf(const StudioAsset &);

 private:
  QWidget &owner_;
  MaterialLayeringHooks hooks_;
  QWidget *bar_;
  QComboBox *stack_;
  QLabel *note_;
  QToolButton *settings_, *export_button_, *import_button_;
  QPointer<MaterialLayeringPanel> dialog_;
  QPointer<QProgressDialog> import_progress_;
  QTimer *import_timer_;
  struct Export {
    MaterialLayeringTarget target;
    QString directory, error;
    bool cancelled = false;
    QPointer<QProgressDialog> progress;
  };
  std::optional<Export> export_;
  QTimer *export_timer_;
  QUuid buffer_;
  std::optional<MaterialLayeringTarget> target_;
  std::vector<StudioAsset> sources_;
  struct Shelf {
    TextureLoadTarget target;
    std::string space;
  };
  std::optional<Shelf> shelf_;
  bool shelf_cancelled_ = false, preparing_ = false;
  void open();
  void fail(const QString &);
  void clear_buffer();
  void export_parameters();
  void poll_export();
  void import_parameters();
  void poll_import();
  TextureLoadTarget load_target(int input, int channel) const;
  MaterialLayeringTarget current_target() const;
};
}  // namespace bts
