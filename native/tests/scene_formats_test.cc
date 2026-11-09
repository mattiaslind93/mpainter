// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneBridge.hh"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <cassert>
#include <bit>
#include <cmath>
#include <iostream>

using namespace bts;
void save(const QString &path,const QByteArray &bytes){QFile file(path);assert(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size());}
QJsonArray array(std::initializer_list<double> values){QJsonArray a;for(auto v:values)a.append(v);return a;}
void append32(QByteArray &data,quint32 value){char b[4];qToLittleEndian(value,b);data.append(b,4);}
QByteArray fixture(bool glb){
 QByteArray data;for(float value:{0.f,0.f,0.f,1.f,0.f,0.f,1.f,1.f,0.f,0.f,1.f,0.f})append32(data,std::bit_cast<quint32>(value));
 for(float value:{0.f,0.f,2.f,0.f,2.f,1.f,0.f,1.f})append32(data,std::bit_cast<quint32>(value));
 for(quint16 value:{0,1,2,0,2,3}){char b[2];qToLittleEndian(value,b);data.append(b,2);}
 QImage image(2,2,QImage::Format_RGBA8888);image.fill(QColor(220,40,70));QByteArray png;QBuffer output(&png);assert(output.open(QIODevice::WriteOnly)&&image.save(&output,"PNG"));
 QJsonArray views{QJsonObject{{"buffer",0},{"byteOffset",0},{"byteLength",48}},QJsonObject{{"buffer",0},{"byteOffset",48},{"byteLength",32}},QJsonObject{{"buffer",0},{"byteOffset",80},{"byteLength",12}}};
 QJsonObject image_ref;
 if(glb){image_ref={{"bufferView",3},{"mimeType","image/png"}};views.append(QJsonObject{{"buffer",0},{"byteOffset",data.size()},{"byteLength",png.size()}});data.append(png);}else image_ref={{"uri","data:image/png;base64,"+QString::fromLatin1(png.toBase64())}};
 QJsonObject buffer{{"byteLength",data.size()}};if(!glb)buffer["uri"]="data:application/octet-stream;base64,"+QString::fromLatin1(data.toBase64());
 QJsonObject root{{"asset",QJsonObject{{"version","2.0"}}},{"scene",0},
 {"scenes",QJsonArray{QJsonObject{{"nodes",QJsonArray{0,1}}}}},
 {"nodes",QJsonArray{QJsonObject{{"name","TranslatedCard"},{"mesh",0},{"translation",array({2,3,4})}},QJsonObject{{"name","ReferenceCamera"},{"camera",0},{"translation",array({0,0,5})}}}},
 {"cameras",QJsonArray{QJsonObject{{"type","perspective"},{"perspective",QJsonObject{{"yfov",3.14159265358979323846/4},{"aspectRatio",1.5},{"znear",.01},{"zfar",200}}}}}},
 {"buffers",QJsonArray{buffer}},{"bufferViews",views},
 {"accessors",QJsonArray{QJsonObject{{"bufferView",0},{"componentType",5126},{"count",4},{"type","VEC3"},{"min",array({0,0,0})},{"max",array({1,1,0})}},QJsonObject{{"bufferView",1},{"componentType",5126},{"count",4},{"type","VEC2"}},QJsonObject{{"bufferView",2},{"componentType",5123},{"count",6},{"type","SCALAR"}}}},
 {"images",QJsonArray{image_ref}},{"textures",QJsonArray{QJsonObject{{"source",0}}}},
 {"materials",QJsonArray{QJsonObject{{"name","CardSurface"},{"pbrMetallicRoughness",QJsonObject{{"baseColorTexture",QJsonObject{{"index",0}}},{"metallicFactor",.2},{"roughnessFactor",.65}}}}}},
 {"meshes",QJsonArray{QJsonObject{{"primitives",QJsonArray{QJsonObject{{"attributes",QJsonObject{{"POSITION",0},{"TEXCOORD_0",1}}},{"indices",2},{"material",0}}}}}}}};
 auto json=QJsonDocument(root).toJson(QJsonDocument::Compact);if(!glb)return json;
 while(json.size()%4)json.append(' ');while(data.size()%4)data.append(char(0));
 QByteArray bytes;append32(bytes,0x46546c67);append32(bytes,2);append32(bytes,quint32(12+8+json.size()+8+data.size()));append32(bytes,json.size());append32(bytes,0x4e4f534a);bytes.append(json);append32(bytes,data.size());append32(bytes,0x004e4942);bytes.append(data);return bytes;
}
int main(){QTemporaryDir folder(QDir::currentPath()+"/scene-formats-XXXXXX");assert(folder.isValid());for(bool binary:{false,true}){
 const auto source=folder.filePath(binary?"card.glb":"card.gltf");save(source,fixture(binary));auto result=convert_mesh_scene(source.toStdString(),true);
 assert(result->cameras.size()==1);const auto &camera=result->cameras.front();std::cerr<<"format="<<(binary?"glb":"gltf")<<" fov="<<camera.vertical_fov<<" near="<<camera.near_clip<<" far="<<camera.far_clip<<" aspect="<<camera.aspect<<"\n";assert(std::abs(camera.vertical_fov-45)<1e-3&&std::abs(camera.near_clip-.01)<1e-6&&std::abs(camera.far_clip-200)<1e-3);assert(std::abs(camera.position[1]+5)<1e-6);
 assert(result->embedded_images.size()==1);const QImage image(QString::fromStdString(result->embedded_images.front()));assert(!image.isNull()&&image.pixelColor(0,0)==QColor(220,40,70));
 Assimp::Importer reader;const auto *scene=reader.ReadFile(result->obj_path,0);assert(scene&&scene->mNumMeshes==1);const auto *mesh=scene->mMeshes[0];assert(mesh->mNumFaces==2&&mesh->HasTextureCoords(0));bool second_tile=false;for(unsigned i=0;i<mesh->mNumVertices;++i){const auto p=mesh->mVertices[i];assert(p.x>=2&&p.x<=3&&p.y>=3&&p.y<=4&&std::abs(p.z-4)<1e-6);second_tile|=mesh->mTextureCoords[0][i].x>1;}assert(second_tile);
 auto excluded=convert_mesh_scene(source.toStdString(),false);assert(excluded->cameras.empty());
 }
 std::cout<<"glTF/GLB in-process geometry, transform, UV tiles, camera optics and embedded image passed\n";
}
