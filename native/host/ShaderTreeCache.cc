#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QCryptographicHash>
#include "ShaderTreeCache.hh"
#include "BKE_blendfile.hh"
#include "BKE_lib_id.hh"
#include "BKE_lib_remap.hh"
#include "BKE_main.hh"
#include "BKE_global.hh"
#include "BKE_image.hh"
#include "DNA_image_types.h"
#include "BKE_report.hh"
#include "BLO_readfile.hh"
#include "BLO_writefile.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include <map>
#include <memory>
#include <vector>
#include <stdexcept>
namespace bts {
// Cache Blender's own lossless node representation. Small ID shells retain
// identity, users and external references; their variable-sized payload is cold.
// Caller must retire GPU programs/depsgraph consumers before eviction.
class ShaderTreeCache::Impl {
 struct Tree {blender::bNodeTree *live;blender::ID *owner;std::string name;};
 std::unique_ptr<QTemporaryFile> file_;std::vector<Tree> trees_;
 std::map<std::string,blender::ID *> ids_;std::vector<std::pair<blender::Image *,std::string>> images_;QByteArray hash_;
 static QByteArray hash_file(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("Shader cache unavailable");QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))throw std::runtime_error("Cannot read shader cache");return h.result();}
 public:
 bool cold()const{return bool(file_);}
 void spill(blender::Main &main,const std::vector<blender::bNodeTree *> &trees,const std::vector<blender::Image *> &images={}){
  using namespace blender;if(file_||trees.empty())return;
  const auto folder=qEnvironmentVariable("BTS_RECOVERY_DIR",QStandardPaths::writableLocation(QStandardPaths::CacheLocation))+"/unloaded-textures";
  if(!QDir().mkpath(folder))throw std::runtime_error("Cannot create shader cache");
  auto file=std::make_unique<QTemporaryFile>(folder+"/shaders-XXXXXX.blend");if(!file->open())throw std::runtime_error("Cannot create shader cache");file->close();
  bke::blendfile::PartialWriteContext context(main);std::vector<Tree> records;
  for(auto *tree:trees){auto *owner=tree->owner_id;auto *root=owner?owner:&tree->id;context.id_add(root,{bke::blendfile::PartialWriteContext::ADD_DEPENDENCIES|bke::blendfile::PartialWriteContext::MAKE_LOCAL|bke::blendfile::PartialWriteContext::SET_FAKE_USER});records.push_back({tree,owner,root->name});}
  for(auto *image:images)context.id_add(&image->id,{bke::blendfile::PartialWriteContext::ADD_DEPENDENCIES|bke::blendfile::PartialWriteContext::MAKE_LOCAL|bke::blendfile::PartialWriteContext::SET_FAKE_USER});
  if(!context.is_valid())throw std::runtime_error("Cannot snapshot shader dependencies");
  std::map<unsigned,ID *> live;ID *id;FOREACH_MAIN_ID_BEGIN(&main,id){live[id->session_uid]=id;}FOREACH_MAIN_ID_END;
  std::map<std::string,ID *> ids;FOREACH_MAIN_ID_BEGIN(&context.bmain,id){if(auto it=live.find(id->session_uid);it!=live.end())ids[id->name]=it->second;}FOREACH_MAIN_ID_END;
  for(auto &record:records){for(const auto &[name,original]:ids)if(original==(record.owner?record.owner:&record.live->id)){record.name=name;break;}}
  ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const bool ok=context.write(file->fileName().toUtf8().constData(),G_FILE_COMPRESS,BLO_WRITE_PATH_REMAP_NONE,reports);BKE_reports_free(&reports);if(!ok)throw std::runtime_error("Cannot write shader cache; original retained");
  auto digest=hash_file(file->fileName());
  std::vector<std::pair<Image *,std::string>> image_records;for(auto *image:images)for(const auto &[name,original]:ids)if(original==&image->id){image_records.push_back({image,name});break;}
  // Allocate every empty shell before changing a single live tree.
  std::vector<std::unique_ptr<ID,void(*)(ID *)>> empty;
  for(const auto &record:records)empty.emplace_back(static_cast<ID *>(BKE_id_new_nomain(ID_NT,record.live->id.name+2)),[](ID *p){BKE_id_free_ex(nullptr,p,LIB_ID_FREE_NO_MAIN|LIB_ID_FREE_NO_USER_REFCOUNT,false);});
  for(std::size_t i=0;i<records.size();++i){auto &r=records[i];BKE_lib_id_swap(nullptr,&r.live->id,empty[i].get(),false,0);r.live->owner_id=r.owner;r.live->type=NTREE_SHADER;}
  for(const auto &[image,name]:image_records)BKE_image_free_packedfiles(image);
  images_=std::move(image_records);
  trees_=std::move(records);ids_=std::move(ids);hash_=digest;file_=std::move(file);
 }
 void restore(){
  using namespace blender;if(!file_)return;
  if(hash_file(file_->fileName())!=hash_)throw std::runtime_error("Shader cache checksum mismatch");
  BlendFileReadReport report{};std::unique_ptr<BlendFileData,decltype(&BLO_blendfiledata_free)> read(BLO_read_from_file(file_->fileName().toUtf8().constData(),BLO_READ_SKIP_USERDEF,&report),BLO_blendfiledata_free);
  if(!read||!read->main)throw std::runtime_error("Cannot restore shader cache");
  std::map<std::string,ID *> loaded;ID *id;FOREACH_MAIN_ID_BEGIN(read->main,id){loaded[id->name]=id;}FOREACH_MAIN_ID_END;
  std::vector<bNodeTree *> restored;for(const auto &r:trees_){const auto found=loaded.find(r.name);if(found==loaded.end())throw std::runtime_error("Shader cache lost an ID");auto *tree=r.owner?reinterpret_cast<Material *>(found->second)->nodetree:reinterpret_cast<bNodeTree *>(found->second);if(!tree)throw std::runtime_error("Shader cache lost a tree");restored.push_back(tree);}
  for(const auto &[image,name]:images_)if(!loaded.contains(name))throw std::runtime_error("Shader cache lost packed image data");
  bke::id::IDRemapper remap;for(const auto &[name,original]:ids_)if(loaded.contains(name))remap.add(loaded.at(name),original);
  BKE_libblock_remap_multiple_raw(read->main,remap,ID_REMAP_SKIP_USER_REFCOUNT);
  for(std::size_t i=0;i<trees_.size();++i){auto &r=trees_[i];BKE_lib_id_swap(nullptr,&r.live->id,&restored[i]->id,false,0);r.live->owner_id=r.owner;}
  for(const auto &[image,name]:images_)std::swap(image->packedfiles,reinterpret_cast<Image *>(loaded.at(name))->packedfiles);
  file_.reset();trees_.clear();ids_.clear();images_.clear();
 }
};
ShaderTreeCache::ShaderTreeCache():impl_(std::make_unique<Impl>()){}
ShaderTreeCache::~ShaderTreeCache()=default;
ShaderTreeCache::ShaderTreeCache(ShaderTreeCache &&)noexcept=default;
ShaderTreeCache &ShaderTreeCache::operator=(ShaderTreeCache &&)noexcept=default;
bool ShaderTreeCache::cold()const{return impl_->cold();}
void ShaderTreeCache::spill(blender::Main &main,const std::vector<blender::bNodeTree *> &trees,const std::vector<blender::Image *> &images){impl_->spill(main,trees,images);}
void ShaderTreeCache::restore(){impl_->restore();}

}
