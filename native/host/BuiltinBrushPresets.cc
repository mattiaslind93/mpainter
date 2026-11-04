#include "BuiltinBrushPresets.hh"
#include <QImage>
#include <QResource>
static void initialize_brush_alphas(){Q_INIT_RESOURCE(builtin_brush_alphas);}
namespace bts {
std::vector<BrushPreset> builtin_brush_presets(){
 initialize_brush_alphas();return default_brush_presets([](const char *file){const auto path=QString(":/builtin-brush-alphas/alpha_%1.png").arg(file);QImage image(path);if(image.isNull())throw std::runtime_error("Bundled brush alpha cannot be decoded");auto bitmap=std::make_shared<AlphaBitmap>();bitmap->name=std::string(file);bitmap->width=image.width();bitmap->height=image.height();bitmap->pixels.resize(std::size_t(image.width())*image.height());for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto p=image.pixel(x,image.height()-1-y);bitmap->pixels[std::size_t(y)*image.width()+x]=float(qGray(p))/255.f*float(qAlpha(p))/255.f;}return bitmap;});
}
}
