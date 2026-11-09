// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#pragma once
#include "RibbonSettings.hh"
#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QJsonObject>
#include <QtEndian>
#include <map>
namespace bts {
namespace ribbon_io {
inline std::uint64_t image_budget()
{
  return std::numeric_limits<quint32>::max();
}
[[noreturn]] inline void fail()
{
  throw std::invalid_argument("Invalid packed ribbon image");
}
inline QByteArray image_bytes(const RibbonImage &image)
{
  image.validate();
  QByteArray bytes;
  QDataStream s(&bytes, QIODevice::WriteOnly);
  s.setByteOrder(QDataStream::LittleEndian);
  s.setFloatingPointPrecision(QDataStream::SinglePrecision);
  s << quint32(0x42545231) << qint32(image.width) << qint32(image.height)
    << QString::fromStdString(image.name);
  for (const auto &p : image.pixels)
    for (float v : p)
      s << v;
  if (s.status() != QDataStream::Ok || bytes.size() > image_budget())
    fail();
  return bytes;
}
inline QString hash(const QByteArray &bytes)
{
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
inline std::shared_ptr<const RibbonImage> read_image(const QString &encoded,
                                                     const QString &expected)
{
  const auto base64 = encoded.toLatin1();
  if (base64.isEmpty())
    fail();
  const auto packed = QByteArray::fromBase64(base64);
  if (packed.toBase64() != base64 || packed.size() < 4 ||
      qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(packed.constData())) >
          image_budget())
    fail();
  const auto bytes = qUncompress(packed);
  if (bytes.isEmpty() || bytes.size() > image_budget() || hash(bytes) != expected)
    fail();
  QDataStream s(bytes);
  s.setByteOrder(QDataStream::LittleEndian);
  s.setFloatingPointPrecision(QDataStream::SinglePrecision);
  quint32 magic;
  qint32 width, height;
  QString name;
  s >> magic >> width >> height >> name;
  if (magic != 0x42545231 || width < 1 || height < 1 || name.isEmpty())
    fail();
  const auto needed = resource_product(resource_product(width, height),
                                       sizeof(std::array<float, 4>));
  if (needed > std::uint64_t(bytes.size() - s.device()->pos()))
    fail();
  auto image = std::make_shared<RibbonImage>();
  image->width = width;
  image->height = height;
  image->name = name.toStdString();
  image->pixels.resize(std::size_t(width) * height);
  for (auto &p : image->pixels)
    for (float &v : p)
      s >> v;
  if (s.status() != QDataStream::Ok || !s.atEnd())
    fail();
  image->validate();
  return image;
}
struct WriteContext {
  QJsonObject images;
  std::map<const RibbonImage *, QString> identities;
};
struct ReadContext {
  std::map<QString, std::shared_ptr<const RibbonImage>> images;
};
inline thread_local WriteContext *writer = nullptr;
inline thread_local ReadContext *reader = nullptr;
}  // namespace ribbon_io
class RibbonWriteScope {
  ribbon_io::WriteContext context_;
  ribbon_io::WriteContext *before_;

 public:
  RibbonWriteScope() : before_(ribbon_io::writer)
  {
    ribbon_io::writer = &context_;
  }
  ~RibbonWriteScope()
  {
    ribbon_io::writer = before_;
  }
  QJsonObject images() const
  {
    return context_.images;
  }
};
class RibbonReadScope {
  ribbon_io::ReadContext context_;
  ribbon_io::ReadContext *before_;

