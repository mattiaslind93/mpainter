// SPDX-License-Identifier: GPL-3.0-or-later
#include <QStandardPaths>
#include "NativeMaterialImageArchive.hh"
#include "NativeMaterialImageSource.hh"
#include "MEM_guardedalloc.h"
#include <set>
#include <algorithm>
#include "NativeMaterialDecodedImage.hh"
#include "NativeMaterialImageSourceReference.hh"
#include "BKE_image.hh"
#include "BKE_idprop.hh"
#include "BKE_main.hh"
#include <memory>
#include <vector>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include "BLI_listbase.h"
#include "BLI_path_utils.hh"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
namespace bts {
std::vector<int> native_material_tiled_image_numbers(const blender::Image &image)
{
  using namespace blender;
  if (image.source != IMA_SRC_TILED || image.type != IMA_TYPE_IMAGE || image.flag & IMA_USE_VIEWS)
    throw std::invalid_argument(
        "Native tiled source requires an ordinary single-view TILED Image");
  std::vector<int> numbers;
  std::set<int> unique;
  for (const auto &tile : image.tiles) {
    if (tile.tile_number < 1001 || tile.tile_number > IMA_UDIM_MAX ||
        !unique.insert(tile.tile_number).second)
      throw std::invalid_argument("Invalid or duplicate native source tile identity");
    numbers.push_back(tile.tile_number);
  }
  if (numbers.empty())
    throw std::invalid_argument("Native tiled source has no expected tiles");
  std::sort(numbers.begin(), numbers.end());
  return numbers;
}
QString native_material_tiled_image_pattern(const blender::Main &main, const blender::Image &image)
{
  using namespace blender;
  (void)native_material_tiled_image_numbers(image);
  const auto literal = QString::fromUtf8(image.filepath);
  if (literal.count("<UDIM>") + literal.count("<UVTILE>") != 1)
    throw std::invalid_argument("Native tiled source requires one explicit UDIM or UVTILE token");
  char path[FILE_MAX]{};
  STRNCPY(path, image.filepath);
  BLI_path_abs(path, ID_BLEND_PATH(&main, &image.id));
  return QString::fromUtf8(path);
}
NativeMaterialEncodedImageSource native_material_encoded_image_source(const blender::Main &main,
                                                                      const blender::Image &image,
                                                                      int tile)
{
  using namespace blender;
  const bool tiled = image.source == IMA_SRC_TILED;
  if (tiled) {
    const auto numbers = native_material_tiled_image_numbers(image);
    if (!std::binary_search(numbers.begin(), numbers.end(), tile))
      throw std::invalid_argument("Native source tile is not in the actual Image tile table");
    (void)native_material_tiled_image_pattern(main, image);
  }
  else if (tile || !ELEM(image.source, IMA_SRC_FILE, IMA_SRC_GENERATED))
    throw std::invalid_argument(
        "Native encoded source requires a single-view FILE or TILED Image");
  if (tiled && !image.packedfiles.is_empty()) {
    const auto expected = native_material_tiled_image_numbers(image);
    std::set<int> packed;
    for (const auto &part : image.packedfiles)
      if (part.view != 0 ||
          !std::binary_search(expected.begin(), expected.end(), part.tile_number) ||
          !packed.insert(part.tile_number).second)
        throw std::invalid_argument("Native packed tile set has an extra or duplicate identity");
    for (int number : expected)
      if (!packed.contains(number)) {
        ImageUser missing{};
        BKE_imageuser_default(&missing);
        missing.tile = number;
        char path[FILE_MAX]{};
        BKE_image_user_file_path_ex(&main, &missing, &image, path, true, true);
        throw std::invalid_argument(native_material_image_source_error(
            image, path, "missing packed tile " + std::to_string(number), true));
      }
  }
  ImageUser user{};
  BKE_imageuser_default(&user);
  if (tiled)
    user.tile = tile;
  char path[FILE_MAX]{};
  BKE_image_user_file_path_ex(&main, &user, &image, path, true, true);
  NativeMaterialEncodedImageSource result;
  result.path = QString::fromUtf8(path);
  result.filename = QFileInfo(result.path).fileName();
  result.tile = tile;
  for (const auto &part : image.packedfiles) {
    if (tiled && part.tile_number != tile)
      continue;
    if (result.packed)
      throw std::invalid_argument("Native source has duplicate packed tile/view identity");
    if (!part.packedfile || !part.packedfile->data || part.packedfile->size <= 0 ||
        MEM_allocN_len(part.packedfile->data) < std::size_t(part.packedfile->size))
      throw std::invalid_argument("Native source packed tile span is invalid");
    result.packed = part.packedfile;
  }
  if (!tiled && !image.packedfiles.is_empty() && image.packedfiles.first != image.packedfiles.last)
    throw std::invalid_argument("Native single-file source has multiple packed views/tiles");
  return result;
}
namespace {
struct MissingReference {
  QString reference, root, resolved;
  bool had_properties = false;
};
[[noreturn]] void reference_error(const blender::Image &image,
                                  const QString &resolved,
                                  const char *reason)
{
  throw std::invalid_argument(
      native_material_image_source_error(image, resolved.toStdString(), reason));
}
QString image_path(const blender::Main &main, const blender::Image &image)
{
  if (image.source == blender::IMA_SRC_TILED)
    return native_material_tiled_image_pattern(main, image);
  blender::ImageUser user{};
  blender::BKE_imageuser_default(&user);
  char path[FILE_MAX]{};
  blender::BKE_image_user_file_path_ex(&main, &user, &image, path, true, true);
  return QString::fromUtf8(path);
}
void validate_reference(const blender::Image &image, const MissingReference &value)
{
  if ((image.source != blender::IMA_SRC_FILE && image.source != blender::IMA_SRC_TILED) ||
      image.type != blender::IMA_TYPE_IMAGE || image.flag & blender::IMA_USE_VIEWS)
    reference_error(image, value.resolved, "unsupported retained missing-image source kind");
  if (image.source == blender::IMA_SRC_TILED) {
    (void)native_material_tiled_image_numbers(image);
    if (value.reference.count("<UDIM>") + value.reference.count("<UVTILE>") != 1)
      reference_error(image, value.resolved, "invalid retained missing-tile pattern");
  }
  const auto valid = [](const QString &s, std::size_t capacity) {
    return !s.isEmpty() && !s.contains(QChar::Null) && std::size_t(s.toUtf8().size()) < capacity;
  };
  if (!valid(value.reference, sizeof(image.filepath)) ||
      !valid(value.root, sizeof(blender::Main::filepath)) || !valid(value.resolved, FILE_MAX) ||
      !QFileInfo(value.root).isAbsolute() || !QFileInfo(value.resolved).isAbsolute())
    reference_error(image, value.resolved, "invalid retained missing-image origin");
  char candidate[FILE_MAX];
  blender::BLI_strncpy(candidate, value.reference.toUtf8().constData(), sizeof(candidate));
  blender::BLI_path_abs(candidate, value.root.toUtf8().constData());
  if (QString::fromUtf8(candidate) != value.resolved)
    reference_error(image,
                    value.resolved,
                    "retained missing-image origin cannot reproduce its actual resolved source");
}
}  // namespace
void retain_missing_native_image_reference(const blender::Main &source_main,
                                           const blender::Image &source,
                                           blender::Main &snapshot_main,
                                           blender::Image &snapshot)
{
  using namespace blender;
  if ((source.source != IMA_SRC_FILE && source.source != IMA_SRC_TILED) ||
      source.type != IMA_TYPE_IMAGE || source.flag & IMA_USE_VIEWS)
    return;
  const MissingReference value{QString::fromUtf8(source.filepath),
                               QString::fromUtf8(source_main.filepath),
                               image_path(source_main, source),
                               snapshot.id.properties != nullptr};
  validate_reference(source, value);
  if (IDP_GetPropertyFromGroup_null(snapshot.id.properties,
                                    native_missing_image_reference_property))
    reference_error(
        source, value.resolved, "reserved missing-image provenance property already exists");
  // MAKE_LOCAL can remove the original linked-library base. Admission above
  // verifies this literal reference can still resolve from the saved Main root.
  const auto reference = value.reference.toUtf8();
  STRNCPY(snapshot.filepath, reference.constData());
  if (image_path(snapshot_main, snapshot) != value.resolved)
    reference_error(
        source, value.resolved, "snapshot image has a different linked-library resolution");
  IDPropertyTemplate init{};
  std::unique_ptr<IDProperty, decltype(&IDP_FreeProperty)> marker(
      IDP_New(IDP_GROUP, &init, native_missing_image_reference_property), IDP_FreeProperty);
  IDP_AddToGroup(marker.get(), IDP_NewInt(1, "version"));
  IDP_AddToGroup(marker.get(), IDP_NewInt(value.had_properties ? 1 : 0, "had_properties"));
  IDP_AddToGroup(marker.get(), IDP_NewString(reference.constData(), "reference"));
  IDP_AddToGroup(marker.get(), IDP_NewString(value.root.toUtf8().constData(), "root"));
  IDP_AddToGroup(marker.get(), IDP_NewString(value.resolved.toUtf8().constData(), "resolved"));
  IDP_AddToGroup(IDP_EnsureProperties(&snapshot.id), marker.release());
}
void restore_missing_native_image_references(blender::Main &main)
{
  using namespace blender;
  struct Restore {
    Image *image;
    IDProperty *marker;
    MissingReference value;
    QByteArray reference;
  };
  std::vector<Restore> pending;
  QString root;
  for (auto &image : main.images) {
    auto *marker = IDP_GetPropertyFromGroup_null(image.id.properties,
                                                 native_missing_image_reference_property);
    if (!marker)
      continue;
    if (image.id.lib)
      reference_error(image, {}, "retained missing-image snapshot is still library linked");
    if (marker->type != IDP_GROUP || marker->len != 5)
      reference_error(image, {}, "malformed retained missing-image provenance");
    const auto integer = [&](const char *name) {
      const auto *p = IDP_GetPropertyFromGroup(marker, name);
      if (!p || p->type != IDP_INT)
        reference_error(image, {}, "malformed retained missing-image integer");
      return p->data.val;
    };
    const auto text = [&](const char *name) {
      const auto *p = IDP_GetPropertyFromGroup(marker, name);
      if (!p || p->type != IDP_STRING || p->subtype != IDP_STRING_SUB_UTF8 || !p->data.pointer ||
          p->len <= 1 || p->len > FILE_MAX ||
          static_cast<const char *>(p->data.pointer)[p->len - 1] != 0)
        reference_error(image, {}, "malformed retained missing-image path");
      const QByteArray bytes(static_cast<const char *>(p->data.pointer), p->len - 1);
      const auto result = QString::fromUtf8(bytes);
      if (bytes.contains(char(0)) || result.toUtf8() != bytes)
        reference_error(image, {}, "invalid retained missing-image path encoding");
      return result;
    };
    const auto version = integer("version"), had = integer("had_properties");
    if (version != 1 || (had != 0 && had != 1))
      reference_error(image, {}, "unsupported retained missing-image provenance version");
    MissingReference value{text("reference"), text("root"), text("resolved"), bool(had)};
    validate_reference(image, value);
    if (!root.isEmpty() && root != value.root)
      reference_error(image, value.resolved, "retained missing-image origins disagree");
    root = value.root;
    const auto reference = value.reference.toUtf8();
    pending.push_back({&image, marker, std::move(value), reference});
  }
  // Publish only after every marker/path is validated. No legacy-base guessing.
  if (pending.empty())
    return;
  const auto base = root.toUtf8();
  STRNCPY(main.filepath, base.constData());
  for (const auto &entry : pending) {
    auto &image = *entry.image;
    STRNCPY(image.filepath, entry.reference.constData());
    IDP_FreeFromGroup(image.id.properties, entry.marker);
    if (!entry.value.had_properties && image.id.properties->len == 0) {
      IDP_FreeProperty(image.id.properties);
      image.id.properties = nullptr;
    }
  }
}
static StudioAsset snapshot_single_source(const blender::Image &image,
                                          const NativeMaterialEncodedImageSource &selected,
                                          const std::atomic_bool &cancel)
{
  using namespace blender;
  const auto *packed = selected.packed;
  const auto source = selected.path;
  const QFileInfo original(source);
  QString filename = selected.filename;
  if (filename.isEmpty() || filename == "." || filename == "..")
    filename = "image.bin";
  if (filename.contains('/') || filename.contains('\\') || filename.contains(QChar::Null))
    throw std::invalid_argument("Invalid native source filename");
  const qint64 size = packed ? packed->size : original.size();
  if (size <= 0 || (!packed && (!original.isFile() || !original.isReadable())))
    throw std::runtime_error(native_material_image_source_error(
        image, source.toStdString(), "encoded source image is missing", packed != nullptr));
  QString hash;
  if (packed) {
    QCryptographicHash digest(QCryptographicHash::Sha256);
    for (qint64 offset = 0; offset < size; offset += 4 * 1024 * 1024) {
      if (cancel.load())
        throw std::runtime_error("Native image snapshot cancelled");
      const auto count = std::min<qint64>(4 * 1024 * 1024, size - offset);
      digest.addData(QByteArrayView(static_cast<const char *>(packed->data) + offset, count));
    }
    hash = QString::fromLatin1(digest.result().toHex());
  }
  else
    hash = hash_texture_file(source, &cancel);
  const auto cache = qEnvironmentVariable(
      "BTS_RESOURCE_CACHE",
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/resources");
  const auto folder = QDir(cache).filePath(hash);
  if (!QDir().mkpath(folder))
    throw std::runtime_error("Cannot create native encoded-image cache");
  const auto destination = QDir(folder).filePath(filename);
  if (QFileInfo(destination).size() != size || hash_texture_file(destination, &cancel) != hash) {
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly))
      throw std::runtime_error("Cannot create native encoded-image snapshot");
    QFile input(source);
    if (!packed && !input.open(QIODevice::ReadOnly))
      throw std::runtime_error("Cannot read native source image");
    QCryptographicHash copied(QCryptographicHash::Sha256);
    for (qint64 offset = 0; offset < size;) {
      if (cancel.load())
        throw std::runtime_error("Native image snapshot cancelled");
      const auto count = std::min<qint64>(4 * 1024 * 1024, size - offset);
      QByteArray chunk;
      if (!packed) {
        chunk = input.read(count);
        if (chunk.size() != count)
          throw std::runtime_error("Native image source changed during snapshot");
      }
      const auto view = packed ? QByteArrayView(static_cast<const char *>(packed->data) + offset,
                                                count) :
                                 QByteArrayView(chunk);
      copied.addData(view);
      if (output.write(view.data(), view.size()) != view.size())
        throw std::runtime_error("Native source snapshot write failed");
      offset += count;
    }
    if (QString::fromLatin1(copied.result().toHex()) != hash || !output.commit())
      throw std::runtime_error("Native source changed or snapshot commit failed");
  }
  StudioAsset asset;
  asset.name = QString::fromUtf8(image.id.name + 2);
  asset.filename = filename;
  asset.path = destination;
  asset.usages = {"texture"};
  asset.search_terms = {"blender-native-image"};
  asset.texture_files = {{0, filename, destination, hash, size}};
  validate_texture_sources(asset.texture_files);
  return asset;
}
StudioAsset snapshot_native_image_source(const blender::Main &main,
                                         const blender::Image &image,
                                         const std::atomic_bool &cancel)
{
  using namespace blender;
  if (image.source != IMA_SRC_TILED)
    return snapshot_single_source(
        image, native_material_encoded_image_source(main, image), cancel);
  const auto numbers = native_material_tiled_image_numbers(image);
  StudioAsset result;
  result.name = QString::fromUtf8(image.id.name + 2);
  result.filename = QFileInfo(native_material_tiled_image_pattern(main, image)).fileName();
  result.usages = {"texture"};
  result.search_terms = {"blender-native-image"};
  for (int number : numbers) {
    if (cancel.load())
      throw std::runtime_error("Native tiled snapshot cancelled");
    const auto selected = native_material_encoded_image_source(main, image, number);
    try {
      auto member = snapshot_single_source(image, selected, cancel);
      auto file = member.texture_files.front();
      file.tile = number;
      result.texture_files.push_back(std::move(file));
    }
    catch (const std::exception &error) {
      throw std::runtime_error(std::string("tile ") + std::to_string(number) + ": " +
                               error.what());
    }
  }
  if (cancel.load())
    throw std::runtime_error("Native tiled snapshot cancelled");
  if (native_material_tiled_image_numbers(image) != numbers)
    throw std::runtime_error("Native source tile table changed during snapshot");
  validate_texture_sources(result.texture_files);
  return result;
}
StudioAsset snapshot_native_image_source(const blender::Image &image,
                                         const QString &library,
                                         const std::atomic_bool &cancel)
{
  std::unique_ptr<blender::Main, decltype(&blender::BKE_main_free)> main(blender::BKE_main_new(),
                                                                         blender::BKE_main_free);
  const auto path = library.toUtf8();
  blender::STRNCPY(main->filepath, path.constData());
  return snapshot_native_image_source(*main, image, cancel);
}

}  // namespace bts
