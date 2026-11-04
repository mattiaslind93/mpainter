#include "NativeMaterialResource.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialBundle.hh"
#include "BKE_main.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "NativeMaterialArchive.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialFragmentNormal.hh"
#include "NativeMaterialImages.hh"
#include "NativeMaterialImageDependencies.hh"
#include "NativeMaterialSource.hh"
#include "NativeMaterialSourceFiles.hh"
#include "BlendMaterial.hh"
#include "TextureMemory.hh"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QThread>
#include <QFileInfo>
#include <algorithm>

namespace bts {
namespace {
void require_worker(const std::atomic_bool &cancel)
{
  if (auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())
    throw std::logic_error("Native material resource preparation requires a worker");
  if (cancel.load())throw std::runtime_error("Native material preparation cancelled");
}

}
PreparedNativeMaterialResource prepare_native_material_resource(
    const blender::Main &reference,const QString &library,const QString &material,const std::atomic_bool &cancel)
{
  require_worker(cancel);
  auto graph=prepare_native_material_graph(reference,library,material,cancel,NativeWorldNormalSurfaceContract::PainterUndisplaced);
  const auto color=std::find_if(graph->channels.begin(),graph->channels.end(),[](const auto &channel){return channel.channel==0;});if(color==graph->channels.end()||!color->tree)throw std::invalid_argument((QString("Native material Color unavailable: ")+(color==graph->channels.end()?QString("missing output"):color->error)).toStdString());
  prepare_optional_native_world_normal(graph,cancel);
  auto archive=archive_native_material(*graph,library,cancel,true);
  graph->resource_content_hash=native_material_source_hash(archive.bytes);
  graph->generate_resource_preview=native_material_bundle_preview(archive.bytes).isEmpty();
  const auto image_references=native_material_bundle_image_manifest(archive.bytes);auto images=prepare_native_material_images(*graph,cancel);
  if(!graph->tiled_image_resources.empty())validate_native_material_tiled_sources(*graph,image_references,cancel);
  prepare_native_world_normal_images(graph,cancel,image_references);
  auto description=std::move(archive.description);const QStringList names{"Color","Metallic","Roughness","Normal (internal)","Bump","Ambient occlusion"};for(const auto &channel:graph->channels)if(!channel.tree&&!(channel.channel==4&&graph->world_normal_tree&&graph->world_normal_error.isEmpty())){const auto name=channel.channel==14?QString("Displacement"):channel.channel<int(names.size())?names[channel.channel]:QString("Channel %1").arg(channel.channel);description.warnings.append(QString("%1 unavailable: %2").arg(name).arg(channel.error));}
  auto asset=native_material_asset(material,std::move(archive.bytes),std::move(description));for(const auto &channel:graph->channels)if(channel.tree)asset.search_terms.append("native-channel:"+QString::number(channel.channel));
  auto diagnostics=std::move(archive.diagnostics);diagnostics["decoded_images"]=images;
  require_worker(cancel);
  return {std::move(asset),std::move(graph),std::move(diagnostics),std::move(archive.textures)};
}
std::shared_ptr<NativeMaterialGraph> hydrate_native_material_resource(
    const blender::Main &,const StudioAsset &asset,const std::atomic_bool &cancel,const std::vector<StudioAsset> &textures){return hydrate_native_material_resource(asset,cancel,textures);}
std::shared_ptr<NativeMaterialGraph> hydrate_native_material_resource(
    const StudioAsset &asset,const std::atomic_bool &cancel,const std::vector<StudioAsset> &textures)
{
  require_worker(cancel);
  if(!is_native_material_asset(asset)||asset.bytes.isEmpty()||!asset.fill_preset||!asset.fill_preset->native_material)
    throw std::invalid_argument("Native material source archive/interface is missing");
  asset_metadata_read(asset_metadata(asset));
  validate_native_material_dependencies(asset,textures);
  const auto bundle=read_native_material_bundle(asset.bytes);auto scene=asset;scene.bytes=bundle.blend;
  auto detached=read_native_material_source(materialize_native_material_archive(scene,cancel),asset.fill_preset->native_material->material,cancel);
  bind_native_material_source_images(*detached.main,bundle,textures,cancel);
  auto graph=finish_native_material_source(std::move(detached),cancel,asset.fill_preset->native_material.get(),NativeWorldNormalSurfaceContract::PainterUndisplaced);
  auto original=native_material_description_json(*asset.fill_preset->native_material);
  auto restored=native_material_description_json(graph->description);
  for(const auto *field:{"warnings","channels","color_to_aces","color_from_aces"}){original.remove(field);restored.remove(field);}
  if(original!=restored){if(qEnvironmentVariableIsSet("BTS_NATIVE_PREPARATION_TRACE"))std::fprintf(stderr,"BTS_NATIVE_INTERFACE_MISMATCH material=%s original=%s restored=%s\n",qPrintable(asset.fill_preset->native_material->material),QJsonDocument(original).toJson(QJsonDocument::Compact).constData(),QJsonDocument(restored).toJson(QJsonDocument::Compact).constData());throw std::runtime_error("Native material source interface does not match its metadata");}
  prepare_native_material_images(*graph,cancel);
  validate_native_material_tiled_sources(*graph,bundle.images,cancel);
  graph->resource_content_hash=native_material_source_hash(asset.bytes);
  graph->generate_resource_preview=blend_material_preview(asset).isNull();
  prepare_optional_native_world_normal(graph,cancel);
  prepare_native_world_normal_images(graph,cancel,bundle.images);
  require_worker(cancel);
  return graph;
}
}
