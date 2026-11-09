// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QEventLoop>
#include <QProgressDialog>
#include <QTimer>
#include <atomic>
#include <future>
namespace bts {
// Synchronous scripting API contract, asynchronous private I/O. The modal
// progress window prevents a script candidate becoming stale while Qt continues
// processing repaint/timer events. Ordinary channel picking uses the nonmodal
// TextureLoadController and keeps the old channel editable until install.
template<class Work> auto await_texture_io(QWidget *parent, const QString &label, Work work)
{
  auto cancel = std::make_shared<std::atomic_bool>(false);
  auto future = std::async(std::launch::async,
                           [work = std::move(work), cancel] { return work(*cancel); });
  QProgressDialog progress(label, "Cancel", 0, 0, parent);
  progress.setObjectName("textureIoProgress");
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.setAutoClose(false);
  QEventLoop loop;
  QTimer poll;
  poll.setInterval(16);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
      loop.quit();
  });
  const auto cancellation = QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&] {
    cancel->store(true);
    progress.setLabelText("Cancelling…");
    progress.show();
  });
  progress.show();
  poll.start();
  loop.exec();
  poll.stop();
  if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    cancel->store(true);
  auto result = future.get();
  QObject::disconnect(cancellation);
  progress.close();
  if (cancel->load())
    throw std::runtime_error("Texture I/O cancelled");
  return result;
}
}  // namespace bts
