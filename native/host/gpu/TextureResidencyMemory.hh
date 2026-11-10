// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFile>
#include <cstddef>
namespace bts {
// mincore reports file-cache residency even after MADV_DONTNEED has removed
// this process's pages. smaps RSS measures the mappings we actually released.
inline std::size_t history_mapping_rss()
{
  QFile file("/proc/self/smaps");
  if (!file.open(QIODevice::ReadOnly))
    return 0;
  std::size_t bytes = 0;
  bool history = false;
  for (const auto &line : file.readAll().split('\n')) {
    const auto first = line.left(line.indexOf(' '));
    if (first.contains('-'))
      history = line.contains("/history-pages/");
    else if (history && line.startsWith("Rss:"))
      bytes += line.simplified().split(' ').at(1).toULongLong() * 1024;
  }
  return bytes;
}
}  // namespace bts
