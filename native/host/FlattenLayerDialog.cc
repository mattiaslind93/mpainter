#include "FlattenLayerDialog.hh"
#include "FillTextureIO.hh"
#include "ImageSequence.hh"
#include "TexturePadding.hh"
#include "EditTiming.hh"
#include "TextureMemory.hh"
#include "FlattenMaskRaster.hh"
#include <mutex>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <thread>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "DNA_scene_types.h"
#include "IMB_colormanagement.hh"
namespace bts {namespace {
class Wait final:public QDialog {
 public:std::atomic_bool cancelled=false;using QDialog::QDialog;
 void reject()override{cancelled=true;}
};
QByteArray encode(const FlattenImage &source){
 using namespace blender;using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
 Buffer image(IMB_allocImBuf(source.width,source.height,ImBufFlags::FloatData),IMB_freeImBuf);if(!image)throw std::bad_alloc();
 image->color_mode=ImColorMode::RGBA;image->ftype=IMB_FTYPE_OPENEXR;image->foptions.flag=R_IMF_EXR_CODEC_ZIP;image->foptions.compress=15;
 IMB_colormanagement_assign_float_colorspace(image.get(),"Non-Color");
 std::copy_n(reinterpret_cast<const float *>(source.pixels->data()),std::size_t(source.width)*source.height*4,image->float_data_for_write());
 const auto bytes=IMB_save_image_to_buffer(image.get(),ImBufFlags::FloatData|ImBufFlags::AlphaChannelPacked);
 if(bytes.is_empty())throw std::runtime_error("Cannot encode flattened bitmap");return QByteArray(reinterpret_cast<const char *>(bytes.data()),bytes.size());
}
std::vector<StudioAsset> resources(FlattenedLayer &bake,const std::atomic_bool &cancel){
 std::vector<StudioAsset> result;std::uint64_t bytes=0;
 for(int c=0;c<15;++c)if(!bake.images[c].empty()){
  if(cancel)return {};StudioAsset asset;asset.scope=AssetScope::Project;asset.name=QString::fromStdString(bake.layer.name)+QString(" channel %1").arg(c);asset.usages={"texture"};asset.search_terms={"flattened"};if(c==3)asset.search_terms.append("internal:flattened-normal");
  std::vector<SequenceMember> members;for(const auto &[tile,image]:bake.images[c]){if(cancel)return {};const auto data=encode(image);bytes+=data.size();if(std::uint64_t(data.size())>texture_available_ram_bytes()/2)throw std::length_error("Insufficient available RAM to package flattened texture");members.push_back({tile,QString("flattened_%1.%2.exr").arg(c).arg(tile),data});}
  asset.filename=QString("flattened_%1.btsseq").arg(c);asset.bytes=make_image_sequence(members);
  fill_channel_texture(bake.layer.fill,c)=decode_fill_texture(asset,c!=0,c==0?"ACEScg":"");result.push_back(std::move(asset));
 }
 return result;
}
}
std::optional<PreparedFlatten> choose_layer_flatten(QWidget *parent,TextureSet source,const QUuid &selected,LayerCaptureBridge bridge){
 Wait dialog(parent);dialog.setObjectName("flattenProgressDialog");dialog.setWindowTitle("Flatten layers");auto *layout=new QVBoxLayout(&dialog);auto *label=new QLabel("Baking channel bitmaps on the GPU…",&dialog);layout->addWidget(label);auto *progress=new QProgressBar(&dialog);progress->setRange(0,0);layout->addWidget(progress);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&Wait::reject);
 std::optional<PreparedFlatten> result;std::string error;std::atomic_bool done=false;
 struct Mailbox {std::mutex mutex;std::optional<LayerCaptureRequest> request;std::optional<LayerCaptureResult> result;std::uint64_t ticket=0;};Mailbox mailbox;
 std::jthread worker([&]{try{EditTiming timing("BTS_FLATTEN_MS");auto plan=prepare_flatten_layer(source,selected);if(plan.snapshot_bytes>flatten_safe_ram_bytes())throw std::length_error("Flatten capture requires "+std::to_string((plan.snapshot_bytes+1048575)/1048576)+" MiB; insufficient available RAM for channel buffers");auto &bake=plan.result;
  const auto capture=[&](LayerCaptureRequest request)->std::shared_ptr<std::vector<FillTexture::Pixel>>{
   {std::lock_guard lock(mailbox.mutex);mailbox.result.reset();mailbox.ticket=0;mailbox.request=request;}
   while(!dialog.cancelled){
    {std::lock_guard lock(mailbox.mutex);if(mailbox.result){if(!mailbox.result->valid)throw std::runtime_error("GPU layer bake failed; original layer retained");return mailbox.result->pixels;}}
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
   }return {};
  };
  bool nonempty=false,mask_background_chosen=false;std::uint64_t transferred=0;int chunks=0;
  for(int tile:source.tiles){if(dialog.cancelled)break;const auto &original=TextureDocument::layer(source,selected);if(original.geometry.type==1&&!original.geometry.includes_tile(tile))continue;
   for(int c=0;c<16;++c){const bool mask=c==15;if(mask?!bake.layer.mask:!plan.enabled[c])continue;if(dialog.cancelled)break;
    const int width=mask?plan.mask_canvas:source.width(tile),height=mask?plan.mask_canvas:source.rows(tile);const auto bytes=std::uint64_t(width)*height*16;
    const auto budget=flatten_safe_ram_bytes();if(bytes>budget)throw std::length_error("Layer bake needs "+std::to_string((bytes+1048575)/1048576)+" MiB, available safe RAM budget "+std::to_string(budget/1048576)+" MiB");
    auto pixels=std::make_shared<std::vector<FillTexture::Pixel>>(std::size_t(width)*height);
    for(int y=0;y<height&&!dialog.cancelled;y+=256)for(int x=0;x<width&&!dialog.cancelled;x+=256){
     LayerCaptureRequest request{selected,mask?0:c,tile,x,y,std::min(256,width-x),std::min(256,height-y),width,height,mask,!mask&&plan.contextual[c]};auto packet=capture(request);if(!packet)break;
     if(packet->size()!=std::size_t(request.width)*request.height)throw std::runtime_error("Incomplete GPU layer bake packet");
     for(int row=0;row<request.height;++row)std::copy_n(packet->data()+row*request.width,request.width,pixels->data()+std::size_t(y+row)*width+x);transferred+=packet->size()*16;++chunks;
    }
    if(dialog.cancelled)break;
    if(mask){assign_flattened_mask(*bake.layer.mask,width,tile,std::span<float>(reinterpret_cast<float *>(pixels->data()),pixels->size()*4),mask_background_chosen);}
    else{const auto &flat=bake.layer;const bool on=c<5?flat.channels[c]&&flat.blending[c]!=LayerBlend::Disabled:c==5?flat.ao_enabled&&flat.ao_blending!=LayerBlend::Disabled:c==14?flat.displacement.enabled&&flat.displacement.blending!=LayerBlend::Disabled:flat.users[c-6].enabled&&flat.users[c-6].blending!=LayerBlend::Disabled;nonempty|=on&&std::any_of(pixels->begin(),pixels->end(),[](const auto &p){return p[3]>0;});bake.images[c][tile]={width,height,std::move(pixels)};}
   }
  }
  if(!dialog.cancelled){if(!nonempty)throw std::invalid_argument("No visible texture data in the selected layer/group");PreparedFlatten ready;ready.bake=std::move(bake);ready.assets=resources(ready.bake,dialog.cancelled);if(!dialog.cancelled)result=std::move(ready);std::printf("BTS_GPU_FLATTEN chunks=%d readback_bytes=%llu cpu_composites=0\n",chunks,(unsigned long long)transferred);}
 }catch(const std::exception &e){error=e.what();}done.store(true,std::memory_order_release);});
 QTimer poll;poll.setInterval(5);QObject::connect(&poll,&QTimer::timeout,&dialog,[&]{
  if(dialog.cancelled)label->setText("Cancelling…");
  if(!dialog.cancelled){std::lock_guard lock(mailbox.mutex);try{
   if(mailbox.request&&!mailbox.ticket){mailbox.ticket=bridge.request(source,*mailbox.request);label->setText(QString("Baking tile %1, %2 on GPU…").arg(mailbox.request->tile).arg(mailbox.request->mask?QString("mask"):QString("channel %1").arg(mailbox.request->channel)));}
   if(mailbox.ticket&&!mailbox.result){auto ready=bridge.result();if(ready&&ready->ticket==mailbox.ticket){mailbox.result=std::move(ready);mailbox.request.reset();}}
  }catch(...){mailbox.result=LayerCaptureResult{mailbox.ticket,false,{}};mailbox.request.reset();}}
  if(done.load(std::memory_order_acquire))dialog.done(dialog.cancelled?QDialog::Rejected:QDialog::Accepted);
 });poll.start();dialog.exec();worker.join();if(!error.empty())throw std::runtime_error(error);return result;
}
}
