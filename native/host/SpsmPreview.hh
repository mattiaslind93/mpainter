#pragma once
#include "../spsm/SpsmReader.hh"
#include "ResourceAvailability.hh"
#include <QByteArray>
#include <QTemporaryFile>
#include <QString>
namespace bts {
namespace spsm_preview_detail {
// Bounded Qt big-endian metadata framing; no QVariant deserialization or code.
class Reader {
 std::span<const std::uint8_t> bytes;std::size_t position=0;
 public:
 explicit Reader(std::span<const std::uint8_t> value):bytes(value){}
 std::uint32_t integer(){if(bytes.size()-position<4)throw spsm::Error("Truncated SPSM preview integer");std::uint32_t result=0;for(int i=0;i<4;++i)result=(result<<8)|bytes[position++];return result;}
 std::uint8_t byte(){if(position==bytes.size())throw spsm::Error("Truncated SPSM preview flag");return bytes[position++];}
 std::span<const std::uint8_t> data(){const auto size=integer();if(size==0xffffffffu)return {};if(size>bytes.size()-position)throw spsm::Error("SPSM preview span crosses metadata boundary");auto result=bytes.subspan(position,size);position+=size;return result;}
 QString string(){const auto value=data();if(value.size()%2)throw spsm::Error("Invalid SPSM preview string");QString result;result.reserve(qsizetype(value.size()/2));for(std::size_t i=0;i<value.size();i+=2)result+=QChar(std::uint16_t(value[i])<<8|value[i+1]);return result;}
};
inline QByteArray encoded_preview(std::span<const std::uint8_t> bytes){
 Reader metadata(bytes);const auto count=metadata.integer();if(count>bytes.size()/9)throw spsm::Error("Invalid SPSM preview metadata count");
 for(std::uint32_t i=0;i<count;++i){const auto name=metadata.string();const auto type=metadata.integer();if(metadata.byte()>1)throw spsm::Error("Invalid SPSM metadata null flag");
  if(type==2||type==3){metadata.integer();continue;}
  if(type!=10&&type!=12)return {}; // Unknown framing is never guessed/scanned.
  const auto value=metadata.data();if(name!="previews"||type!=12)continue;
  Reader previews(value);const auto entries=previews.integer();if(entries>value.size()/8)throw spsm::Error("Invalid SPSM preview count");
  for(std::uint32_t entry=0;entry<entries;++entry){const auto label=previews.string();const auto image=previews.data();if(label=="__DEFAULT_PREVIEW__"){
   return QByteArray(reinterpret_cast<const char *>(image.data()),qsizetype(image.size()));
  }}return {};
 }return {};
}
}
inline QByteArray spsm_embedded_preview(const QByteArray &source){
 // Thumbnail jobs already run on a worker. Private temporary bytes prevent an
 // asset-machine path from being consulted and disappear with this handle.
 QTemporaryFile file;if(!file.open()||file.write(source)!=source.size()||!file.flush())throw spsm::Error("Cannot stage SPSM preview source");
 const auto metadata=spsm::read_preview_metadata(file.fileName().toStdString());return metadata?spsm_preview_detail::encoded_preview(*metadata):QByteArray{};
}
}
