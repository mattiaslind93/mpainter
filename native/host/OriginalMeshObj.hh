// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SceneSnapshot.hh"
#include <QByteArray>
#include <QString>
namespace bts {
// Deterministic OBJ from the pre-preview source snapshot; never from viewport modifiers.
// The snapshot is triangulated; full mesh-export controls are a separate feature (PD-134).
inline QByteArray original_snapshot_obj(const SceneSnapshot &source)
{
  QByteArray result = "# BTS source geometry without displacement or viewport tessellation\n";
  int index = 1;
  const auto number = [](float value) { return QString::number(value, 'g', 9).toLatin1(); };
  for (const auto &triangle : source.uv_triangles) {
    for (const auto &point : triangle.positions) {
      result += "v";
      for (float value : point) {
        result += ' ';
        result += number(value);
      }
      result += '\n';
    }
    for (const auto &point : triangle.corners) {
      result += "vt " + number(point[0]) + " " + number(point[1]) + "\n";
    }
    result += "f";
    for (int corner = 0; corner < 3; ++corner) {
      const auto vertex = QByteArray::number(index + corner);
      result += " " + vertex + "/" + vertex;
    }
    result += '\n';
    index += 3;
  }
  return result;
}
}  // namespace bts
