#pragma once
#include <QtCore>
#include <cstring>
#include "PackedFileLimit.hh"
#include "BKE_image.hh"
#include "BKE_packedFile.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "MEM_guardedalloc.h"
namespace bts {
// Encode before narrowing to PackedFile::size. BKE_image_memorypack narrows
// internally, so checking its result afterwards is too late.
inline bool safe_image_memorypack(blender::Image *image)
{
  using namespace blender;
  if (BKE_image_is_multiview(image))
    throw std::runtime_error("Cannot safely pack multiview project image");
  BKE_image_free_packedfiles(image);
  for (ImageTile &tile : image->tiles) {
    ImageUser user{};
    BKE_imageuser_default(&user);
    user.tile = tile.tile_number;
    auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
    if (!buffer)
      return false;
    try {
      buffer->ftype = buffer->float_data() ? IMB_FTYPE_OPENEXR : IMB_FTYPE_PNG;
      const auto encoded = IMB_save_image_to_buffer(buffer, ImBufFlags::ByteData);
      if (encoded.is_empty()) {
        BKE_image_release_ibuf(image, buffer, nullptr);
        return false;
      }
      require_packed_file_size(encoded.size(), "Packed project image");
      auto *data = MEM_new_array_uninitialized<char>(encoded.size(), __func__);
      std::memcpy(data, encoded.data(), encoded.size());
      auto *packed = MEM_new<ImagePackedFile>(__func__);
      packed->packedfile = BKE_packedfile_new_from_memory(data, int(encoded.size()));
      packed->tile_number = tile.tile_number;
      packed->view = 0;
      BKE_image_user_file_path(&user, image, packed->filepath);
      BLI_addtail(&image->packedfiles, packed);
      buffer->userflags &= ~IB_BITMAPDIRTY;
    }
    catch (...) {
      BKE_image_release_ibuf(image, buffer, nullptr);
      throw;
    }
    BKE_image_release_ibuf(image, buffer, nullptr);
  }
  if (image->source == IMA_SRC_GENERATED) {
    image->source = IMA_SRC_FILE;
    image->type = IMA_TYPE_IMAGE;
  }
  for (ImageTile &tile : image->tiles)
    tile.gen_flag &= ~IMA_GEN_TILE;
  BKE_image_clear_autosave(image);
  return true;
}
}  // namespace bts
