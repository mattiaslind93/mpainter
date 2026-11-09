// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#pragma once
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <stdexcept>
namespace bts {
inline QString validated_material_library_path(const QString &path)
{
  const QFileInfo info(path.trimmed());
  if (!info.isAbsolute() || !info.isReadable() ||
      (!info.isDir() &&
       !(info.isFile() && info.suffix().compare("blend", Qt::CaseInsensitive) == 0)))
    throw std::invalid_argument("Choose a readable absolute folder or .blend file");
  return info.canonicalFilePath();
}
inline QStringList material_library_paths()
{
  QSettings settings("mPainter", "Preferences");
  const auto saved = settings.value("Materials/libraryPaths").toStringList();

  const ResourceAvailability text_memory;
  std::uint64_t text_units = 0;
  QStringList result;
  // Missing/offline libraries stay visible and removable in Preferences.
  for (const auto &path : saved)
    if (QFileInfo(path).isAbsolute() && !path.contains(QChar::Null) && !result.contains(path)) {
      text_units = resource_sum(text_units, path.size());
      result.append(path);
    }
  return result;
}
inline void save_material_library_paths(const QStringList &paths)
{

  const ResourceAvailability text_memory;
  std::uint64_t text_units = 0;
  QStringList unique;
  for (const auto &path : paths) {
    if (!QFileInfo(path).isAbsolute() || path.contains(QChar::Null))
      throw std::invalid_argument("Invalid material library path");
    if (!unique.contains(path)) {
      text_units = resource_sum(text_units, path.size());
      unique.append(path);
    }
  }
  QSettings settings("mPainter", "Preferences");
  settings.setValue("Materials/libraryPaths", unique);
  settings.sync();
  if (settings.status() != QSettings::NoError)
    throw std::runtime_error("Could not save material library preferences");
}
}  // namespace bts
