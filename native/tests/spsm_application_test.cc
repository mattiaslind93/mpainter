// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmImportUi.hh"
#include "SpsmSource.hh"
#include "SpsmAssetDispatch.hh"
#include <atomic>
#include <thread>
#include <chrono>
#include <QElapsedTimer>
#include <QApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <cassert>
#include <iostream>
// This timer/cancellation boundary test uses an already prepared native recipe;
// source parsing/report semantics have independent real-file integration tests.
namespace bts {
std::shared_ptr<const PreparedSpsmSource> prepared_spsm_source(const StudioAsset &)
{
  return {};
}
QString spsm_source_report(const StudioAsset &)
{
  return {};
}
bool spsm_source_partial(const StudioAsset &)
{
  return true;
}
}  // namespace bts
using namespace bts;
int main(int argc, char **argv)
{
  QApplication application(argc, argv);
  assert(!spsm_confirmation_required(false, false, false));
  assert(spsm_confirmation_required(true, false, false));
  assert(spsm_confirmation_required(false, true, false));
  assert(!spsm_confirmation_required(true, true, true));
  enum class Case { Ready, Cancel, PrepareError, InstallError, WorkerError };
  for (const auto test :
       {Case::Ready, Case::Cancel, Case::PrepareError, Case::InstallError, Case::WorkerError})
  {
    QWidget parent;
    QEventLoop loop;
    bool valid = true, wrong_thread = false, timed_out = false;
    int packets = 0, installed = 0, errors = 0, released = 0, pings = 0;
    StudioAsset source;
    source.filename = "already-prepared.native";
    if (test != Case::WorkerError)
      source.material_preset = std::make_shared<const SmartMaterialPreset>();
    const auto gui = QThread::currentThread();
    QTimer ping;
    ping.setInterval(1);
    QObject::connect(&ping, &QTimer::timeout, &loop, [&] { ++pings; });
    ping.start();
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&] {
      timed_out = true;
      loop.quit();
    });
    deadline.start(2000);
    prepare_spsm_application(
        &parent,
        std::move(source),
        [&] { return valid; },
        [&](StudioAsset ready) {
          wrong_thread |= QThread::currentThread() != gui;
          assert(packets == 3 && ready.material_preset && installed == 0);
          if (test == Case::InstallError)
            throw std::runtime_error("install rejected");
          ++installed;
          loop.quit();
        },
        [&](QString message) {
          assert(!message.isEmpty());
          ++errors;
          loop.quit();
        },
        [&](const StudioAsset &ready) {
          wrong_thread |= QThread::currentThread() != gui;
          assert(ready.material_preset && installed == 0 && released == 0);
          ++packets;
          if (test == Case::PrepareError)
            throw std::runtime_error("GPU allocation failed");
          if (test == Case::Cancel && packets == 2)
            valid = false;
          return packets == 3;
        },
        [&] { ++released; });
    loop.exec();
    assert(!timed_out && !wrong_thread && released == 1 && pings > 0);
    if (test == Case::Ready)
      assert(installed == 1 && errors == 0 && packets == 3);
    else
      assert(installed == 0 && errors == 1);
    if (test == Case::Cancel)
      assert(packets == 2);
    if (test == Case::PrepareError)
      assert(packets == 1);
    if (test == Case::WorkerError)
      assert(packets == 0);
    // Deferred deletion and another event-loop turn must not release twice.
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    assert(released == 1);
  }
  {
    QWidget parent;
    std::atomic_bool entered = false, unblock = false, finished = false, globals_alive = true,
                     observed_alive = false;
    int delivered = 0, errors = 0, releases = 0;
    set_spsm_asset_hydrator([&](StudioAsset &asset) {
      entered = true;
      while (!unblock.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      observed_alive = globals_alive.load();
      asset.material_preset = std::make_shared<const SmartMaterialPreset>();
      finished = true;
      return true;
    });
    StudioAsset source;
    source.filename = "pending.spsm";
    source.usages = {"smartmaterial"};
    prepare_spsm_application(
        &parent,
        std::move(source),
        [] { return true; },
        [&](StudioAsset) { ++delivered; },
        [&](QString) { ++errors; },
        {},
        [&] { ++releases; });
    QElapsedTimer deadline;
    deadline.start();
    while (!entered.load() && deadline.elapsed() < 2000)
      QApplication::processEvents();
    assert(entered.load());
    std::thread complete([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      unblock = true;
    });
    finish_spsm_applications(&parent);
    globals_alive = false;
    complete.join();
    assert(finished && observed_alive && delivered == 0 && errors == 0 && releases == 1);
    assert(parent.findChildren<QTimer *>("spsmApplicationPoll").empty());
    finish_spsm_applications(&parent);
    assert(releases == 1);
    set_spsm_asset_hydrator({});
  }
  std::cout
      << "SPSM frame preparation: atomic install/cancel, worker/GPU/install failures, main-thread packets, responsive timer and one release PASS\n";
}
