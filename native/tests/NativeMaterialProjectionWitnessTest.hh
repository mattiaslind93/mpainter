// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialProjectionWitness.hh"
#include <QTemporaryDir>
#include <QFile>
#include <cassert>
inline void verify_projection_resource_witness()
{
  using namespace bts;
  using projection_ui_proof::resource_witness;
  QTemporaryDir directory;
  assert(directory.isValid());
  const QByteArray bytes("retained original image bytes");
  const auto write = [&](const QString &path, const QByteArray &data) {
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(data) == data.size());
  };
  StudioAsset byte_source;
  byte_source.filename = "source.bin";
  byte_source.bytes = bytes;
  const auto byte_record = resource_witness(byte_source);
  assert(byte_record["bytes"].toInteger() == bytes.size());
  assert(byte_record["sha256"].toString() ==
         QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
  StudioAsset disk;
  disk.filename = "original.png";
  const auto first = directory.filePath("warm-cache"), second = directory.filePath("cold-cache");
  write(first, bytes);
  write(second, bytes);
  disk.texture_files = {{0, "original.png", first, hash_texture_file(first), bytes.size()}};
  const auto expected = resource_witness(disk);
  assert(expected["bytes"].toInteger() == bytes.size());
  assert(!expected["texture_files"].toArray()[0].toObject().contains("path"));
  disk.texture_files[0].path = second;
  assert(resource_witness(disk) == expected);
  const auto rejects = [&] {
    bool rejected = false;
    try {
      resource_witness(disk);
    }
    catch (const std::exception &) {
      rejected = true;
    }
    assert(rejected);
  };
  auto changed = bytes;
  changed[0] = 'X';
  write(second, changed);
  rejects();
  write(second, bytes.left(bytes.size() - 1));
  rejects();
  assert(QFile::remove(second));
  rejects();
  byte_source.bytes.clear();
  bool rejected = false;
  try {
    resource_witness(byte_source);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected);
}
