// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
class QString;
namespace bts {
// Diagnostic only. Caller must own a current Blender GPU context.
QJsonObject verify_native_fragment_bump(const QString &directory);
}  // namespace bts
