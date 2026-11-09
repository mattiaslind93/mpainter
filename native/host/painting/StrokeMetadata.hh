// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "StrokeHistory.hh"
#include <QJsonValue>
namespace bts {
QJsonValue stroke_history_json(const std::shared_ptr<const StrokeHistory> &, const StrokeRaster &);
std::shared_ptr<const StrokeHistory> read_stroke_history_json(const QJsonValue &);
}  // namespace bts
