#include "TextureExtent.hh"
#include "PaintPixels.hh"
#include "PaintRasterSource.hh"
#include "PaintRecording.hh"
#include "EffectPaint.hh"
#include "SparsePresetIO.hh"
#include "MeshMaps.hh"
#include <QCoreApplication>
#include <QDir>
#include <cassert>
#include <iostream>
using namespace bts;
namespace {
struct DeferredSource final:PaintRasterSource {
 mutable int evaluations=0;
 PaintPixels evaluate_cpu()const override{++evaluations;throw std::runtime_error("Metadata must not rasterize");}
};
template<class Function>bool rejects(Function &&function){try{function();}catch(const std::exception &){return true;}return false;}
void stream_format(QDataStream &stream){stream.setVersion(QDataStream::Qt_6_8);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);}
}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);assert(argc==2);qputenv("BTS_RECOVERY_DIR",QByteArray(argv[1]));QDir().mkpath(QString::fromLocal8Bit(argv[1]));
 static_assert(sparse_texture_max_side==4194304);
 assert(valid_sparse_texture_extent(16384,128,true)&&valid_sparse_texture_extent(sparse_texture_max_side,128,true));
 assert(!valid_sparse_texture_extent(sparse_texture_max_side+1)&&!valid_sparse_texture_extent(129,128,true));
 assert(sparse_texture_page_count(65)==4&&sparse_texture_page_count(65,1)==2);
 assert(sparse_texture_page_count(sparse_texture_max_side)==(1ULL<<32));
 assert(texture_buffer_bytes(16384,16384,16)==4ULL*1024*1024*1024);
 assert(rejects([]{texture_buffer_bytes(sparse_texture_max_side,sparse_texture_max_side,UINT64_MAX);}));
 texture_gpu_max_size=8192;assert(rejects([]{require_gpu_texture_extent(16384,16384,"GPU fixture");}));
 texture_gpu_max_size=32768;require_gpu_texture_extent(16384,16384,"GPU fixture");
 texture_gpu_max_size=4096;assert(rejects([]{require_gpu_texture_extent(8192,128,"GPU fixture");}));texture_gpu_max_size=0;
 std::array<PaintPixels::Pixel,64*64> page{};page.back()={.25f,0,0,.5f};
 const auto before=PaintPixels::resident_bytes();PaintPixels pixels;
 {
  TextureRamQueryScope ram([]()->std::uint64_t{return 1024*1024;});
  pixels.assign_page(16384,0,(255u<<16)|255u,page);assert(pixels.page_count(0)==1&&pixels.sample(0,16383,16383)==page.back());
  assert(PaintPixels::resident_bytes()==before+sizeof(page));
  PaintPixels largest;largest.assign_page(sparse_texture_max_side,0,UINT32_MAX,page);assert(largest.sample(0,sparse_texture_max_side-1,sparse_texture_max_side-1)==page.back());
  assert(rejects([&]{PaintPixels invalid;invalid.assign_page(sparse_texture_max_side+1,0,0,page);}));
  assert(rejects([&]{PaintPixels invalid;invalid.assign_page(16384,0,256,page);}));
  TilePaintPixels tiled;tiled.assign_page(16384,0,(255u<<16)|255u,page,1001);QByteArray encoded;QDataStream write(&encoded,QIODevice::WriteOnly);stream_format(write);std::uint64_t decoded=0;write_preset_plane(write,tiled,decoded,1024*1024);assert(decoded==sizeof(page));
  QDataStream read(encoded);stream_format(read);decoded=0;const auto restored=read_preset_plane(read,{1001},decoded,1024*1024);assert(read.atEnd()&&restored.sample(0,16383,16383)==page.back());
  TextureRamQueryScope empty([]()->std::uint64_t{return 0;});
  PaintPixels admitted;admitted.assign_page(16384,0,0,page);assert(admitted.resolution()==16384&&admitted.page_count(0)==1&&admitted.sample(0,63,63)==page.back());
  PaintPixels::PageVersions versions;versions[0][UINT32_MAX]=1;auto source=std::make_shared<DeferredSource>();auto retained=PaintPixels::from_source(sparse_texture_max_side,source,versions);PaintMetadataScope metadata;assert(retained.page_count(0)==1&&source->evaluations==0);
  QJsonArray planes;for(int i=0;i<11;++i)planes.append(QJsonArray{});
  QJsonArray counts{double(1ULL<<32),0,0,0,0},hashes{"0","0","0","0","0"};planes[0]=QJsonArray{QJsonObject{{"number",1001},{"pages",counts},{"hashes",hashes}}};
  QJsonObject description{{"resolution",sparse_texture_max_side},{"planes",planes}};assert(read_effect_paint(description).resolution==sparse_texture_max_side);
  counts[0]=double((1ULL<<32)+1);planes[0]=QJsonArray{QJsonObject{{"number",1001},{"pages",counts},{"hashes",hashes}}};description["planes"]=planes;assert(rejects([&]{read_effect_paint(description);}));
 }
 // Width is large while the actual immutable result is only 8 MiB, not 4 GiB.
 std::vector<float> rectangle(std::size_t(16384)*32*4,0);{TextureRamQueryScope empty([]()->std::uint64_t{return 0;});MeshMapPixels baked(16384,std::move(rectangle),32);assert(baked.height==32&&baked.resolution==16384);}
 // A tiny footprint exercises wide row math and seam padding without a dense image.
 PaintPixels surface_pixels;PaintPixels::Stamp stamp{};stamp.channels={true,false,false,false,false};stamp.hardness=1;
 auto footprint=std::make_shared<PaintPixels::SurfaceFootprint>();footprint->center={1e15,1e15,1e15};footprint->radius=1e15;
 PaintPixels::SurfaceTriangle triangle{};triangle.uv={{{.5,.5},{.5+4./65536,.5},{.5,.5+4./65536}}};triangle.position={{{1e15,1e15,1e15},{1e15+1,1e15,1e15},{1e15,1e15+1,1e15}}};triangle.seam_edges={true,true,true};footprint->triangles.push_back(triangle);stamp.surface=footprint;surface_pixels.stamp(65536,stamp);assert(surface_pixels.sample(0,32768,32768)[3]>0);
 auto outside=std::make_shared<PaintPixels::SurfaceFootprint>();outside->radius=1;triangle.uv={{{2,2},{3,2},{2,3}}};outside->triangles.assign(200001,triangle);stamp.surface=outside;assert(surface_pixels.stamp(65536,stamp).empty());
 stamp.surface.reset();stamp.center={1e6,1e6};stamp.axis_x={.1,0};stamp.axis_y={0,.1};assert(surface_pixels.stamp(65536,stamp).empty());
 std::cout<<"Texture extents: 16K/max-key single pages, uint64 ceil grids, sparse preset roundtrip, metadata without dense RAM/GPU charge, actual GPU caps, 64K surface math and >200K footprints PASS\n";
}
