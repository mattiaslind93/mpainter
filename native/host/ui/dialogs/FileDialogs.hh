// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFileDialog>
class QListWidget;
class QShowEvent;
class QHideEvent;
namespace bts {
// All product choosers deliberately use Qt widgets on every platform.
class FileDialog : public QFileDialog {
 public:
  explicit FileDialog(QWidget *parent = nullptr,
                      const QString &caption = {},
                      const QString &directory = {},
                      const QString &filter = {});
  ~FileDialog() override;
  static QString getOpenFileName(QWidget *parent = nullptr,
                                 const QString &caption = {},
                                 const QString &directory = {},
                                 const QString &filter = {},
                                 QString *selected_filter = nullptr,
                                 Options options = {});
  static QStringList getOpenFileNames(QWidget *parent = nullptr,
                                      const QString &caption = {},
                                      const QString &directory = {},
                                      const QString &filter = {},
                                      QString *selected_filter = nullptr,
                                      Options options = {});
  static QString getSaveFileName(QWidget *parent = nullptr,
                                 const QString &caption = {},
                                 const QString &directory = {},
                                 const QString &filter = {},
                                 QString *selected_filter = nullptr,
                                 Options options = {});
  static QString getExistingDirectory(QWidget *parent = nullptr,
                                      const QString &caption = {},
                                      const QString &directory = {},
                                      Options options = ShowDirsOnly);
  static void remember_folder(const QString &);
  // Optional bookkeeping must not delay a durable project save.
  static void remember_folder_async(const QString &);
  // Finishes idle directory threads before a chooser can close on the UI thread.
  static void prepare_directory_thread();
  // Reuse one hidden chooser to avoid repeated model/widget initialization.
  // The pinned Qt patch lets this subclass own asynchronous state persistence.
  static FileDialog *borrow(QWidget *parent,
                            const QString &caption = {},
                            const QString &directory = {},
                            const QString &filter = {});
  static QStringList recent_folders();
  static QStringList pinned_folders();

 private:
  QListWidget *pinned_ = nullptr, *recent_ = nullptr;
  bool state_published_ = false;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void publish_state();
  void refresh_folders();
  void relax_model();
  // QFileSystemModel joins its directory thread on the destroying thread.
  // Swap in a thread that has already exited, and join the busy one on a worker.
  void release_directory_listing();
  void pin_folder(const QString &);
  void unpin_folder();
  void move_pin(int);
  void save_pins();
};
}  // namespace bts
