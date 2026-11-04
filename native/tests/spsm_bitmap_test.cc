#include "SpsmBitmap.hh"
#include "../host/ResourceBudget.hh"
#include <hdf5.h>
#include <lz4.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>

using namespace bts::spsm;
using namespace bts::spsm::bitmap;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F> void rejected(F action,std::string_view message={}){
 try{action();}catch(const std::exception &e){require(message.empty()||std::string_view(e.what()).find(message)!=std::string_view::npos,"rejection had unexpected reason");return;}throw std::runtime_error("Invalid image operation accepted");
}
struct Writer {
 ImageBytes bytes;
 template<class T> void number(T value){for(std::size_t i=sizeof(T);i-->0;)bytes.push_back(std::uint8_t(std::uint64_t(value)>>(8*i)));}
 void text(std::string_view s){number<std::uint32_t>(std::uint32_t(s.size()*2));for(const auto c:s){bytes.push_back(0);bytes.push_back(std::uint8_t(c));}}
 void field(std::string_view key,std::uint32_t type){text(key);number(type);number<std::uint8_t>(0);}
 void u32(std::string_view key,std::uint32_t n){field(key,3);number(n);}
 void i32(std::string_view key,std::int32_t n){field(key,2);number(std::bit_cast<std::uint32_t>(n));}
 void u64(std::string_view key,std::uint64_t n){field(key,5);number(n);}
 void i64(std::string_view key,std::int64_t n){field(key,4);number(std::bit_cast<std::uint64_t>(n));}
 void string(std::string_view key,std::string_view s){field(key,10);text(s);}
};
struct Record {ImageBytes bytes,raw;std::uint64_t header_size;};
Record record(bool v2=true,std::uint32_t width=32768,std::uint32_t height=2,
              std::uint64_t pitch=32768*8+16,std::uint64_t raw_override=0,
              std::uint64_t compressed_override=0,unsigned extra=0,bool icc=false){
 ImageBytes raw(std::size_t((32768*8+16)*2));for(std::size_t i=0;i<raw.size();++i)raw[i]=std::uint8_t((i*37+i/13)&255);
 // Include binary patterns that must survive without half/color/alpha conversion.
 raw[0]=0;raw[1]=0x80;raw[2]=0xff;raw[3]=0x7f;
 ImageBytes compressed(std::size_t(LZ4_compressBound(int(raw.size()))));const auto size=LZ4_compress_default(reinterpret_cast<const char *>(raw.data()),reinterpret_cast<char *>(compressed.data()),int(raw.size()),int(compressed.size()));require(size>0,"LZ4 fixture compression failed");compressed.resize(size);
 Writer w;w.number<std::uint32_t>(12+extra+(icc?1:0));w.u32("width",width);w.string("version",v2?"v2":"v1");w.i64("pitch",std::int64_t(pitch));w.u32("height",height);if(v2)w.string("format","RGBA16F");else w.i32("format",3);w.u32("flags",0x12345678);w.u64("dataSizeRaw",raw_override?raw_override:raw.size());w.u64("dataSizeLZ4",compressed_override?compressed_override:compressed.size());w.string("compression","lz4");w.i32("colorSpace",123456);w.i32("alphaFormat",-33);w.u64("unknownFutureEnum",UINT64_MAX);
 if(icc)w.u32("ICCdataSize",4);
 for(unsigned i=0;i<extra;++i)w.u64("future_"+std::to_string(i),i);
 const auto header=w.bytes.size();w.bytes.insert(w.bytes.end(),compressed.begin(),compressed.end());if(icc)w.bytes.insert(w.bytes.end(),{1,2,3,4});return {std::move(w.bytes),std::move(raw),header};
}
std::shared_ptr<const EncodedImage> encoded(const Record &r){return std::make_shared<const EncodedImage>(EncodedImage{parse_header(r.bytes,r.bytes.size()),std::make_shared<const ImageBytes>(r.bytes)});}
std::uint64_t fingerprint(std::span<const std::uint8_t> bytes){std::uint64_t result=14695981039346656037ULL;for(auto b:bytes){result^=b;result*=1099511628211ULL;}return result;}
struct Temporary {
 std::filesystem::path path=std::filesystem::temp_directory_path()/("bts-raw-images-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 Temporary(){require(std::filesystem::create_directory(path),"Cannot create private image test directory");}
 ~Temporary(){std::error_code error;std::filesystem::remove_all(path,error);}
};
void checked(herr_t result){require(result>=0,"HDF5 fixture setup failed");}
void put_dataset(hid_t file,const char *name,std::span<const std::uint8_t> bytes,bool filtered=false){
 const hsize_t count=bytes.size();const auto space=H5Screate_simple(1,&count,nullptr);const auto props=H5Pcreate(H5P_DATASET_CREATE);
 if(filtered){const hsize_t chunk=std::min<hsize_t>(count,1024);checked(H5Pset_chunk(props,1,&chunk));checked(H5Pset_deflate(props,1));}
 const auto data=H5Dcreate2(file,name,H5T_STD_U8LE,space,H5P_DEFAULT,props,H5P_DEFAULT);require(data>=0,"HDF5 fixture dataset create failed");if(count)checked(H5Dwrite(data,H5T_NATIVE_UCHAR,H5S_ALL,H5S_ALL,H5P_DEFAULT,bytes.data()));checked(H5Dclose(data));checked(H5Pclose(props));checked(H5Sclose(space));
}
ImageBytes empty_preset(){
 // One empty DataLayersPreset object, using the existing reader's wire format.
 ImageBytes out;const auto put=[&](std::uint64_t n,unsigned size){for(unsigned i=0;i<size;++i)out.push_back(std::uint8_t(n>>(8*i)));};
 put(0x1b7c2fdd,4);put(0,4);put(45,4);put(18,1);put(0,1);put(0,4);const std::string name="DataLayersPreset";put(name.size(),4);out.insert(out.end(),name.begin(),name.end());put(0,2);const auto end=out.size();for(unsigned i=0;i<4;++i)out[14+i]=std::uint8_t(end>>(8*i));return out;
}
std::filesystem::path container(const Temporary &t,const Record &r,bool filtered=false){
 const auto path=t.path/(filtered?"filtered.spsm":"images.spsm");const auto file=H5Fcreate(path.string().c_str(),H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT);require(file>=0,"Cannot create HDF5 image fixture");const auto group=H5Gcreate2(file,"/resources",H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);require(group>=0,"Cannot create resource group");checked(H5Gclose(group));put_dataset(file,"/preset.bin",empty_preset());put_dataset(file,"/resources/pixels.image",r.bytes,filtered);put_dataset(file,"/resources/forbidden.opaque",ImageBytes{0x53,0x42,0x53,0x41,0x52});checked(H5Fclose(file));return path;
}
void synthetic(){
 const auto r=record();const auto h=parse_header(std::span(r.bytes).first(r.header_size),r.bytes.size());require(h->encoded->size()==r.header_size&&h->width==32768&&h->pitch==32768*8+16&&h->height==2,"header metadata/padding or >8K width changed");
 require(std::get<std::int64_t>(h->field("colorSpace")->value)==123456&&std::get<std::int64_t>(h->field("alphaFormat")->value)==-33&&std::get<std::uint64_t>(h->field("unknownFutureEnum")->value)==UINT64_MAX,"raw enum/unknown field changed");
 const auto first=decode(encoded(r));require(*first.bytes==r.raw,"Known-layout byte sequence, row order or padding changed");const auto copied=first;require(copied.bytes.get()==first.bytes.get()&&copied.encoded.get()==first.encoded.get(),"decoded/shared encoded ownership lost");
 const auto v1=record(false);const auto raw_v1=decode(encoded(v1));require(*raw_v1.bytes==v1.raw&&std::get<std::int64_t>(raw_v1.encoded->header->field("format")->value)==3,"v1 opaque format was interpreted/reordered");
 require(first.row_order==Interpretation::UnverifiedStoredBytes&&first.component_order==Interpretation::UnverifiedStoredBytes&&first.color==Interpretation::UnverifiedStoredBytes&&first.alpha==Interpretation::UnverifiedStoredBytes,"unverified appearance metadata was asserted");
 for(std::size_t end=0;end<r.header_size;++end)rejected([&]{parse_header(std::span(r.bytes).first(end),r.bytes.size());});
 auto corrupt=r.bytes;std::fill(corrupt.begin()+r.header_size,corrupt.end(),0xff);rejected([&]{decode(std::make_shared<const EncodedImage>(EncodedImage{h,std::make_shared<const ImageBytes>(corrupt)}));},"LZ4");
 corrupt=r.bytes;corrupt[4]=0;corrupt[5]=0;corrupt[6]=0;corrupt[7]=3;rejected([&]{parse_header(corrupt,corrupt.size());},"Odd UTF-16");
 corrupt=r.bytes;corrupt[8]=0xd8;corrupt[9]=0;rejected([&]{parse_header(corrupt,corrupt.size());},"surrogate");
 corrupt=r.bytes;std::fill(corrupt.begin(),corrupt.begin()+4,0xff);rejected([&]{parse_header(corrupt,corrupt.size());},"field count");
 rejected([&]{parse_header(r.bytes,r.bytes.size()+1);},"spans");rejected([&]{parse_header(r.bytes,r.bytes.size(),{16});},"RAM budget");
 const auto many=record(true,32768,2,32768*8+16,0,0,1024);require(parse_header(many.bytes,many.bytes.size())->fields.size()==1036,"arbitrary header field-count cap");
 const auto huge=record(true,1,1,std::uint64_t(INT32_MAX)+1,std::uint64_t(INT32_MAX)+1);const auto huge_encoded=encoded(huge);rejected([&]{decode(huge_encoded);},"signed-int codec API");
 rejected([&]{const auto overflow=record(true,1,3,std::uint64_t(INT64_MAX));parse_header(overflow.bytes,overflow.bytes.size());});
 const auto mismatch=record(true,1,1,r.raw.size()+1,r.raw.size()+1);rejected([&]{decode(encoded(mismatch));},"raw size mismatch");
 const auto profile=record(true,32768,2,32768*8+16,0,0,0,true);const auto profiled=encoded(profile);require(profiled->header->icc_bytes==4&&*profiled->record==profile.bytes,"unverified ICC/raw record was discarded");rejected([&]{decode(profiled);},"ICC payload placement");
 const auto owned=encoded(r);{bts::TextureRamQueryScope low([]()->std::uint64_t{return 65536;});rejected([&]{decode(owned);},"RAM budget");}rejected([&]{decode(owned,{r.raw.size()});},"RAM budget");
 Temporary t;const auto path=container(t,r);const auto archive=read_archive(path);require(archive.payload_bytes_read==empty_preset().size(),"inventory touched image/package payload");const auto descriptor=describe_image(archive,"/resources/pixels.image");require(descriptor.header_bytes_read==r.header_size,"lazy descriptor read compressed payload");const auto loaded=load_encoded(descriptor);require(*loaded->record==r.bytes&&*decode(loaded).bytes==r.raw,"lazy HDF5 hydrate/decode differed from original bytes");
 rejected([&]{describe_image(archive,"/resources/forbidden.opaque");},"image");rejected([&]{describe_image(archive,"/resources/../forbidden.opaque");},"image");auto forged=descriptor.resource;forged.dataset.path="/resources/forbidden.opaque";rejected([&]{read_image_range(forged,0,1);},"image");
 rejected([&]{read_image_range(descriptor.resource,UINT64_MAX,1);},"bounds");rejected([&]{read_image_range(descriptor.resource,0,UINT64_MAX);},"bounds");rejected([&]{load_encoded(descriptor,{16});},"RAM budget");
 auto image_job=std::async(std::launch::async,[&]{for(int i=0;i<6;++i)require(*decode(load_encoded(describe_image(archive,"/resources/pixels.image"))).bytes==r.raw,"parallel image bytes differ");});auto inventory_job=std::async(std::launch::async,[&]{for(int i=0;i<6;++i)require(read_archive(path).datasets.size()==3,"parallel metadata inventory differs");});image_job.get();inventory_job.get();
 const auto bad=container(t,r,true);Archive fake;fake.source=bad;fake.datasets={{"/resources/pixels.image",r.bytes.size()}};rejected([&]{describe_image(fake,"/resources/pixels.image");},"filtered");
 {const auto file=H5Fopen(path.string().c_str(),H5F_ACC_RDWR,H5P_DEFAULT);require(file>=0,"Cannot create alias fixture");checked(H5Lcreate_hard(file,"/resources/pixels.image",file,"/resources/alias.image",H5P_DEFAULT,H5P_DEFAULT));checked(H5Fclose(file));}Archive aliases;aliases.source=path;aliases.datasets={{"/resources/pixels.image",r.bytes.size()}};rejected([&]{describe_image(aliases,"/resources/pixels.image");},"aliases");rejected([&]{read_image_range(descriptor.resource,0,1);},"changed");
 std::cout<<"PASS raw bitmaps: byte-exact v1/v2/padded/>8K rows, raw enums, all header truncations, LZ4 corruption/int bounds, unknown fields, ICC refusal, low RAM, shared ownership, lazy image-only HDF5/storage/alias/stamp checks and concurrent inventory\n";
}
void real_files(const std::filesystem::path &directory){
 const char *names[]={"dirty-plastic-robot.spsm","shd-hard-rust.spsm","sratched-metal-pealing-paint-dirt.spsm","used_varnish_wood.spsm","worn leather chest.spsm"};unsigned images=0,v1=0,v2=0;
 for(const auto name:names){const auto file=directory/name;const auto original_size=std::filesystem::file_size(file);const auto original_time=std::filesystem::last_write_time(file);const auto archive=read_archive(file);for(const auto &dataset:archive.datasets){if(!dataset.path.starts_with("/resources/")||!dataset.path.ends_with(".image")||dataset.path.find('/',11)!=std::string::npos)continue;
  const auto descriptor=describe_image(archive,dataset.path);const auto &h=*descriptor.header;require(descriptor.header_bytes_read==h.payload_offset&&h.encoded->size()==h.payload_offset,"real descriptor read beyond header");require((h.version=="v1"&&h.payload_offset==345)||(h.version=="v2"&&h.payload_offset==394),"observed real header layout changed");
  const auto a=decode(load_encoded(descriptor));const auto b=decode(load_encoded(descriptor));require(*a.encoded->record==*b.encoded->record&&*a.bytes==*b.bytes,"real repeat encoded/decode not bit-exact");require(a.bytes->size()==h.raw_bytes&&h.raw_bytes==h.pitch*h.height,"real raw row span mismatch");require(fingerprint(*a.bytes)==fingerprint(*b.bytes),"real repeat raw fingerprint mismatch");if(h.version=="v1")++v1;else ++v2;++images;
  std::cout<<"RAW_IMAGE "<<name<<' '<<dataset.path<<" version="<<h.version<<" width="<<h.width<<" height="<<h.height<<" pitch="<<h.pitch<<" header_bytes="<<h.payload_offset<<" encoded_bytes="<<dataset.size<<" raw_bytes="<<h.raw_bytes<<" encoded_fnv1a64="<<std::hex<<fingerprint(*a.encoded->record)<<" raw_fnv1a64="<<fingerprint(*a.bytes)<<std::dec<<" repeat_byte_equal=1 appearance_verified=0\n";
 }require(std::filesystem::file_size(file)==original_size&&std::filesystem::last_write_time(file)==original_time,"source fixture changed");}
 require(images==8&&v1==2&&v2==6,"real embedded image inventory count differs");std::cout<<"PASS all8 raw images repeated byte-equal; source mtimes/sizes unchanged; no package reads or appearance assertions\n";
}
}
int main(int argc,char **argv){try{synthetic();if(argc>1)real_files(argv[1]);return 0;}catch(const std::exception &e){std::cerr<<"Raw bitmap test failed: "<<e.what()<<'\n';return 1;}}
