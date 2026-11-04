#pragma once
namespace blender {struct Main;struct Scene;}
namespace bts {class TextureDocument;void pack_skew_fields(blender::Main &,blender::Scene &,const TextureDocument &);void read_skew_fields(const blender::Scene &,TextureDocument &,bool required);}
