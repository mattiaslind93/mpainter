// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
#include <QByteArray>
namespace bts {
QByteArray write_skew_archive(const TextureDocument &);
void read_skew_archive(TextureDocument &, const QByteArray &);
}  // namespace bts
