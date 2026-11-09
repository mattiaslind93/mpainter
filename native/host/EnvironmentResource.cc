#include "ResourceAvailability.hh"
#include "EnvironmentResource.hh"
#include <cmath>
#include <memory>
#include <stdexcept>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include "BKE_studiolight.h"
#include "BLI_listbase.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
namespace bts {
using namespace blender;
namespace {

QByteArray read(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() <= 0)
    throw std::runtime_error("Environment must be a readable HDR/EXR file");

  const auto bytes = file.readAll();
  if (bytes.size() != file.size())
    throw std::runtime_error("Environment read was incomplete");
  return bytes;
}
}  // namespace
QJsonObject pack_environment_resource(const QString &name)
{
  for (StudioLight &light : BKE_studiolight_listbase())
    if (name == light.name && (light.flag & STUDIOLIGHT_TYPE_WORLD)) {
      const auto bytes = read(QString::fromUtf8(light.filepath));
      return {{"name", name},
              {"data", QString::fromLatin1(bytes.toBase64())},
              {"sha256",
               QString::fromLatin1(
                   QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
    }
  throw std::runtime_error("Template environment resource is missing");
}
QString unpack_environment_resource(const QJsonObject &data)
{
  auto *light = register_environment_bytes(
      QByteArray::fromBase64(data["data"].toString().toLatin1(),
                             QByteArray::AbortOnBase64DecodingErrors),
      data["name"].toString());
  return QString::fromUtf8(light->filepath);
}
StudioLight *register_environment_bytes(const QByteArray &bytes, const QString &name)
{
  const auto suffix = QFileInfo(name).suffix().toLower();
  if (bytes.isEmpty() || name.isEmpty() || name.toUtf8().size() >= 256 ||
      QFileInfo(name).fileName() != name || name.contains('\\') ||
      (suffix != "hdr" && suffix != "exr"))
    throw std::runtime_error("Invalid environment resource name or size");
  const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  for (StudioLight &light : BKE_studiolight_listbase())
    if (name == light.name) {
      if (!(light.flag & STUDIOLIGHT_TYPE_WORLD) ||
          QCryptographicHash::hash(read(QString::fromUtf8(light.filepath)),
                                   QCryptographicHash::Sha256) != hash)
        throw std::runtime_error(
            "Another environment uses this filename with different content; rename the selected file");
      return &light;
    }
  const QString directory = QStandardPaths::writableLocation(
                                QStandardPaths::GenericCacheLocation) +
                            "/mPainter/environments/" + QString::fromLatin1(hash.toHex());
  if (!QDir().mkpath(directory))
    throw std::runtime_error("Cannot create environment cache");
  const auto path = directory + "/" + name;
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    throw std::runtime_error("Cannot cache environment bytes");
  const auto native_path = path.toUtf8();
  using Buffer = std::unique_ptr<ImBuf, decltype(&IMB_freeImBuf)>;
  Buffer header(IMB_load_image_from_filepath(native_path.constData(),
                                             ImBufFlags::Test | ImBufFlags::AlphaIgnore),
                IMB_freeImBuf);
  if (!header || header->x < 1 || header->y < 1)
    throw std::runtime_error("Cannot decode environment dimensions");
  Buffer image(IMB_load_image_from_filepath(native_path.constData(), ImBufFlags::AlphaIgnore),
               IMB_freeImBuf);
  if (!image || !image->float_data() || image->channels < 1 || image->channels > 4)
    throw std::runtime_error("Environment requires native floating-point HDR/EXR pixels");
  const auto count = std::size_t(image->x) * image->y * image->channels;
  for (std::size_t i = 0; i < count; ++i)
    if (!std::isfinite(image->float_data()[i]))
      throw std::runtime_error("Environment contains nonfinite radiance");
  auto *light = BKE_studiolight_load(native_path.constData(), STUDIOLIGHT_TYPE_WORLD);
  if (!light)
    throw std::runtime_error("Cannot register environment");
  return light;
}
StudioLight *register_environment_file(const QString &path)
{
  const QFileInfo info(path);
  if (!info.isAbsolute())
    throw std::runtime_error("Environment requires an absolute file path");
  return register_environment_bytes(read(path), info.fileName());
}
}  // namespace bts
