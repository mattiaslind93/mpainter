// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The release build sets MPAINTER_WITH_HARNESS=0 and does not compile native/harness.
// These are the same entry points main() calls, and they register and run nothing.
#include <optional>

#include <QCommandLineParser>
#include <QString>
#include <QStringList>

namespace bts {
class BlenderHost;
}

namespace bts::harness {

inline void add_harness_options(QCommandLineParser &) {}

inline std::optional<int> run_pre_host_test(const QCommandLineParser &)
{
  return std::nullopt;
}

inline std::optional<int> run_host_test(bts::BlenderHost &, const QCommandLineParser &)
{
  return std::nullopt;
}

class HarnessContext {
 public:
  explicit HarnessContext(const QCommandLineParser &) {}
  HarnessContext(const HarnessContext &) = delete;
  HarnessContext &operator=(const HarnessContext &) = delete;

  // Always false, so a harness branch in main() does not run.
  bool is_set(const QString &) const
  {
    return false;
  }
  QString value(const QString &) const
  {
    return QString();
  }
  QStringList values(const QString &) const
  {
    return {};
  }
};

}  // namespace bts::harness
