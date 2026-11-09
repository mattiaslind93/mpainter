#include "SpsmBitmapTexture.hh"
#include "BLI_math_half.hh"
#include "IMB_colormanagement.hh"
#include <QCryptographicHash>
#include <QUuid>
#include <QByteArrayView>
#include <algorithm>
#include <bit>
#include <climits>
#include <limits>
namespace bts {
namespace {
using namespace spsm;
using namespace spsm::bitmap;
std::string field_key(std::string_view s){return std::to_string(s.size())+":"+std::string(s);}
std::string location(const ImageResource &r){return field_key(std::filesystem::absolute(r.source).lexically_normal().generic_string())+field_key(r.dataset.path);}
std::string policy_key(const SpsmBitmapPolicy &p){return std::to_string(int(p.bytes))+":"+std::to_string(int(p.rows))+":"+std::to_string(int(p.alpha))+":"+std::to_string(p.raw)+":"+field_key(p.input_space);}
std::string canonical(const std::string &name){
 if(name.empty()||std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))throw Error("SPSM color requires an explicit known input color space");
 const auto *space=blender::IMB_colormanagement_space_get_named(name.c_str());if(!space)space=blender::IMB_colormanagement_space_from_interop_id(name);
 if(!space)throw Error("Unknown SPSM input color space: "+name);if(blender::IMB_colormanagement_space_is_data(space))throw Error("SPSM color input is a data color space; select raw interpretation instead");return blender::IMB_colormanagement_colorspace_get_name(space);
}
void policy_valid(const SpsmBitmapPolicy &p){
 if((p.bytes!=SpsmByteOrder::LittleEndian&&p.bytes!=SpsmByteOrder::BigEndian)||(p.rows!=SpsmRowOrder::FirstRowIsV0&&p.rows!=SpsmRowOrder::FirstRowIsV1)||(p.alpha!=SpsmAlpha::Straight&&p.alpha!=SpsmAlpha::DataComponents))throw Error("SPSM bitmap needs explicit byte order, row order and alpha interpretation; source enums remain unverified");
 if(p.raw&&!p.input_space.empty())throw Error("Raw SPSM data must not specify a color space");
 if(!p.raw&&p.alpha!=SpsmAlpha::Straight)throw Error("SPSM color requires explicit straight alpha; data components are not color alpha");
}
void require_format(const Header &h){
 const auto *format=h.field("format");const auto *text=format?std::get_if<Text>(&format->value):nullptr;
 if(h.version!="v2"||!format||format->source_null||!text||text->null||text->utf8!="RGBA16F")throw Error("Unsupported SPSM bitmap format: only literal v2 RGBA16F is interpretable; v1 integer format is unverified");
}
std::uint64_t retained(const RawImage &raw){
 auto n=resource_sum(raw.bytes->capacity(),raw.encoded->record->capacity());const auto &h=*raw.encoded->header;
 n=resource_sum(n,resource_sum(sizeof(Header),resource_product(h.fields.capacity(),sizeof(HeaderField))));
 if(h.encoded)n=resource_sum(n,h.encoded->capacity());
 n=resource_sum(n,resource_sum(h.version.capacity(),h.compression.capacity()));
 for(const auto &f:h.fields){n=resource_sum(n,resource_sum(f.key.utf16_be.capacity(),f.key.utf8.capacity()));if(const auto *v=std::get_if<Text>(&f.value))n=resource_sum(n,resource_sum(v->utf16_be.capacity(),v->utf8.capacity()));if(const auto *v=std::get_if<ImageBytes>(&f.value))n=resource_sum(n,v->capacity());}return n;
}
struct Storage {RawImage original;std::vector<FillTexture::Pixel> pixels;};
}
std::shared_ptr<const SpsmBitmapTexture> interpret_spsm_bitmap(RawImage raw,const ImageResource &source,const SpsmBitmapPolicy &p,ReadOptions options){
 policy_valid(p);
 if(!raw.encoded||!raw.encoded->header||!raw.encoded->record||!raw.bytes)throw Error("Missing SPSM immutable bitmap ownership");
 const auto &h=*raw.encoded->header;require_format(h);
 if(h.width>std::uint64_t(INT_MAX)||h.height>std::uint64_t(INT_MAX)||!h.width||!h.height)throw Error("SPSM extent exceeds native FillTexture signed-int representation");
 const auto row=resource_product(h.width,8),count=resource_product(h.width,h.height),size=resource_product(count,sizeof(FillTexture::Pixel));
 if(h.pitch<row||resource_product(h.pitch,h.height)!=h.raw_bytes||raw.bytes->size()!=h.raw_bytes||count>std::vector<FillTexture::Pixel>{}.max_size())throw Error("Invalid SPSM RGBA16F row span or native pixel storage");
 const auto metadata=resource_product(resource_sum(resource_sum(source.source.native().size(),source.dataset.path.size()),p.input_space.size()),16);
 const auto added=resource_sum(size,resource_sum(metadata,sizeof(Storage)+sizeof(SpsmBitmapTexture)+sizeof(FillTexture)+1024));


 const auto from=p.raw?std::string{}:canonical(p.input_space),target=p.raw?std::string{}:canonical("ACEScg");
 auto storage=std::make_shared<Storage>();storage->original=raw;storage->pixels.resize(std::size_t(count));
 for(std::uint64_t y=0;y<h.height;++y){const auto sy=p.rows==SpsmRowOrder::FirstRowIsV0?y:h.height-1-y;const auto *line=raw.bytes->data()+std::size_t(sy*h.pitch);
  for(std::uint64_t x=0;x<h.width;++x){auto &pixel=storage->pixels[std::size_t(y*h.width+x)];for(int c=0;c<4;++c){const auto *v=line+std::size_t(x*8+c*2);const auto bits=p.bytes==SpsmByteOrder::LittleEndian?std::uint16_t(v[0]|(std::uint16_t(v[1])<<8)):std::uint16_t((std::uint16_t(v[0])<<8)|v[1]);pixel[c]=blender::math::half_to_float(bits);if(!std::isfinite(pixel[c]))throw Error("SPSM half-float bitmap contains nonfinite components");}
   if(p.alpha==SpsmAlpha::Straight&&(pixel[3]<0||pixel[3]>1))throw Error("SPSM straight alpha is outside 0..1; interpretation not silently changed");
  }
 }
 if(!p.raw&&from!=target)for(std::uint64_t y=0;y<h.height;++y)blender::IMB_colormanagement_transform_float(reinterpret_cast<float *>(storage->pixels.data()+std::size_t(y*h.width)),int(h.width),1,4,from.c_str(),target.c_str(),false);
 auto texture=std::make_shared<FillTexture>();texture->width=int(h.width);texture->height=int(h.height);texture->raw=p.raw;texture->source_space=p.raw?std::string{}:p.input_space;texture->input_space=p.raw?"Non-Color/raw":from;texture->name=source.dataset.path;texture->floating_point=true;texture->data_units=p.raw;texture->range_valid=true;texture->minimum=std::numeric_limits<float>::infinity();texture->maximum=-texture->minimum;
 for(const auto &pixel:storage->pixels){for(const auto v:pixel)if(!std::isfinite(v))throw Error("SPSM OCIO conversion produced nonfinite components");texture->minimum=std::min(texture->minimum,pixel[0]);texture->maximum=std::max(texture->maximum,pixel[0]);}
 const auto identity=location(source);if(identity.size()>std::size_t(std::numeric_limits<qsizetype>::max())||raw.encoded->record->size()>std::size_t(std::numeric_limits<qsizetype>::max()))throw Error("SPSM identity exceeds native Qt byte-view representation");
 const QUuid domain("22e74542-d4e0-59d5-89b9-7dfdce5878e5");texture->asset=QUuid::createUuidV5(domain,QByteArrayView(identity.data(),qsizetype(identity.size()))).toString(QUuid::WithoutBraces).toStdString();
 const auto encoded_sha=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(raw.encoded->record->data()),qsizetype(raw.encoded->record->size())),QCryptographicHash::Sha256).toHex().toStdString();
 QCryptographicHash digest(QCryptographicHash::Sha256);const auto semantic=std::string("bts-spsm-pixels-v1:")+field_key(encoded_sha)+field_key(policy_key(p))+field_key(from)+field_key(target);digest.addData(QByteArrayView(semantic.data(),qsizetype(semantic.size())));
 // Canonical little-endian IEEE float bytes keep identity independent of CPU
 // endianness, and bind the actual configured OCIO result as well as policy.
 static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559);std::array<char,4096> block;std::size_t at=0;
 for(const auto &pixel:storage->pixels)for(const auto value:pixel){const auto bits=std::bit_cast<std::uint32_t>(value);for(unsigned c=0;c<4;++c)block[at++]=char(bits>>(8*c));if(at==block.size()){digest.addData(QByteArrayView(block.data(),qsizetype(at)));at=0;}}
 if(at)digest.addData(QByteArrayView(block.data(),qsizetype(at)));texture->hash=digest.result().toHex().toStdString();
 texture->storage_key="spsm:"+texture->hash+":"+policy_key(p);texture->pixels=std::shared_ptr<const std::vector<FillTexture::Pixel>>(storage,&storage->pixels);texture->validate();
 auto result=std::make_shared<SpsmBitmapTexture>();result->texture=std::move(*texture);result->source=source;result->original=std::move(raw);result->policy=p;result->encoded_sha256=encoded_sha;
 result->warnings={"Byte order, row order and alpha are caller policies; source enum meanings and source-rendered appearance remain unverified.",p.raw?"Raw components retained without color conversion; caller must validate channel use.":"Input color space is an explicit caller policy, not inferred from the source colorSpace ordinal; converted to ACEScg via configured OCIO."};
 if(p.alpha==SpsmAlpha::DataComponents)result->warnings.push_back("Fourth raw component is preserved as data; FillTexture alpha sampling must be chosen deliberately by the caller.");
 return result;
}
std::shared_ptr<const SpsmBitmapTexture> SpsmBitmapTextureCache::load(const Descriptor &d,const SpsmBitmapPolicy &p,ReadOptions options){
 policy_valid(p);if(!d.header)throw Error("Missing SPSM bitmap descriptor");require_format(*d.header);const auto key=location(d.resource)+":"+std::to_string(d.resource.stamp.file_bytes)+":"+std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d.resource.stamp.modified.time_since_epoch()).count())+":"+policy_key(p);
 std::lock_guard lock(mutex_);std::erase_if(entries_,[](const auto &entry){return entry.second.expired();});
 if(const auto found=entries_.find(key);found!=entries_.end())if(auto shared=found->second.lock())return shared;
 const auto metadata=resource_product(key.size(),4);
 auto result=interpret_spsm_bitmap(decode(load_encoded(d,options),options),d.resource,p,options);entries_[key]=result;return result;
}
}
