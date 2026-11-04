#include "ResourceBudget.hh"
#include "TextureExtent.hh"
#include "BitmapMask.hh"
#include "ImageSequence.hh"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include "FileDialogs.hh"
#include <QFileInfo>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>
#include <memory>
#include <stdexcept>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
namespace bts {
std::shared_ptr<const AlphaBitmap> read_bitmap_alpha(const QString &path)
{
  using namespace blender;
  const QFileInfo file(path);
  if(!file.isAbsolute()||!file.isFile()||file.size()<=0)
    throw std::runtime_error("Choose a readable nonempty image file");
  require_texture_ram(resource_product(file.size(),2),"Bitmap source read");
  QFile source(path);if(!source.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read alpha source");const auto bytes=source.readAll();if(bytes.size()!=file.size())throw std::runtime_error("Incomplete alpha source read");return read_bitmap_alpha_bytes(bytes,file.completeBaseName());
}
std::shared_ptr<const AlphaBitmap> read_bitmap_alpha_bytes(const QByteArray &bytes,const QString &name) {
  if(is_image_sequence(bytes))return read_bitmap_alpha_bytes(read_image_sequence(bytes).front().bytes,name);
  using namespace blender;
  if(bytes.isEmpty())throw std::runtime_error("Image source is empty");
  using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
  const auto description=name.toUtf8();const auto *data=reinterpret_cast<const unsigned char *>(bytes.constData());
  Buffer header(IMB_load_image_from_memory(data,bytes.size(),ImBufFlags::Test|ImBufFlags::AlphaChannelPacked|ImBufFlags::NoColorspaceConvert,description.constData()),IMB_freeImBuf);
  if(!header||header->x<1||header->y<1)throw std::runtime_error("Cannot decode image dimensions");
  require_texture_ram(resource_product(resource_product(header->x,header->y),sizeof(float)*5),"Bitmap decode and scalar pixels");
  Buffer image(IMB_load_image_from_memory(data,bytes.size(),ImBufFlags::AlphaChannelPacked|ImBufFlags::NoColorspaceConvert,description.constData()),IMB_freeImBuf);
  if(!image||image->channels<1||image->channels>4||(!image->float_data()&&!image->byte_data()))throw std::runtime_error("Image has no readable pixel data");
  auto result=std::make_shared<AlphaBitmap>();result->width=image->x;result->height=image->y;result->name=name.toStdString();
  result->pixels.resize(std::size_t(image->x)*image->y);
  for(std::size_t i=0;i<result->pixels.size();++i) {
    const float value=image->float_data()?image->float_data()[i*image->channels]:image->byte_data()[i*4]/255.f;
    if(!std::isfinite(value))throw std::runtime_error("Image contains nonfinite scalar data");
    result->pixels[i]=std::clamp(value,0.f,1.f);
  }
  return result;
}
LayerMask load_bitmap_mask(const QString &path,int resolution,int tile,bool bilinear)
{
  if(!valid_sparse_texture_extent(resolution,128)||tile<1001||tile>2000)throw std::runtime_error("Invalid mask dimensions or UV tile");
  const auto image=read_bitmap_alpha(path);
  const auto texels=texture_texel_count(resolution,resolution);
  require_texture_ram(resource_sum(resource_product(texels,sizeof(float)*4),resource_product(sparse_texture_page_count(resolution),PaintPixels::page_side*PaintPixels::page_side*sizeof(PaintPixels::Pixel))),"Bitmap mask RGBA scratch and authored pages");
  std::vector<float> rgba(std::size_t(resource_product(texels,4)),0);
  for(int y=0;y<resolution;++y)for(int x=0;x<resolution;++x) {
    const auto i=(std::size_t(y)*resolution+x)*4;rgba[i]=image->sample((x+.5)/resolution,(y+.5)/resolution,bilinear);rgba[i+3]=1;
  }
  LayerMask mask;mask.background=0;mask.pixels.assign_channel(resolution,2,rgba,tile);return mask;
}
std::optional<LayerMask> choose_bitmap_mask(QWidget *parent,int resolution,const std::set<int> &tiles)
{
  QDialog dialog(parent);dialog.setObjectName("bitmapMaskDialog");dialog.setWindowTitle("Add bitmap mask");dialog.resize(540,240);
  auto *layout=new QVBoxLayout(&dialog);auto *form=new QFormLayout;layout->addLayout(form);
  auto *path=new QLineEdit(&dialog);path->setObjectName("bitmapMaskPath");form->addRow("Image",path);
  auto *browse=new QPushButton("Choose image…",&dialog);form->addRow(browse);
  QObject::connect(browse,&QPushButton::clicked,&dialog,[&]{const auto selected=bts::FileDialog::getOpenFileName(&dialog,"Mask image",{},"Images (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.hdr *.tga *.bmp);;All files (*)");if(!selected.isEmpty())path->setText(selected);});
  auto *tile=new QComboBox(&dialog);tile->setObjectName("bitmapMaskTile");for(int number:tiles)tile->addItem(QString::number(number),number);form->addRow("UV tile",tile);
  auto *filter=new QComboBox(&dialog);filter->setObjectName("bitmapMaskFilter");filter->addItems({"Bilinear","Nearest"});form->addRow("Resampling",filter);
  auto *description=new QLabel("Fits the image to the selected UV tile. Red is used as raw grayscale; alpha and color profiles are ignored. Other tiles stay black. Imported pixels remain paintable and are packed into the project.",&dialog);description->setWordWrap(true);layout->addWidget(description);
  auto *error=new QLabel(&dialog);error->setObjectName("bitmapMaskError");error->setWordWrap(true);layout->addWidget(error);
  auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->setObjectName("bitmapMaskButtons");layout->addWidget(buttons);
  std::optional<LayerMask> result;
  QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{try{result=load_bitmap_mask(path->text(),resolution,tile->currentData().toInt(),filter->currentIndex()==0);dialog.accept();}catch(const std::exception &e){error->setText(QString::fromUtf8(e.what()));}});
  if(dialog.exec()!=QDialog::Accepted)return std::nullopt;return result;
}
}
