#include "NativeMaterialPreviewImages.hh"
#include "NativeMaterialDecodedImage.hh"
#include "NativeMaterialGraph.hh"
#include "ResourceBudget.hh"
#include "BKE_image.hh"
#include "BKE_image_hosted.hh"
#include "BKE_main.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "DNA_node_types.h"
#include "DNA_packedFile_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include "MEM_guardedalloc.h"
#include <QCoreApplication>
#include <QThread>
#include <QJsonObject>
#include <algorithm>
#include <memory>
#include <set>
namespace bts {
QJsonArray prepare_native_material_preview_images(NativeMaterialGraph &graph,const std::atomic_bool &cancel)
{
 using namespace blender;
 if(auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())throw std::logic_error("Preview images must be decoded on a worker");
 std::set<Image *> required;std::set<const bNode *> visited;
 const auto visit=[&](auto &&self,const bNode &node)->void{
  if(!visited.insert(&node).second)return;
  if(STREQ(node.idname,"ShaderNodeTexImage")&&node.id&&GS(node.id->name)==ID_IM)required.insert(reinterpret_cast<Image *>(node.id));
  for(const auto &socket:node.inputs)if(socket.link&&socket.link->fromnode)self(self,*socket.link->fromnode);
 };
 for(const auto &channel:graph.channels)if(channel.channel==0&&channel.tree)for(const auto &node:channel.tree->nodes)if(STREQ(node.idname,"ShaderNodeOutputAOV"))visit(visit,node);
 QJsonArray report;
 for(auto *image:required){
  auto decoded=decode_native_material_image(*graph.main,*image,cancel);
  if(!blender::BKE_image_assign_hosted_file_buffer(image,decoded.buffer.get()))throw std::runtime_error("Preview cannot replace a non-private image cache");
  report.append(std::move(decoded.metadata));
 }
 return report;
}
}
