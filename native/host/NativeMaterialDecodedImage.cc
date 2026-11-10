// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialDecodedImage.hh"
#include "NativeMaterialImageSource.hh"
#include "ResourceAvailability.hh"
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BLI_listbase.h"
#include "BLI_path_utils.hh"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include "MEM_guardedalloc.h"
#include <QCoreApplication>
#include <QThread>
#include <QJsonObject>
#include <algorithm>
#include <memory>
namespace bts {
std::string native_material_image_source_error(const blender::Image &image,
                                               std::string_view resolved,
                                               std::string_view reason,
                                               bool packed)
{
  return "Image \"" + std::string(image.id.name + 2) + "\" (reference \"" +
         std::string(image.filepath) + "\", resolved \"" + std::string(resolved) + "\", " +
         (packed ? "packed" : "unpacked") + "): " + std::string(reason);
}
NativeMaterialDecodedImage decode_native_material_image_tile(const blender::Main &main,
                                                             const blender::Image &source,
                                                             int tile,
                                                             const std::atomic_bool &cancel)
{
  using namespace blender;
  auto *image = &source;
  if (auto *app = QCoreApplication::instance(); app && QThread::currentThread() == app->thread())
    throw std::logic_error("Native images must be decoded on a worker");
  if (cancel.load())
    throw std::runtime_error("Native image loading cancelled");
  if ((image->source != IMA_SRC_FILE && image->source != IMA_SRC_TILED) ||
      image->type != IMA_TYPE_IMAGE)
    throw std::runtime_error(
        "Named-space native decoding requires an ordinary FILE or TILED image source");
  // Read literal source components. Image alpha_mode remains untouched and
  // still controls the native shader. In particular, retain straight RGB at
  // alpha zero instead of premultiplying/unpremultiplying it on the worker.
  const auto alpha = ImBufFlags::AlphaChannelPacked;
  char path[FILE_MAX]{}, space[IM_MAX_SPACE]{};
  STRNCPY(space, image->colorspace_settings.name);
  const auto selected = native_material_encoded_image_source(main, *image, tile);
  STRNCPY(path, selected.path.toUtf8().constData());
  const auto *packed = selected.packed;
  const auto fail = [&](std::string_view reason) -> void {
    throw std::runtime_error(native_material_image_source_error(
        *image,
        path,
        tile ? std::string("tile ") + std::to_string(tile) + ": " + std::string(reason) :
               std::string(reason),
        packed != nullptr));
  };
  // Legacy Filmic's sRGB OETF is standard sRGB/Rec.709, now named sRGB.
  // Resolve only this historical name; retain the authored Image declaration.
  // https://github.com/sobotka/filmic-blender/blob/master/config.ocio
  const QString authored_space = QString::fromUtf8(space);
  if (space[0] && IMB_colormanagement_colorspace_get_named_index(space) < 0) {
    if (STREQ(space, "sRGB OETF") && IMB_colormanagement_colorspace_get_named_index("sRGB") >= 0)
      STRNCPY(space, "sRGB");
    else
      fail(std::string("Unknown authored native image color space: ") + space);
  }
  const QString resolved_space = QString::fromUtf8(space);
  using Buffer = std::unique_ptr<ImBuf, decltype(&IMB_freeImBuf)>;
  const auto read = [&](ImBufFlags flags) {
    return Buffer(packed ?
                      IMB_load_image_from_memory(static_cast<const unsigned char *>(packed->data),
                                                 packed->size,
                                                 flags,
                                                 image->id.name + 2,
                                                 path,
                                                 space) :
                      IMB_load_image_from_filepath(path, flags, space),
                  IMB_freeImBuf);
  };
  {
    auto header = read(ImBufFlags::Test | ImBufFlags::ByteData | ImBufFlags::NoColorspaceConvert |
                       alpha);
    if (!header || header->x <= 0 || header->y <= 0)
      fail("Cannot read native image dimensions");
    // A decoder may retain both RGBA byte (4) and float (16) planes while
    // normalization allocates another RGBA float plane (16): 36 bytes/texel.
  }
  // NoColorspaceConvert leaves an explicit actual float-buffer space even for
  // EXR. Ordinary BKE decoding may already convert float data and clear its tag;
  // using the original file tag on that buffer would apply a second transform.
  auto raw = read(ImBufFlags::ByteData | ImBufFlags::FloatData | ImBufFlags::NoColorspaceConvert |
                  alpha);
  if (!raw || (!raw->float_data() && !raw->byte_data()))
    fail("Cannot decode native image");

  const bool floating = raw->float_data() != nullptr;
  const auto *actual_space = floating ? raw->float_buffer.colorspace : raw->byte_buffer.colorspace;
  if (!actual_space)
    throw std::runtime_error("Native image has no explicit decoded color space");
  const char *from = IMB_colormanagement_colorspace_get_name(actual_space);
  const bool data = IMB_colormanagement_space_is_data(actual_space);
  const char *to = data ? from : main.colorspace.scene_linear_name;
  if (!to[0] || STREQ(to, "scene_linear") ||
      IMB_colormanagement_colorspace_get_named_index(to) < 0)
    throw std::runtime_error("Decoded image target color space is unavailable");
  auto processor = ColormanageProcessor::colorspace_processor_new(from, to);
  auto normalized = Buffer(
      IMB_allocImBuf(raw->x, raw->y, ImBufFlags::FloatData | ImBufFlags::UninitializedPixels),
      IMB_freeImBuf);
  if (!normalized || !normalized->float_data())
    throw std::bad_alloc();
  normalized->channels = 4;
  normalized->color_mode = ImColorMode::RGBA;
  normalized->foptions = raw->foptions;
  // Transport buffer already has the image's requested representation. This
  // flag prevents the unchanged float uploader from applying alpha again;
  // the Image's original alpha_mode retains all graph sampling semantics.
  normalized->flags |= ImBufFlags::AlphaChannelPacked;
  const bool premultiplied = !data && image->alpha_mode == IMA_ALPHA_PREMUL;
  auto *target = normalized->float_data_for_write();
  if (floating) {
    if (raw->channels != 1 && raw->channels != 3 && raw->channels != 4)
      throw std::runtime_error("Native float image channel layout is unavailable");
    // Preserve Blender's literal float expansion: one channel becomes
    // (Y,Y,Y,Y), including alpha; three channels receive alpha one. Alpha mode
    // and the source shader decide how those components are interpreted.
    IMB_colormanagement_imbuf_to_float_texture(target, 0, 0, raw->x, raw->y, raw.get(), true);
  }
  else {
    const auto *bytes = raw->byte_data();
    for (std::size_t i = 0, n = std::size_t(raw->x) * raw->y * 4; i < n; ++i)
      target[i] = bytes[i] / 255.f;
  }
  for (int y = 0; y < raw->y; y += 32) {
    if (cancel.load())
      throw std::runtime_error("Native image normalization cancelled");
    processor.apply(
        target + std::size_t(y) * raw->x * 4, raw->x, std::min(32, raw->y - y), 4, premultiplied);
  }
  if (image->alpha_mode == IMA_ALPHA_IGNORE)
    for (std::size_t i = 0, n = std::size_t(raw->x) * raw->y; i < n; ++i)
      target[i * 4 + 3] = 1;
  IMB_colormanagement_assign_float_colorspace(normalized.get(), to);

  QJsonObject metadata = QJsonObject{{"image", QString::fromUtf8(image->id.name + 2)},
                                     {"authored_space", authored_space},
                                     {"resolved_source_space", resolved_space},
                                     {"legacy_space_alias", authored_space != resolved_space},
                                     {"decoded_space", QString::fromUtf8(from)},
                                     {"target_space", QString::fromUtf8(to)},
                                     {"raw_float", floating},
                                     {"data", data},
                                     {"alpha_mode", int(image->alpha_mode)}};
  return {std::move(normalized), std::move(metadata)};
}
NativeMaterialDecodedImage decode_native_material_image(const blender::Main &main,
                                                        blender::Image &image,
                                                        const std::atomic_bool &cancel)
{
  if (image.source != blender::IMA_SRC_FILE)
    throw std::invalid_argument("Named-space native decoding requires a single-file image source");
  return decode_native_material_image_tile(main, image, 0, cancel);
}

}  // namespace bts
