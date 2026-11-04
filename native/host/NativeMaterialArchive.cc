#include <QStandardPaths>
#include "NativeMaterialArchive.hh"
#include "NativeMaterialGraph.hh"
#include "BlendMaterial.hh"
#include <QBuffer>
#include "NativeMaterialImageArchive.hh"
#include "NativeMaterialImageSource.hh"
#include "NativeMaterialImageSourceReference.hh"
#include "NativeMaterialDecodedImage.hh"
#include "NativeMaterialBundle.hh"
#include "TextureMemory.hh"
#include "BKE_blendfile.hh"
#include "BKE_image.hh"
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "BKE_report.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLO_writefile.hh"
#include "DNA_material_types.h"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include "DNA_scene_types.h"
#include "DNA_node_types.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <set>
#include <map>
namespace bts {
NativeMaterialArchive archive_native_material(NativeMaterialGraph &graph,const QString &source,const std::atomic_bool &cancel,bool external_images){
 using namespace blender;using blender::bke::blendfile::PartialWriteContext;
 if(auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())throw std::logic_error("Native material archive must be prepared on a worker");
 if(cancel.load())throw std::runtime_error("Native material archive cancelled");
 const auto path=source.toUtf8();if(!QFileInfo(source).isAbsolute()||path.size()>=int(sizeof(graph.main->filepath)))throw std::invalid_argument("Native material archive requires an absolute source library path");
 const auto cache=qEnvironmentVariable("BTS_RESOURCE_CACHE",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/resources");if(!QDir().mkpath(cache))throw std::runtime_error("Cannot create native material archive cache");QTemporaryDir temporary(cache+"/native-material-XXXXXX");if(!temporary.isValid())throw std::runtime_error("Cannot create native material archive directory");
 const std::string previous=graph.main->filepath;const auto source_root=QFileInfo(QString::fromStdString(previous)).isAbsolute()?QString::fromStdString(previous):source;const auto root_path=source_root.toUtf8();STRNCPY(graph.main->filepath,root_path.constData());const auto restore=qScopeGuard([&]{STRNCPY(graph.main->filepath,previous.c_str());});
 auto preview=blend_material_datablock_preview(*graph.material);if(preview.isNull()&&QFileInfo(source).isFile()&&QFileInfo(source).suffix().compare("blend",Qt::CaseInsensitive)==0)preview=blend_material_source_preview(source,graph.description.material);QByteArray preview_png;if(!preview.isNull()){QBuffer output(&preview_png);output.open(QIODevice::WriteOnly);if(!preview.save(&output,"PNG"))throw std::runtime_error("Cannot snapshot native source preview");}
 QElapsedTimer timer;timer.start();PartialWriteContext context(*graph.main);auto *material=context.id_add(&graph.material->id,{PartialWriteContext::ADD_DEPENDENCIES|PartialWriteContext::MAKE_LOCAL|PartialWriteContext::SET_FAKE_USER});if(!material||!context.is_valid())throw std::runtime_error("Cannot snapshot native material dependencies");
 ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto cleanup=qScopeGuard([&]{BKE_reports_free(&reports);});QStringList missing,missing_normal;QJsonArray images;
 // Ordinary channels remain hard dependencies. Optional world Normal keeps
 // missing source references and its own precise warning without losing PBR.
 std::set<const bNode *> visited;std::set<unsigned int> required,optional_normal;
 const auto visit=[&](auto &&self,const bNode &node)->void{if(!visited.insert(&node).second)return;if(STREQ(node.idname,"ShaderNodeTexImage")&&node.id&&GS(node.id->name)==ID_IM)required.insert(node.id->session_uid);for(const auto &socket:node.inputs)if(socket.link&&socket.link->fromnode)self(self,*socket.link->fromnode);};for(const auto &channel:graph.channels)if(channel.tree)for(const auto &node:channel.tree->nodes)if(STREQ(node.idname,"ShaderNodeOutputAOV"))visit(visit,node);
 if(graph.world_normal_tree){auto ordinary=std::move(required);required.clear();visited.clear();for(const auto &node:graph.world_normal_tree->nodes)if(STREQ(node.idname,"ShaderNodeOutputAOV"))visit(visit,node);optional_normal=std::move(required);required=std::move(ordinary);}
 const auto record=[&](const Image &image,QJsonObject row){const bool channel=required.contains(image.id.session_uid),normal=optional_normal.contains(image.id.session_uid);row["session_uid"]=qint64(image.id.session_uid);row["required_channel"]=channel;row["optional_world_normal"]=normal;row["dependency_role"]=channel?"required_channel":normal?"optional_world_normal":"unused";images.append(row);};
 std::map<unsigned int,const Image *> source_images;for(const auto &image:graph.main->images)source_images.emplace(image.id.session_uid,&image);
 const auto original=[&](const Image &image)->const Image &{const auto found=source_images.find(image.id.session_uid);if(found==source_images.end())throw std::logic_error("Archive Image has no matching source UID");return *found->second;};
 const auto retain_missing=[&](Image &image){retain_missing_native_image_reference(*graph.main,original(image),context.bmain,image);};
 const auto pack_error=[&](const Image &image){const auto &source_image=original(image);ImageUser user{};BKE_imageuser_default(&user);std::string reason="source image could not be packed";if(image.source==IMA_SRC_TILED){for(int number:native_material_tiled_image_numbers(image)){bool found=false;for(const auto &part:image.packedfiles)found|=part.tile_number==number&&part.packedfile&&part.packedfile->size>0;if(!found){user.tile=number;reason="tile "+std::to_string(number)+": "+reason;break;}}}char resolved[FILE_MAX]{};BKE_image_user_file_path_ex(graph.main.get(),&user,&source_image,resolved,true,true);return QString::fromStdString(native_material_image_source_error(source_image,resolved,reason));};
 std::vector<NativeMaterialImageReference> references;std::map<QString,StudioAsset> external_sources;
 for(auto &image:context.bmain.images){
  if(cancel.load())throw std::runtime_error("Native material archive cancelled");
  if(external_images){
   try{auto asset=snapshot_native_image_source(*graph.main,original(image),cancel);const auto hash=asset.hash();NativeMaterialImageReference reference{QString::fromUtf8(image.id.name+2),hash,asset.filename};if(image.source==IMA_SRC_TILED){reference.source=NativeMaterialImageSourceKind::Tiled;reference.tiles=asset.texture_files;for(auto &tile:reference.tiles)tile.path.clear();}reference.validate();references.push_back(std::move(reference));external_sources.try_emplace(hash,std::move(asset));BKE_image_free_packedfiles(&image);if(image.source!=IMA_SRC_TILED)image.source=IMA_SRC_FILE;const auto relative=("//native_images/"+hash+"/"+references.back().filename).toUtf8();if(relative.size()>=qsizetype(sizeof(image.filepath)))throw std::length_error((QString("Native image archive path needs %1 UTF-8 bytes; Image.filepath stores at most %2 bytes plus NUL").arg(relative.size()).arg(sizeof(image.filepath)-1)).toStdString());STRNCPY(image.filepath,relative.constData());record(image,QJsonObject{{"name",QString::fromUtf8(image.id.name+2)},{"packed",false},{"external",true},{"sha256",hash},{"required",required.contains(image.id.session_uid)}});}catch(const std::exception &error){if(cancel.load()||required.contains(image.id.session_uid))throw;retain_missing(image);if(optional_normal.contains(image.id.session_uid))missing_normal.append(QString::fromUtf8(error.what()));else missing.append(QString::fromUtf8(error.what()));record(image,QJsonObject{{"name",QString::fromUtf8(image.id.name+2)},{"packed",false},{"external",false},{"required",false}});}continue;
  }
  if(image.source==IMA_SRC_TILED&&!image.packedfiles.is_empty())try{const auto numbers=native_material_tiled_image_numbers(original(image));(void)native_material_encoded_image_source(*graph.main,original(image),numbers.front());}catch(const std::exception &error){if(cancel.load()||required.contains(image.id.session_uid))throw;retain_missing(image);if(optional_normal.contains(image.id.session_uid))missing_normal.append(QString::fromUtf8(error.what()));else missing.append(QString::fromUtf8(error.what()));record(image,QJsonObject{{"name",QString::fromUtf8(image.id.name+2)},{"packed",false},{"required",false}});continue;}
  if(image.packedfiles.is_empty()){if(image.source==IMA_SRC_FILE||image.source==IMA_SRC_TILED){
   // Stock packfiles resolves through G_MAIN before using its base argument.
   // Resolve this private snapshot explicitly; never rebind the live G_MAIN.
   const std::string literal=image.filepath;const auto restore_path=qScopeGuard([&]{STRNCPY(image.filepath,literal.c_str());});
   if(image.source==IMA_SRC_FILE&&image.type==IMA_TYPE_IMAGE&&!(image.flag&IMA_USE_VIEWS)){ImageUser user{};BKE_imageuser_default(&user);char absolute[FILE_MAX]{};BKE_image_user_file_path_ex(&context.bmain,&user,&image,absolute,true,true);STRNCPY(image.filepath,absolute);}
   if(image.source==IMA_SRC_TILED){const auto absolute=native_material_tiled_image_pattern(*graph.main,original(image)).toUtf8();STRNCPY(image.filepath,absolute.constData());}
   BKE_image_packfiles(&reports,&image,root_path.constData());
  }else if(required.contains(image.id.session_uid))throw std::runtime_error(std::string(image.id.name+2)+": generated source must be packed before native material archiving");}
  bool complete=true;for(const auto &tile:image.tiles){bool found=false;for(const auto &part:image.packedfiles)if(part.packedfile&&part.packedfile->size>0&&(image.source!=IMA_SRC_TILED||part.tile_number==tile.tile_number))found=true;complete&=found;}
  if(!complete){const auto reason=pack_error(image);if(required.contains(image.id.session_uid))throw std::runtime_error(reason.toStdString());if(image.source==IMA_SRC_TILED&&original(image).packedfiles.is_empty())BKE_image_free_packedfiles(&image);retain_missing(image);if(optional_normal.contains(image.id.session_uid))missing_normal.append(reason);else missing.append(reason);}
  record(image,QJsonObject{{"name",QString::fromUtf8(image.id.name+2)},{"packed",complete},{"tiles",BLI_listbase_count(&image.tiles)},{"required",required.contains(image.id.session_uid)}});
 }
 const auto output=temporary.filePath("material.blend");if(!context.write(output.toUtf8().constData(),G_FILE_COMPRESS,BLO_WRITE_PATH_REMAP_NONE,reports))throw std::runtime_error("Native material library archive write failed");if(cancel.load())throw std::runtime_error("Native material archive cancelled");
 QFile file(output);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read native material archive");NativeMaterialArchive result;result.bytes=file.readAll();if(result.bytes.size()!=file.size()||result.bytes.isEmpty())throw std::runtime_error("Native material archive read was incomplete");if(external_images||!preview_png.isEmpty()){result.bytes=native_material_bundle_bytes(result.bytes,references,{},preview_png);for(auto &[hash,asset]:external_sources)result.textures.push_back(std::move(asset));}result.description=graph.description;for(const auto &name:missing)result.description.warnings.append("Unused/unavailable native source image retained by reference: "+name);for(const auto &name:missing_normal)result.description.warnings.append("Bump shading image is unavailable; original reference kept: "+name);result.diagnostics={{"bytes",result.bytes.size()},{"external_images",external_images},{"image_resources",qint64(result.textures.size())},{"write_ms",timer.nsecsElapsed()/1e6},{"materials",BLI_listbase_count(&context.bmain.materials)},{"images",images},{"source_inputs_preserved",graph.original_inputs_preserved},{"missing_unused_images",QJsonArray::fromStringList(missing)},{"missing_optional_normal_images",QJsonArray::fromStringList(missing_normal)}};return result;
}
}
