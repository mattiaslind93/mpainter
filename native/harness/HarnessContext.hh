// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QCommandLineParser>
#include <QString>
#include <QStringList>

namespace bts::harness {

// main()'s one view of the harness command line. Every read of an option that
// add_harness_options() registers goes through here; product options stay on the parser.
// Design and the path to MPAINTER_WITH_HARNESS: docs/architecture/harness-context.md.
class HarnessContext {
 public:
  explicit HarnessContext(const QCommandLineParser &parser) : parser_(parser) {}
  HarnessContext(const HarnessContext &) = delete;
  HarnessContext &operator=(const HarnessContext &) = delete;

  bool is_set(const QString &option) const { return parser_.isSet(option); }
  QString value(const QString &option) const { return parser_.value(option); }
  QStringList values(const QString &option) const { return parser_.values(option); }

 private:
  const QCommandLineParser &parser_;
};

}  // namespace bts::harness
