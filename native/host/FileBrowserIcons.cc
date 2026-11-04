#include "FileBrowser.hh"
#include <QApplication>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <array>
namespace bts {
BrowserFileType browser_file_type(const QString &path){
 // Blender filelist.cc ED_path_extension_type/ED_file_extension_icon, in its
 // precedence order. App-supported USD/3D and material containers extend it.
 const auto name=path.mid(path.lastIndexOf('/')+1).toLower();
 if(name.endsWith(".blend.gz"))return BrowserFileType::Blend;
 const auto dot=name.lastIndexOf('.');const auto suffix=dot<0?QString():name.mid(dot+1);
 if(suffix.startsWith("blend")&&suffix.size()>5){bool digits=true;for(auto c:suffix.mid(5))digits&=c.isDigit();if(digits)return BrowserFileType::Backup;}
 static const QHash<QString,BrowserFileType> extensions=[]{
  QHash<QString,BrowserFileType> values;
  const auto add=[&](BrowserFileType type,const char *items){for(const auto &item:QString::fromLatin1(items).split(' '))values.insert(item,type);};
  add(BrowserFileType::Blend,"blend ble");
  add(BrowserFileType::Script,"py");
  add(BrowserFileType::Text,"txt glsl osl data pov ini mcr inc fountain toml");
  add(BrowserFileType::Font,"ttf pfb otf woff woff2");
  add(BrowserFileType::Object,"abc obj mtl 3ds fbx glb gltf svg pdf ply stl usd usda usdc usdz dae dxf lwo lws off 3mf");
  add(BrowserFileType::Volume,"vdb");add(BrowserFileType::Archive,"zip");
  add(BrowserFileType::Image,"png tga bmp jpg jpeg sgi rgb rgba tif tiff tx avif jp2 j2c hdr dds dpx cin exr psd pdd psb webp ktx ktx2");
  add(BrowserFileType::Movie,"avi flc mov movie mp4 m4v m2v m2t m2ts mts ts mv avs wmv ogv r3d dv mpeg mpg mpg2 vob mkv flv divx xvid mxf webm gif");
  // .ogg is ambiguous upstream (MOV_is_movie_file reads its header). Keep the
  // common audio icon here: file-icon lookup must never probe media on disk.
  add(BrowserFileType::Sound,"wav ogg oga mp3 mp2 ac3 aac flac wma eac3 aif aiff m4a mka opus");
  add(BrowserFileType::SmartMaterial,"spsm");add(BrowserFileType::MaterialArchive,"sbsar");return values;
 }();
 return extensions.value(suffix,BrowserFileType::Blank);
}
QIcon browser_file_icon(BrowserFileType type){
 static const auto icons=[]{
  std::array<QIcon,int(BrowserFileType::Count)> result;
  const QImage atlas(":/file-browser/atlas.png");
  int y=0;for(int size:{16,24,32,48,64,96,128,192,256}){
   for(int column=0;column<int(result.size());++column){
    QImage image=atlas.copy(column*256,y,size,size);
    QPainter painter(&image);painter.setCompositionMode(QPainter::CompositionMode_SourceIn);painter.fillRect(image.rect(),QApplication::palette().color(QPalette::Text));painter.end();
    result[column].addPixmap(QPixmap::fromImage(image));
   }y+=size;
  }return result;
 }();
 return icons.at(int(type));
}
QIcon browser_file_large_icon(BrowserFileType type){
 // Blender file_draw_special_image: large folder/document container with
 // centered, contrasting file-type overlay. Every size is cached once.
 static const auto icons=[]{
  std::array<QIcon,int(BrowserFileType::Count)> result;
  for(int index=0;index<int(result.size());++index){
   const auto type=BrowserFileType(index);
   const bool folder=type==BrowserFileType::Folder||type==BrowserFileType::Redirect||type==BrowserFileType::FolderLarge;
   const bool parent=type==BrowserFileType::Parent||type==BrowserFileType::ParentLarge;
   const auto container=parent?BrowserFileType::ParentLarge:folder?BrowserFileType::FolderLarge:BrowserFileType::FileLarge;
   for(int size:{16,24,32,48,64,96,128,192,256}){
    auto image=browser_file_icon(container).pixmap(size,size).toImage();
    if(!parent&&type!=BrowserFileType::Folder&&type!=BrowserFileType::FolderLarge&&type!=BrowserFileType::Blank&&type!=BrowserFileType::FileLarge){
     const int overlay_size=qRound(size*.48);auto overlay=browser_file_icon(type).pixmap(overlay_size,overlay_size).toImage();QPainter tint(&overlay);tint.setCompositionMode(QPainter::CompositionMode_SourceIn);tint.fillRect(overlay.rect(),QApplication::palette().color(QPalette::Base));tint.end();
     QPainter painter(&image);painter.setOpacity(.75);painter.drawImage((size-overlay_size)/2,(size-overlay_size)/2+qRound(size*.09),overlay);
    }
    result[index].addPixmap(QPixmap::fromImage(image));
   }
  }return result;
 }();return icons.at(int(type));
}
namespace {BrowserThumbnailLoader &loader_storage(){static BrowserThumbnailLoader loader;return loader;}}
void set_browser_thumbnail_loader(BrowserThumbnailLoader loader){loader_storage()=std::move(loader);}
BrowserThumbnailLoader browser_thumbnail_loader(){return loader_storage();}
}
