#include "BuiltinSmartMasks.hh"
#include "BuiltinMaskNames.hh"
#include "FillTexturePool.hh"
#include <QFile>
#include <QImage>
#include <QResource>
#include <array>
#include <atomic>
#include <stdexcept>
#include <string_view>

// Keep this outside namespace bts: Qt's generated initializer is global.
static void initialize_builtin_grunges(){Q_INIT_RESOURCE(builtin_grunges);}
namespace bts {namespace {
std::atomic_uint64_t rcc_decodes=0;
struct Recipe {const char *name,*image;int generator;float contrast,bias,opacity,scale,gamma;};
constexpr std::array recipes{
 Recipe{"Rust","rust_spots",9,1.8f,-.05f,.72f,.28f,.85f},
 Recipe{"Edge Wear","edge_wear",8,2.f,.20f,.65f,.34f,.9f},
 Recipe{"Crease Dirt","crease_dirt",9,2.f,.3f,.8f,.32f,.82f},
 Recipe{"Moss","moss_organic",9,1.4f,-.12f,.78f,.42f,1.15f},
 Recipe{"Ground Dirt","ground_mud",1,2.f,-.1f,.8f,.38f,.95f},
 Recipe{"Scratches","scratches",8,2.f,.15f,.9f,.2f,.85f},
 Recipe{"Leaks & Stains","leaks_streaks",9,2.8f,-.05f,.92f,.42f,.88f},
 Recipe{"Surface Dust","fine_dust",6,1.2f,.02f,.55f,.24f,1.15f}
};
QUuid builtin_id(const QString &name){return QUuid::createUuidV5(QUuid("{954c6511-3b9d-4f1f-bbdd-173dfbb99b69}"),name.toUtf8());}
MaskEffect generator(int kind,const char *name,LayerBlend blend=LayerBlend::Normal,float opacity=1){
 MaskEffect e;e.kind=MaskEffectKind::Generator;e.generator=kind;e.name=name;e.blending=blend;e.opacity=opacity;
 e.response=1;e.optional_inputs=true;e.map_filter=1;e.ao_influence=.8f;e.curvature_influence=.8f;
 e.triplanar_grunge=true;e.surface_fallback=true;
 e.position_influence=.15f;e.normal_influence=.15f;e.thickness_influence=.15f;
 return e;
}
std::vector<StudioAsset> make_assets(){
 initialize_builtin_grunges();std::vector<StudioAsset> result;result.reserve(10);
 std::size_t index=0;
 for(const auto &recipe:recipes){
  const auto name=std::string_view(recipe.name);
  const auto filename=QString("grunge_%1.png").arg(recipe.image);
  QFile source(":/builtin-grunges/"+filename);if(!source.open(QIODevice::ReadOnly))throw std::runtime_error("Bundled grunge input is missing");
  StudioAsset image;image.id=builtin_id(filename);image.filename=filename;
  auto label=QString::fromLatin1(recipe.image);label.replace('_',' ');for(int i=0;i<label.size();++i)if(i==0||label[i-1]==' ')label[i]=label[i].toUpper();
  image.name="Grunge "+label;image.usages={"texture","procedural","grunge"};image.scope=AssetScope::Builtin;
  image.search_terms={"builtin:grunge","user-supplied","tileable","non-color"};image.bytes=source.readAll();
  const auto decoded=QImage::fromData(image.bytes,"PNG");++rcc_decodes;
  if(decoded.isNull()||decoded.size()!=QSize(512,512)||!decoded.isGrayscale())throw std::runtime_error("Invalid bundled grayscale grunge");
  auto texture=std::make_shared<FillTexture>();texture->asset=image.id.toString(QUuid::WithoutBraces).toStdString();
  texture->hash=image.hash().toStdString();texture->name=image.name.toStdString();texture->raw=true;
  texture->input_space="Non-Color/raw";texture->width=decoded.width();texture->height=decoded.height();
  auto pixels=std::make_shared<std::vector<FillTexture::Pixel>>(std::size_t(texture->width)*texture->height);
  for(int y=0;y<texture->height;++y)for(int x=0;x<texture->width;++x){
   const float v=qRed(decoded.pixel(x,texture->height-1-y))/255.f;(*pixels)[std::size_t(y)*texture->width+x]={v,v,v,1};
  }
  texture->pixels=intern_fill_texture_pixels(*texture,std::move(pixels));texture->validate();
  LayerMask mask;mask.background=0;
  auto base=generator(recipe.generator,"Mesh Maps");base.contrast=recipe.contrast;base.bias=recipe.bias;
  if(recipe.generator==6){base.name="Upward World Normal";base.light_direction={0,.35f,.93675f};base.curve=1;}
  if(recipe.generator==1){base.name="Bottom Position Falloff";base.axis=2;base.invert=true;base.curve=1;}
  // Keep exposed, unbaked surfaces useful without replacing the mesh-map response.
  if(name=="Leaks & Stains"){base.response=2;base.axis=1;base.grunge_pattern=3;base.grunge_scale=2;base.grunge_amount=.3f;base.ao_influence=.6f;base.curvature_influence=1;}
  if(name=="Crease Dirt"){base.thickness_influence=.65f;base.curvature_influence=1;}
  mask.effects.push_back(base);
  if(name=="Rust"||name=="Edge Wear"||name=="Leaks & Stains"){
   auto height=generator(1,"Position Height Gradient",LayerBlend::Multiply,.25f);height.axis=name=="Leaks & Stains"?1:2;height.invert=name!="Edge Wear"&&name!="Leaks & Stains";
   height.curve=1;mask.effects.push_back(height);
  }
  if(recipe.generator==6||recipe.generator==1){
   auto cavities=generator(3,"Ambient Occlusion Cavities",LayerBlend::LinearDodge,.25f);cavities.invert=true;
   mask.effects.push_back(cavities);
  }
  if(name=="Ground Dirt"){
   auto upward=generator(6,"World Normal Exposure",LayerBlend::Multiply,.25f);upward.light_direction={0,0,1};
   mask.effects.push_back(upward);
  }
  MaskEffect fill;fill.kind=MaskEffectKind::Fill;fill.name="Tri-planar "+image.name.toStdString();
  fill.blending=LayerBlend::Multiply;
  fill.opacity=recipe.opacity;fill.mapping.projection.mode=1;fill.mapping.projection.texture=texture;
  fill.mapping.projection.scale3={recipe.scale,recipe.scale,recipe.scale};fill.mapping.projection.hardness=.35;
  fill.mapping.projection.alpha_mask[0]=false;mask.effects.push_back(fill);
  MaskEffect levels;levels.kind=MaskEffectKind::Levels;levels.name="Balance & Contrast";
  levels.levels.input_low=.06f;levels.levels.input_high=.94f;levels.levels.gamma=recipe.gamma;levels.levels.clamp=true;
  mask.effects.push_back(levels);
  StudioAsset preset;preset.name=recipe.name;preset.filename=QString::fromUtf8(builtin_mask_names[index].slug.data(),builtin_mask_names[index].slug.size())+".btsmask";
  preset.id=builtin_id(preset.filename);preset.scope=AssetScope::Builtin;preset.usages={"smartmask"};
  preset.search_terms={"builtin:native-smart-mask","mesh-maps","triplanar","editable"};
  preset.smart_mask=std::make_shared<const LayerMask>(std::move(mask));preset.bytes=write_mask_preset(*preset.smart_mask);
  hydrate_asset_presets(preset); // Validate/cache once before publication; never decode per apply.
  result.push_back(std::move(preset));++index;
 }
 // The two simple built-ins live in AssetCatalog in the reference. Keep all
 // ten masks together here, with the already shipped mPainter identities.
 for(int kind:{1,2}){
  LayerMask mask;mask.background=0;MaskEffect effect;effect.kind=MaskEffectKind::Generator;effect.generator=kind;
  if(kind==2){effect.contrast=4;effect.bias=-.5f;}
  mask.effects.push_back(effect);
  const auto &entry=builtin_mask_names[index++];StudioAsset preset;
  preset.name=QString::fromUtf8(entry.name.data(),entry.name.size());
  preset.filename=QString::fromUtf8(entry.slug.data(),entry.slug.size())+".btsmask";
  preset.id=builtin_id(preset.filename);preset.scope=AssetScope::Builtin;preset.usages={"smartmask"};
  preset.search_terms={"builtin:native-smart-mask","mesh-maps","editable"};
  preset.smart_mask=std::make_shared<const LayerMask>(std::move(mask));preset.bytes=write_mask_preset(*preset.smart_mask);
  hydrate_asset_presets(preset);result.push_back(std::move(preset));
 }
 return result;
}
} // namespace
std::uint64_t builtin_smart_mask_rcc_decodes(){return rcc_decodes.load(std::memory_order_relaxed);}
const std::vector<StudioAsset> &builtin_smart_mask_assets(){static const auto assets=make_assets();return assets;}
} // namespace bts
