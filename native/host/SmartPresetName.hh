// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceAvailability.hh"
#include <QString>
#include <stdexcept>
namespace bts {
inline QString smart_preset_name(const QString &text)
{

  const auto name = text.trimmed();
  if (name.isEmpty() || name.contains(QChar::Null))
    throw std::invalid_argument("Choose a nonempty smart-preset name without NUL characters");
  return name;
}
}  // namespace bts