 public:
  explicit RibbonReadScope(const QJsonObject &pool) : before_(ribbon_io::reader)
  {
    std::uint64_t total = 0;
    for (auto it = pool.begin(); it != pool.end(); ++it) {
      if (it.key().size() != 64 || !it.value().isString())
        ribbon_io::fail();
      auto image = ribbon_io::read_image(it.value().toString(), it.key());
      total += image->pixels.size() * 16;
      context_.images.emplace(it.key(), std::move(image));
    }
    ribbon_io::reader = &context_;
  }
  ~RibbonReadScope()
  {
    ribbon_io::reader = before_;
  }
};
inline QJsonObject ribbon_json(const RibbonSettings &s)
{
  s.validate();
  QJsonObject image;
  if (auto *w = ribbon_io::writer) {
    auto found = w->identities.find(s.image.get());
    if (found == w->identities.end()) {
      const auto bytes = ribbon_io::image_bytes(*s.image);
      const auto id = ribbon_io::hash(bytes);
      w->images[id] = QString::fromLatin1(qCompress(bytes, 1).toBase64());
      found = w->identities.emplace(s.image.get(), id).first;
    }
    image["reference"] = found->second;
  }
  else {
    const auto bytes = ribbon_io::image_bytes(*s.image);
    image = {{"reference", ribbon_io::hash(bytes)},
             {"packed", QString::fromLatin1(qCompress(bytes, 1).toBase64())}};
  }
  return {{"enabled", s.enabled},
          {"flip", s.flip},
          {"omit_ends", s.omit_ends},
          {"offsets_only", s.offsets_only},
          {"mirror", s.mirror},
          {"remove_clipped", s.remove_clipped},
          {"stretch", int(s.stretch)},
          {"tiling", int(s.tiling)},
          {"corner", int(s.corner)},
          {"ratio", int(s.ratio)},
          {"alignment", int(s.alignment)},
          {"alpha", int(s.alpha)},
          {"orientation", s.orientation},
          {"count", s.count},
          {"aspect", s.aspect},
          {"start_offset", s.start_offset},
          {"end_offset", s.end_offset},
          {"image", image}};
}
inline RibbonSettings read_ribbon(const QJsonValue &value)
{
  if (!value.isObject())
    ribbon_io::fail();
  const auto o = value.toObject();
  RibbonSettings s;
  const auto boolean = [&](const char *key) {
    if (!o[key].isBool())
      ribbon_io::fail();
    return o[key].toBool();
  };
  const auto number = [&](const char *key) {
    if (!o[key].isDouble() || !std::isfinite(o[key].toDouble()))
      ribbon_io::fail();
    return o[key].toDouble();
  };
  const auto integer = [&](const char *key) {
    const double v = number(key);
    if (v < 0 || v > 65536 || v != int(v))
      ribbon_io::fail();
    return int(v);
  };
  s.enabled = boolean("enabled");
  s.flip = boolean("flip");
  s.omit_ends = boolean("omit_ends");
  s.offsets_only = boolean("offsets_only");
  s.mirror = boolean("mirror");
  s.remove_clipped = boolean("remove_clipped");
  s.stretch = RibbonStretch(integer("stretch"));
  s.tiling = RibbonTiling(integer("tiling"));
  s.corner = RibbonCorner(integer("corner"));
  s.ratio = RibbonRatio(integer("ratio"));
  s.alignment = RibbonAlignment(integer("alignment"));
  s.alpha = RibbonAlpha(integer("alpha"));
  s.orientation = integer("orientation");
  s.count = integer("count");
  s.aspect = number("aspect");
  s.start_offset = number("start_offset");
  s.end_offset = number("end_offset");
  if (!o["image"].isObject())
    ribbon_io::fail();
  const auto image = o["image"].toObject();
  const auto id = image["reference"].toString();
  if (id.size() != 64)
    ribbon_io::fail();
  if (image.contains("packed")) {
    if (!image["packed"].isString())
      ribbon_io::fail();
    s.image = ribbon_io::read_image(image["packed"].toString(), id);
  }
  else {
    if (!ribbon_io::reader)
      ribbon_io::fail();
    const auto found = ribbon_io::reader->images.find(id);
    if (found == ribbon_io::reader->images.end())
      ribbon_io::fail();
    s.image = found->second;
  }
  s.validate();
  return s;
}
}  // namespace bts
