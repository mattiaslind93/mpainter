#pragma once
#include "BakedMapShelf.hh"
#include <QCache>
#include <QImage>
#include <QStringList>
namespace bts {
struct BakedShelfItem {QString id,name;QImage thumbnail;QString description;};
inline QString baked_map_title(const QString &set,int role){const std::array<const char *,7> names{"Normal","World Space Normal","ID","Ambient Occlusion","Curvature","Position","Thickness"};return set+QString::fromUtf8(" – ")+QString::fromLatin1(names.at(role));}
inline BakedShelfItem present_baked_map(const QUuid &set,const QString &name,int role,const MeshMap &map){
 BakedShelfItem result;result.id=baked_shelf_id(set,role);result.name=baked_map_title(name,role);QStringList tiles;for(const auto &[tile,plane]:map.tiles)tiles.append(QString::number(tile));
 if(map.tiles.empty())return result;const auto &[tile,plane]=*map.tiles.begin();result.description=result.name+QString("\n%1 × %2 px · %3 UV tiles: %4\nPreview: tile %5").arg(plane.resolution).arg(plane.rows()).arg(map.tiles.size()).arg(tiles.join(", ")).arg(tile);
 if(!plane.pixels)return result;
 // Immutable bake data already lives in RAM. Sample a small preview, cache by
 // content/role, and never decode/re-encode the full map to populate the Shelf.
 static QCache<QString,QImage> previews(4*1024*1024);const auto key=QString::fromStdString(plane.hash)+":"+QString::number(role)+":"+QString::number(plane.resolution)+":"+QString::number(plane.rows());
 if(const auto *cached=previews.object(key)){result.thumbnail=*cached;return result;}
 QImage image(64,64,QImage::Format_RGB32);const auto &data=*plane.pixels;
 for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto index=(std::size_t((63-y)*data.height/64)*data.resolution+x*data.resolution/64)*4;const float alpha=std::clamp(data.rgba[index+3],0.f,1.f);const int checker=((x/8+y/8)&1)?54:40;int rgb[3];for(int c=0;c<3;++c)rgb[c]=qRound(checker*(1-alpha)+255*std::clamp(data.rgba[index+((role==3||role==4||role==6)?0:c)],0.f,1.f)*alpha);image.setPixel(x,y,qRgb(rgb[0],rgb[1],rgb[2]));}
 result.thumbnail=image;previews.insert(key,new QImage(image),int(image.sizeInBytes()));return result;
}
}
