// SPDX-License-Identifier: GPL-3.0-or-later
#include "RibbonIO.hh"
#include "SurfacePathIO.hh"
#include "TextureDocument.hh"
#include "StrokeArchive.hh"
#include <QJsonArray>
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
 SurfacePath path;path.id="c0a80001-1234-4000-8000-000000000001";path.points={{{0,0,0},{0,0,1},{.2,.5},1},{{1,0,0},{0,0,1},{.8,.5},1}};path.ribbon.enabled=true;path.ribbon.count=3;path.ribbon.tiling=RibbonTiling::Custom;path.ribbon.flip=path.ribbon.mirror=true;
 const auto standalone=surface_path_json(path);auto restored=read_surface_path(standalone);assert(surface_path_json(*restored)==standalone&&restored->ribbon.image->pixels==path.ribbon.image->pixels);
 auto legacy=standalone;legacy["version"]=3;legacy.remove("ribbon");assert(!read_surface_path(legacy)->ribbon.enabled);
 NewProjectOptions options;options.absolute_mesh_path="/ribbon.obj";options.resolution=DocumentResolution::Pixels128;ImportSummary imported;imported.materials={{1,"Ribbon"}};SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Ribbon";material.uniform_editable=true;scene.materials.push_back(material);TextureDocument doc(options,imported,scene);auto &set=doc.set(1);auto &layer=set.layers[0];PaintPixels::Stamp stamp{{.5,.5},{.1,0},{0,.1}};TilePaintPixels ink;ink.stamp(128,stamp,set.tiles);StrokeRaster raster{ink,{}};auto definition=std::make_shared<SurfacePath>(path);layer.strokes=append_surface_path({},{},raster,definition,128);layer.pixels=replay_strokes(*layer.strokes,128).paint;
 auto duplicate=std::make_shared<SurfacePath>(path);duplicate->id="c0a80001-1234-4000-8000-000000000002";layer.strokes=append_surface_path(layer.strokes,{layer.pixels,{}},raster,duplicate,128);layer.pixels=replay_strokes(*layer.strokes,128).paint;
 const auto json=doc.to_json();assert(json["schema"]==bts::texture_document_schema&&json["ribbon_images"].toObject().size()==1);const std::array bindings{TextureSetBinding{set.id,1,"Ribbon"}};auto loaded=TextureDocument::from_json(json,bindings);loaded.set(1).layers[0].pixels=layer.pixels;read_stroke_archive(loaded,write_stroke_archive(doc));const auto &operations=loaded.set(1).layers[0].strokes->operations;assert(operations[0]->path->ribbon.image==operations[1]->path->ribbon.image);assert(loaded.to_json()==json);
 auto corrupt=json;auto images=json["ribbon_images"].toObject();images[images.begin().key()]="AAAAAA==";corrupt["ribbon_images"]=images;bool rejected=false;try{TextureDocument::from_json(corrupt,bindings);}catch(const std::exception &){rejected=true;}assert(rejected);
 auto invalid=standalone;auto ribbon=invalid["ribbon"].toObject();ribbon["orientation"]=4;invalid["ribbon"]=ribbon;rejected=false;try{read_surface_path(invalid);}catch(const std::exception &){rejected=true;}assert(rejected);
 std::cout<<"Ribbon path v4/schema49 standalone and pooled image archive, shared immutable pixels, legacy v3 and corrupt image rejection passed\n";
}
