// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include "StrokeMetadata.hh"
#include "HistoryMemory.hh"
#include <QByteArray>
namespace bts {
QJsonValue stroke_history_json(const std::shared_ptr<const StrokeHistory> &, const StrokeRaster &);
std::shared_ptr<const StrokeHistory> read_stroke_history_json(const QJsonValue &);
bool limit_stroke_history(TextureDocument &, std::optional<std::uint64_t> maximum = std::nullopt);
QByteArray write_stroke_archive(const TextureDocument &);
void read_stroke_archive(TextureDocument &, const QByteArray &);
}  // namespace bts
