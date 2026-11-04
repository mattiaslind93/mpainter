#include "FileBrowser.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include <cstring>
#include <mutex>
namespace bts {namespace {
QImage native_thumbnail(const QString &path,bool blend,const blender::ThumbCancellationToken &cancel){
 using namespace blender;
 if(cancel.is_cancelled())return {};
 static std::once_flag directories;std::call_once(directories,[]{IMB_thumb_makedirs();});
 const auto encoded=path.toUtf8();
 // Match filelist_cache_preview_runf: native cache validation + creation and
 // native embedded blend previews. No GPU or Qt widgets on this worker.
 IMB_thumb_locks_acquire();IMB_thumb_path_lock(encoded.constData());
 ImBuf *buffer=IMB_thumb_manage(encoded.constData(),THB_LARGE,blend?THB_SOURCE_BLEND:THB_SOURCE_IMAGE,&cancel);
 IMB_thumb_path_unlock(encoded.constData());IMB_thumb_locks_release();
 if(!buffer)return {};
 QImage result;
 if(!cancel.is_cancelled()){
  IMB_byte_from_float(buffer);
  if(buffer->byte_buffer.data&&buffer->x>0&&buffer->y>0){
   result=QImage(buffer->x,buffer->y,QImage::Format_RGBA8888);
   for(int y=0;y<buffer->y;++y)std::memcpy(result.scanLine(y),buffer->byte_buffer.data+std::size_t(buffer->y-y-1)*buffer->x*4,std::size_t(buffer->x)*4);
  }
 }
 IMB_freeImBuf(buffer);return result;
}
// Translation-unit registration keeps the widget-only tests independent of
// Blender's link graph, while production always installs the native decoder.
const bool installed=[]{set_browser_thumbnail_loader(native_thumbnail);return true;}();
}}
