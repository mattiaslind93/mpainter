#pragma once
namespace blender {struct Main;struct Scene;}
namespace bts {class TextureDocument;void pack_mesh_maps(blender::Main &,blender::Scene &,const TextureDocument &);void read_mesh_maps(const blender::Scene &,TextureDocument &);}
