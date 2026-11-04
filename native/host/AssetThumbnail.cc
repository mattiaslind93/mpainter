#include "Theme.hh"
#include "AssetThumbnail.hh"
#include "MaterialSpherePreview.hh"
#include "SpsmPreview.hh"
#include "SpsmAssetDispatch.hh"
#include "BlendMaterial.hh"
#include "NativeMaterialAsset.hh"
#include "ImageSequence.hh"
#include "AlphaPanel.hh"
#include "PaintPixels.hh"
#include "SmartMaterial.hh"
#include <QCache>
#include <QBuffer>
#include <QImageReader>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QTimer>
#include <deque>
#include <future>
#include <set>
#include <QPainter>
#include <cmath>
#include <memory>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
namespace bts {
namespace {
QImage build_thumbnail(const StudioAsset &asset,bool &native_missing){
  QString cached_path;
  if(asset.usages.contains("texture")&&!asset.texture_files.empty()){
    const auto &file=asset.texture_files.front();const QFileInfo source(file.path);
    if(source.dir().dirName()=="blobs"&&source.completeBaseName()==file.sha256){cached_path=source.dir().filePath(asset.hash()+".thumbnail-v1.png");QImageReader reader(cached_path);if(reader.size()==QSize(58,58)){auto cached=reader.read();if(!cached.isNull())return cached;}}
  }
  const auto sequence=is_image_sequence(asset.bytes)?read_image_sequence(asset.bytes):std::vector<SequenceMember>{};const auto &source=sequence.empty()?asset.bytes:sequence.front().bytes;
  QImage image(58,58,QImage::Format_RGB32);image.fill(QColor(bts::theme::Surface));
  if(asset.brush_preset){native_missing=true;}
  else if(asset.smart_mask&&!is_spmsk_asset(asset)){
    native_missing=true;
    image=smart_mask_sphere_preview(*asset.smart_mask,84);
  }
  else if(is_spmsk_asset(asset)&&!qEnvironmentVariableIsSet("BTS_BN_SPMSK_UI")){
    native_missing=true;
    try{
      StudioAsset copy=asset;
      if(!copy.smart_mask)hydrate_asset_presets(copy);
      if(copy.smart_mask)image=smart_mask_sphere_preview(*copy.smart_mask,84);
    }catch(...){}
  }
  else if(asset.projection_weave&&asset.material){ProjectionMaterial source;source.values=*asset.material;source.weave=true;source.density=20;for(int y=0;y<58;++y)for(int x=0;x<58;++x){const auto value=source.local({2*(x+.5)/58-1,1-2*(y+.5)/58});int rgb[3];for(int c=0;c<3;++c)rgb[c]=qRound(255*std::pow(std::clamp(value.base_color[c],0.f,1.f),1/2.2));image.setPixel(x,y,qRgb(rgb[0],rgb[1],rgb[2]));}}
  else if(is_spsm_archive_asset(asset)){
    const auto encoded=spsm_embedded_preview(asset.bytes);QBuffer buffer;buffer.setData(encoded);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer);auto extent=reader.size();
    // Default previews in the supplied files use the public WEBP VP8X header.
    if(!extent.isValid()&&encoded.size()>=30&&encoded.startsWith("RIFF")&&encoded.mid(8,8)=="WEBPVP8X"){
      const auto size=[&](int offset){return 1+int(static_cast<unsigned char>(encoded[offset]))+(int(static_cast<unsigned char>(encoded[offset+1]))<<8)+(int(static_cast<unsigned char>(encoded[offset+2]))<<16);};extent=QSize(size(24),size(27));
    }
    QImage preview;if(extent.isValid()){require_texture_ram(resource_product(resource_product(extent.width(),extent.height()),8),"SPSM shaderball decode");preview=reader.read();}
    if(preview.isNull()&&extent.isValid()&&!encoded.isEmpty()){
      using Buffer=std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>;Buffer native(blender::IMB_load_image_from_memory(reinterpret_cast<const unsigned char *>(encoded.constData()),encoded.size(),blender::ImBufFlags::NoColorspaceConvert|blender::ImBufFlags::AlphaChannelPacked,"SPSM embedded shaderball"),blender::IMB_freeImBuf);
      if(native&&native->byte_data()&&native->x>0&&native->y>0){require_texture_ram(resource_product(resource_product(native->x,native->y),4),"SPSM shaderball preview");preview=QImage(native->x,native->y,QImage::Format_RGBA8888);for(int y=0;y<native->y;++y)std::copy_n(native->byte_data()+std::size_t(native->y-y-1)*native->x*4,std::size_t(native->x)*4,preview.scanLine(y));}
    }
    if(!preview.isNull()){const auto scaled=preview.scaled(58,58,Qt::KeepAspectRatio,Qt::SmoothTransformation);QPainter painter(&image);painter.drawImage((58-scaled.width())/2,(58-scaled.height())/2,scaled);}
    else{native_missing=is_spmsk_asset(asset)||bool(asset.smart_mask);QPainter painter(&image);painter.setPen(QPen(QColor(bts::theme::Text),2));painter.setBrush(QColor(bts::theme::Border));painter.drawEllipse(QRectF(8,8,42,42));}
  }
  else if(asset.smart_material)image=smart_material_sphere_preview(asset.material_preset.get(),84);
  else if(const auto saved=blend_material_preview(asset);!saved.isNull()){QPainter painter(&image);auto scaled=saved.scaled(58,58,Qt::KeepAspectRatio,Qt::SmoothTransformation);painter.drawImage((58-scaled.width())/2,(58-scaled.height())/2,scaled);}
  else if(is_native_material_asset(asset)){native_missing=true;} // Pending native GPU preview, never uniform defaults.
  else if(asset.material)image=material_sphere_preview(*asset.material,asset.fill_preset?&*asset.fill_preset:nullptr,84);
  else if(asset.alpha)image=AlphaPanel::thumbnail(*asset.alpha);
  else if(!asset.texture_files.empty()){
    const auto name=asset.texture_files.front().path.toUtf8();char space[64]{};using Buffer=std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>;Buffer native(blender::IMB_thumb_load_image(name.constData(),58,space,blender::IMBThumbLoadFlags::LoadLargeFiles),blender::IMB_freeImBuf);
    if(native&&native->x>0&&native->y>0&&(native->float_data()||native->byte_data())){const int h=std::clamp(qRound(58.*native->y/native->x),1,58);QImage decoded(58,h,QImage::Format_RGB32);for(int y=0;y<h;++y)for(int x=0;x<58;++x){const auto index=std::size_t((h-y-1)*native->y/h)*native->x+x*native->x/58;int rgb[3];for(int c=0;c<3;++c){const float value=native->float_data()?native->float_data()[index*native->channels+std::min(c,native->channels-1)]:native->byte_data()[index*4+c]/255.f;rgb[c]=qRound(255*std::pow(std::clamp(value/(1+std::max(0.f,value)),0.f,1.f),1/2.2));}decoded.setPixel(x,y,qRgb(rgb[0],rgb[1],rgb[2]));}QPainter painter(&image);painter.drawImage(0,(58-h)/2,decoded);}
  }
  else if(!asset.bytes.isEmpty()){
    auto decoded=QImage::fromData(source);
    if(decoded.isNull()){
      using Buffer=std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>;Buffer native(blender::IMB_load_image_from_memory(reinterpret_cast<const unsigned char *>(source.constData()),source.size(),blender::ImBufFlags::NoColorspaceConvert|blender::ImBufFlags::AlphaChannelPacked,"Shelf thumbnail"),blender::IMB_freeImBuf);
      if(native&&native->x>0&&native->y>0&&(native->float_data()||native->byte_data())){const int h=std::clamp(qRound(58.*native->y/native->x),1,58);decoded=QImage(58,h,QImage::Format_RGB32);for(int y=0;y<h;++y)for(int x=0;x<58;++x){const auto index=std::size_t((h-y-1)*native->y/h)*native->x+x*native->x/58;int rgb[3];for(int c=0;c<3;++c){const float value=native->float_data()?native->float_data()[index*native->channels+std::min(c,native->channels-1)]:native->byte_data()[index*4+c]/255.f;rgb[c]=qRound(255*std::pow(std::clamp(value/(1+std::max(0.f,value)),0.f,1.f),1/2.2));}decoded.setPixel(x,y,qRgb(rgb[0],rgb[1],rgb[2]));}}
    }
    if(!decoded.isNull()){const auto scaled=decoded.scaled(58,58,Qt::KeepAspectRatio,Qt::SmoothTransformation);QPainter painter(&image);painter.drawImage((58-scaled.width())/2,(58-scaled.height())/2,scaled);}
  }
  if(image.size()!=QSize(84,84))image=image.scaled(58,58,Qt::KeepAspectRatio,Qt::SmoothTransformation);if(asset.alpha&&asset.alpha->kind==AlphaKind::DynamicOutline){QPainter painter(&image);painter.setPen(Qt::NoPen);painter.setBrush(QColor(bts::theme::Warning));painter.drawPolygon(QPolygonF{{46,58},{58,46},{58,58}});}
  const int tile_count=!asset.texture_files.empty()&&asset.texture_files.front().tile?int(asset.texture_files.size()):int(sequence.size());
  if(tile_count){QPainter painter(&image);painter.setPen(QColor(bts::theme::Heading));painter.setBrush(bts::theme::alpha(bts::theme::Canvas, 220));painter.drawRoundedRect(QRectF(32,32,25,25),3,3);auto font=painter.font();font.setBold(true);font.setPixelSize(17);painter.setFont(font);painter.drawText(QRectF(32,32,25,25),Qt::AlignCenter,QString::number(tile_count));}
  if(!cached_path.isEmpty()){QSaveFile cached(cached_path);if(cached.open(QIODevice::WriteOnly)&&image.save(&cached,"PNG"))cached.commit();}
  return image;
}
struct ThumbnailJobs:QObject {
 QCache<QString,QImage> cache{32*1024*1024};struct Request{QString key;StudioAsset asset;};std::deque<Request> queue;std::set<QString> pending;struct Result{QString first;QImage second;StudioAsset asset;bool native_missing=false;};std::future<Result> job;QPointer<QObject> native_owner;std::function<void(const StudioAsset &,std::function<void(QImage)>)> native_generator;QTimer poll;std::vector<std::pair<QPointer<QObject>,std::function<void()>>> callbacks;bool stopped=false;
 ThumbnailJobs(){poll.setInterval(16);connect(&poll,&QTimer::timeout,this,[this]{if(job.valid()&&job.wait_for(std::chrono::seconds(0))==std::future_status::ready){try{auto result=job.get();pending.erase(result.first);cache.insert(result.first,new QImage(result.second),int(result.second.sizeInBytes()));if(result.native_missing&&native_owner&&native_generator){const auto key=result.first;native_generator(result.asset,[this,key](QImage source){if(stopped)return;const int target=(source.size()==QSize(84,84))?84:58;QImage image(target,target,QImage::Format_RGB32);image.fill(QColor(source.isNull()?bts::theme::ErrorSurface:bts::theme::Surface));QPainter painter(&image);const auto scaled=(source.size()==QSize(target,target))?source:source.scaled(target,target,Qt::KeepAspectRatio,Qt::SmoothTransformation);painter.drawImage((target-scaled.width())/2,(target-scaled.height())/2,scaled);painter.end();cache.insert(key,new QImage(image),int(image.sizeInBytes()));for(auto &[owner,callback]:callbacks)if(owner)callback();});}for(auto &[owner,callback]:callbacks)if(owner)callback();}catch(...){pending.clear();}next();}if(!job.valid())poll.stop();});}
 ~ThumbnailJobs(){stop();}
 void next(){if(stopped||job.valid()||queue.empty())return;auto request=std::move(queue.front());queue.pop_front();job=std::async(std::launch::async,[request=std::move(request)]()mutable{try{bool missing=false;auto image=build_thumbnail(request.asset,missing);
 // Cold SPMSK metadata deliberately has no smart_mask pointer on the GUI
 // thread. Prepare only the missing native preview here, on this worker, so
 // the GPU owner receives the actual retained source rather than a null mask.
 if(missing&&is_spmsk_asset(request.asset)&&!request.asset.smart_mask)hydrate_asset_presets(request.asset);
 return Result{request.key,std::move(image),request.asset,missing};}catch(...){QImage error(58,58,QImage::Format_RGB32);error.fill(QColor(bts::theme::ErrorSurface));return Result{request.key,error,{}};}});poll.start();}
 void stop(){stopped=true;queue.clear();poll.stop();if(job.valid())job.wait();}
};
ThumbnailJobs &thumbnails(){static ThumbnailJobs value;return value;}
}
QImage asset_thumbnail(const StudioAsset &asset){auto &state=thumbnails();const auto key=asset.id.toString()+":"+(asset.revision?QString::number(asset.revision):asset.hash());if(const auto *image=state.cache.object(key))return *image;if(!state.stopped&&state.pending.insert(key).second){state.queue.push_back({key,asset});state.next();}QImage placeholder(58,58,QImage::Format_RGB32);placeholder.fill(QColor(bts::theme::Surface));return placeholder;}
void asset_thumbnail_native_generator(QObject *owner,std::function<void(const StudioAsset &,std::function<void(QImage)>)> generator){auto &state=thumbnails();state.native_owner=owner;state.native_generator=std::move(generator);}
void asset_thumbnail_ready(QObject *owner,std::function<void()> callback){thumbnails().callbacks.emplace_back(owner,std::move(callback));}
void stop_asset_thumbnail_jobs(){thumbnails().stop();}
}
