#pragma once
namespace blender {struct Main;struct Scene;}
namespace bts {class TextureDocument;void pack_stroke_history(blender::Main &,blender::Scene &,const TextureDocument &);void read_stroke_history(const blender::Scene &,TextureDocument &,bool required);}
