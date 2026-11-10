// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureIoAwait.hh"
#include <QApplication>
#include <cassert>
#include <thread>
#include <iostream>
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  QWidget parent;
  parent.show();
  // QProgressDialog::closeEvent emits canceled even on a successful finish.
  const auto result = bts::await_texture_io(
      &parent, "Successful import", [](const std::atomic_bool &cancel) {
        assert(!cancel);
        return 42;
      });
  assert(result == 42);
  QTimer::singleShot(30, &parent, [&] {
    auto *progress = parent.findChild<QProgressDialog *>("textureIoProgress");
    assert(progress);
    progress->close();
  });
  bool cancelled = false;
  try {
    bts::await_texture_io(&parent, "Cancelled import", [](const std::atomic_bool &cancel) {
      while (!cancel.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return 0;
    });
  }
  catch (const std::runtime_error &error) {
    cancelled = std::string(error.what()) == "Texture I/O cancelled";
  }
  assert(cancelled);
  bool failed = false;
  try {
    bts::await_texture_io(&parent, "Failed import", [](const std::atomic_bool &) -> int {
      throw std::runtime_error("corrupt image");
    });
  }
  catch (const std::runtime_error &error) {
    failed = std::string(error.what()) == "corrupt image";
  }
  assert(failed);
  std::cout << "Texture I/O success, user cancellation and worker error passed\n";
}
