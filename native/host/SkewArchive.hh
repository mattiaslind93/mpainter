#pragma once
#include "TextureDocument.hh"
#include <QByteArray>
namespace bts {
QByteArray write_skew_archive(const TextureDocument &);
void read_skew_archive(TextureDocument &,const QByteArray &);
}
