// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackedFileLimit.hh"
#include "ResourceAvailability.hh"
#include "SkewIO.hh"
#include "SkewArchive.hh"
#include <QCryptographicHash>
#include "BKE_idprop.hh"
#include "BKE_image.hh"
#include "BLI_string.h"
#include "DNA_ID.h"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_scene_types.h"
#include "MEM_guardedalloc.h"
namespace bts {
using namespace blender;
void pack_skew_fields(Main &main, Scene &scene, const TextureDocument &document)
{
  const auto bytes = write_skew_archive(document);
  auto *root = IDP_EnsureProperties(&scene.id);
  auto *reference = IDP_GetPropertyFromGroup(root, "bts_skew_data");
  auto *id = reference && reference->type == IDP_ID ? IDP_ID_get(reference) : nullptr;
  Image *image = id && GS(id->name) == ID_IM ? reinterpret_cast<Image *>(id) : nullptr;
  if (!image) {
    const float clear[4] = {0, 0, 0, 0};
    image = BKE_image_add_generated(&main,
                                    1,
                                    1,
                                    "BTS Skew Correction (data)",
                                    32,
                                    1,
                                    IMA_GENTYPE_BLANK,
                                    clear,
                                    false,
                                    true,
                                    false);
    if (!image)
      throw std::runtime_error("Cannot allocate skew correction resource");
    BKE_image_free_buffers(image);
    image->source = IMA_SRC_FILE;
    STRNCPY(image->filepath, "//bts_data/skew.bin");
  }
  require_packed_file_size(bytes.size(), "Skew archive");
  BKE_image_free_packedfiles(image);
  auto *data = MEM_new_array_uninitialized<char>(bytes.size(), __func__);
  std::copy_n(bytes.constData(), bytes.size(), data);
  BKE_image_packfiles_from_mem(nullptr, image, data, bytes.size());
  if (image->packedfiles.is_empty())
    throw std::runtime_error("Cannot pack skew correction");
  IDPropertyTemplate value{};
  value.id = &image->id;
  IDP_ReplaceInGroup(root, IDP_New(IDP_ID, &value, "bts_skew_data"));
  const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  IDP_ReplaceInGroup(root, IDP_NewString(hash.constData(), "bts_skew_sha256"));
}
void read_skew_fields(const Scene &scene, TextureDocument &document, bool required)
{
  auto *root = scene.id.properties;
  const auto *reference = root ? IDP_GetPropertyFromGroup(root, "bts_skew_data") : nullptr;
  if (!required && !reference)
    return;
  auto *id = reference && reference->type == IDP_ID ? IDP_ID_get(reference) : nullptr;
  const auto *checksum = root ? IDP_GetPropertyFromGroup(root, "bts_skew_sha256") : nullptr;
  if (!id || GS(id->name) != ID_IM || !checksum || checksum->type != IDP_STRING)
    throw std::runtime_error("Skew correction resource is missing");
  const auto *image = reinterpret_cast<const Image *>(id);
  const auto *file = static_cast<const ImagePackedFile *>(image->packedfiles.first);
  if (!file || file->next || !file->packedfile || file->packedfile->size < 4)
    throw std::runtime_error("Skew correction resource is invalid");
  const QByteArray bytes(static_cast<const char *>(file->packedfile->data),
                         file->packedfile->size);
  if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() !=
      QByteArray(IDP_string_get(checksum)))
    throw std::runtime_error("Skew correction checksum mismatch");
  read_skew_archive(document, bytes);
}
}  // namespace bts
