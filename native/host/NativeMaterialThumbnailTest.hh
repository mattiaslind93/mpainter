#include "Theme.hh"
#pragma once
#include "AssetThumbnail.hh"
#include "BlendLibraryIndex.hh"
#include "MaterialSpherePreview.hh"
#include "NativeMaterialSourceLoader.hh"
#include "NativeMaterialBundle.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialApply.hh"
#include "BlendMaterialBindings.hh"
#include <epoxy/gl.h>
#include <cstdio>
#include <QPainter>
#include <QElapsedTimer>
namespace bts {
inline QJsonObject verify_native_material_thumbnails(BlenderHost &host,const std::function<void()> &frame,const std::function<QImage()> &capture){
 const auto output=qEnvironmentVariable("BTS_NATIVE_THUMBNAIL_OUTPUT",qEnvironmentVariable("BTS_NATIVE_MATERIAL_GPU_TEST")),source=qEnvironmentVariable("BTS_NATIVE_THUMBNAIL_SOURCE"),project=output+"/thumbnail-project.blend";
 const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
 if(!qEnvironmentVariableIsEmpty("BTS_MATERIAL_IMPORT_CASES")){
  QFile manifest(qEnvironmentVariable("BTS_MATERIAL_IMPORT_CASES"));require(manifest.open(QIODevice::ReadOnly),"Cannot read material import cases");
  const auto cases=QJsonDocument::fromJson(manifest.readAll()).array();require(cases.size()>=4,"Normal import test requires four materials");
  NativeMaterialSourceLoader loader(host,nullptr,[]{},[](const QString &){});QJsonArray proof;
  for(const auto &entry:cases){
   const auto test=entry.toObject();const auto name=test["material"].toString();
   std::printf("MPAINTER_MATERIAL_CASE %s\n",qPrintable(name));std::fflush(stdout);
   auto prepared=host.import_blend_material(test["file"].toString(),name,std::make_shared<std::atomic_bool>(false)).get();
   const auto image_resources=prepared.textures;
   StudioAsset result=prepared.material;const bool native=bool(prepared.native_graph);QString failure;
   QJsonObject details{{"material",name},{"native",native},{"normal_texture",bool(prepared.material.fill_preset&&fill_channel_texture(*prepared.material.fill_preset,3))}};
   if(native){
    details["world_normal_tree"]=bool(prepared.native_graph->world_normal_tree);details["world_normal_error"]=prepared.native_graph->world_normal_error;
    details["warnings"]=QJsonArray::fromStringList(prepared.native_graph->description.warnings);
    loader.prepared(std::move(prepared),[&](const StudioAsset &asset){result=asset;},[&](const QString &error){failure=error;});
    QElapsedTimer clock;clock.start();while(loader.pending()){frame();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(clock.elapsed()<120000,"Normal import GPU preparation timed out");}require(failure.isEmpty(),qPrintable(failure));
   }
   const auto image=blend_material_preview(result);require(!image.isNull(),"Imported material has no shaderball preview");
   const auto center=image.pixelColor(image.width()/2,image.height()/2);require(std::max({center.red(),center.green(),center.blue()})>30,"Imported material shaderball is black");
   require(image.save(output+"/"+name+"-mpainter.png"),"Cannot save imported shaderball");
   details["preview_center"]=QJsonArray{center.red(),center.green(),center.blue()};
   if(!qEnvironmentVariableIsEmpty("BTS_MATERIAL_IMPORT_MESH")){
    NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;
    const auto imported=host.new_project(qEnvironmentVariable("BTS_MATERIAL_IMPORT_MESH").toStdString(),qEnvironmentVariable("BTS_ENVIRONMENT").toStdString(),&options);
    const auto snapshot=host.scene_snapshot();TextureDocument document(options,imported,snapshot);auto &set=document.set(snapshot.materials.front().session_id);
    if(native){
     loader.ensure(result,image_resources,[](const StudioAsset &){},[&](const QString &error){failure=error;});
     QElapsedTimer load;load.start();while(loader.pending()){frame();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(load.elapsed()<120000,"Normal material hydration timed out");}
     require(failure.isEmpty(),qPrintable(failure));apply_native_material_asset(set,result);
    }else{TextureDocument::add_fill(set);apply_blend_material_channels(set,TextureDocument::layer(set,set.selected_layer),*result.material,result.fill_preset?&*result.fill_preset:nullptr,true);}
    host.set_selected_material(set.material_session_uid);host.apply_texture_set(set,128);
    const auto settle=[&]{QElapsedTimer render;render.start();int frames=0;bool done=false;do{frame();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(render.elapsed()<180000,"Normal material rendering timed out");++frames;const auto state=host.eevee_completion_snapshot();done=state.status==EeveeCompletionStatus::Captured&&state.needed_shaders_loaded&&!state.queued_shaders&&!state.queued_textures&&!state.reset_pending&&(state.sampling_finished||state.viewport_sampling_finished);}while(host.texture_updates_pending()||frames<24||(!done&&!qEnvironmentVariableIsSet("BTS_MATERIAL_IMPORT_RAPID_SWITCH_TEST")));};
    settle();const auto completion=host.eevee_completion_snapshot();details["render_completion"]=QJsonObject{{"samples",qint64(completion.sample_index)},{"sample_count",qint64(completion.sample_count)},{"ready",completion.needed_shaders_loaded},{"sampling_finished",completion.sampling_finished},{"viewport_sampling_finished",completion.viewport_sampling_finished},{"queued_shaders",qint64(completion.queued_shaders)},{"queued_textures",qint64(completion.queued_textures)}};
    details["world_normal"]=host.native_world_normal_status(set.material_session_uid);details["shader"]=host.shader_parameter_state();
    if(name=="image-normal"||name=="procedural-bump"||name=="bump-normal-chain"){
     if(name=="image-normal")require(details["normal_texture"].toBool(),"Image normal was not imported into channel 3");
     std::vector<std::array<int,2>> coordinates;for(int y=16;y<112;y+=12)for(int x=16;x<112;x+=12)coordinates.push_back({x,y});
     const auto pixels=host.diagnostic_gpu_pixels(set.material_session_uid,3,coordinates);std::array<float,3> low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f};
     for(const auto &pixel:pixels)for(int c=0;c<3;++c){low[c]=std::min(low[c],pixel[c]);high[c]=std::max(high[c],pixel[c]);}
     const auto variation=std::max({high[0]-low[0],high[1]-low[1],high[2]-low[2]});details["normal_channel_range"]=variation;
     require(variation>1e-4,"Imported normal channel is constant");
    }
    if(name=="unsupported-normal"||name=="unsupported-color"||name=="unsupported-closure-color")require(!details["warnings"].toArray().isEmpty(),"Unsupported Normal did not preserve a warning");
    const auto coat_weight=[&]{for(const auto &entry:host.shader_parameter_state()){const auto sockets=entry.toObject()["openpbr"].toObject()["sockets"].toObject();if(sockets.contains("Coat Weight"))return sockets["Coat Weight"].toArray().first().toDouble();}return -1.;};
    if(name=="coat-normal"||name=="constant-coat"){
     require(coat_weight()>.99,"Imported coat lobe was lost");
     auto resources=image_resources;resources.push_back(result);for(auto &asset:resources)asset.scope=AssetScope::Project;
     const auto project_file=output+"/"+name+"-project.blend";host.begin_save_project(document,project_file.toStdString(),resources);
     QElapsedTimer saving;saving.start();while(host.save_project_pending()){frame();host.poll_save_project();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(saving.elapsed()<120000,"Coat project save timed out");}
     auto reopened=host.open_project(project_file.toStdString());const auto restored=host.project_resources();
     for(const auto &asset:restored)if(asset.fill_preset&&asset.fill_preset->native_material)loader.ensure(asset,restored,[](const StudioAsset &){},[&](const QString &error){failure=error;});
     QElapsedTimer opening;opening.start();while(loader.pending()){frame();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(opening.elapsed()<120000,"Coat project restore timed out");}require(failure.isEmpty(),qPrintable(failure));
     auto &restored_set=reopened.set(reopened.texture_sets().front().material_session_uid);host.apply_texture_set(restored_set,128);settle();
     require(coat_weight()>.99,"Saved coat lobe was lost on reopen");details["coat_save_open"]=true;
     for(auto &layer:restored_set.layers)if(layer.fill.native_material)layer.visible=false;
     host.apply_texture_set(restored_set,128);settle();
     require(coat_weight()==0,"Hiding imported material retained its coat lobe");details["coat_disabled_cleanly"]=true;
     for(auto &layer:restored_set.layers)if(layer.fill.native_material)layer.visible=true;host.apply_texture_set(restored_set,128);settle();
    }

    require(host.shader_failures().empty(),"Imported material shader compilation failed");
    const auto rendered=capture();require(!rendered.isNull()&&rendered.save(output+"/"+name+"-viewport.png"),"Cannot save native material rendering");
   }
   proof.append(details);
   QFile progress(output+"/material-import-progress.json");require(progress.open(QIODevice::WriteOnly),"Cannot save material import progress");progress.write(QJsonDocument(proof).toJson());
  }
  return {{"normal_material_imports",proof},{"renderer",QString::fromStdString(host.renderer())},{"visible_windows",0}};
 }
 QImage known(13,9,QImage::Format_RGBA8888);for(int y=0;y<known.height();++y)for(int x=0;x<known.width();++x)known.setPixelColor(x,y,QColor(x%3==0?255:0,y%3==0?255:0,(x+y)%2?255:0,255));
 const auto same=[](const QImage &a,const QImage &b){return !a.isNull()&&!b.isNull()&&a.convertToFormat(QImage::Format_RGBA8888)==b.convertToFormat(QImage::Format_RGBA8888);};
 const auto shelf=[&](const StudioAsset &asset,const QImage &saved){QImage expected(58,58,QImage::Format_RGB32);expected.fill(QColor(bts::theme::Surface));QPainter painter(&expected);const auto scaled=saved.scaled(58,58,Qt::KeepAspectRatio,Qt::SmoothTransformation);painter.drawImage((58-scaled.width())/2,(58-scaled.height())/2,scaled);painter.end();QElapsedTimer clock;clock.start();do{const auto actual=asset_thumbnail(asset);if(actual==expected)return;QCoreApplication::processEvents(QEventLoop::AllEvents,2);}while(clock.elapsed()<10000);throw std::runtime_error("Shelf thumbnail differs from saved PreviewImage pixels/aspect");};
 if(qEnvironmentVariableIsSet("BTS_NATIVE_THUMBNAIL_COLD")){
  auto document=host.open_project(project.toStdString());const auto resources=host.project_resources();require(resources.size()==3,"Cold project lost material resources");for(const auto &asset:resources){const auto preview=blend_material_preview(asset);if(asset.name=="BG No preview"){QImage expected(output+"/evaluated-preview.png");require(!expected.isNull()&&preview==expected,"Cold project changed evaluated native preview");shelf(asset,expected);}else{require(same(preview,known),"Cold project changed exact source PreviewImage");shelf(asset,known);}}
  return {{"fresh_process_project_open",true},{"source_absent",!QFileInfo::exists(source)},{"resources",int(resources.size())},{"saved_preview_exact",true},{"generated_preview_exact",true}};
 }
 const auto entries=inspect_blend_materials(source);const auto entry=std::find_if(entries.begin(),entries.end(),[](const auto &entry){return entry.name=="BG Saved";});require(entry!=entries.end()&&same(entry->preview,known),"Blender source PreviewImage pixels/orientation changed");const auto library=inspect_blend_asset_library(source,{});const auto indexed=std::find_if(library.materials.begin(),library.materials.end(),[](const auto &entry){return entry.material.name=="BG Saved";});require(indexed!=library.materials.end()&&same(indexed->material.preview,known),"Library index changed source PreviewImage");
 auto uniform=host.import_blend_material(source,"BG Saved",std::make_shared<std::atomic_bool>(false)).get().material;uniform.name="BG Uniform";require(same(blend_material_preview(uniform),known),"Uniform import changed source PreviewImage");shelf(uniform,known);
 NativeMaterialSourceLoader loader(host,nullptr,[]{},[](const QString &){});std::vector<StudioAsset> resources{uniform};QString failure;
 for(const auto *name:{"BG Saved","BG No preview"}){auto prepared=host.import_native_material(source,name,std::make_shared<std::atomic_bool>(false)).get();if(QString(name)=="BG Saved")require(same(blend_material_preview(prepared.material),known),"Native archive dropped source PreviewImage");loader.prepared(std::move(prepared),[&](const StudioAsset &asset){auto renamed=asset;if(renamed.name=="BG Saved")renamed.name="BG Native renamed";resources.push_back(std::move(renamed));},[&](const QString &error){failure=error;});QElapsedTimer clock;clock.start();while(loader.pending()){frame();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(clock.elapsed()<120000,"Native thumbnail GPU preparation timed out");}require(failure.isEmpty(),qPrintable(failure));}
 require(resources.size()==3,"Native previews did not publish both resources");require(same(blend_material_preview(resources[1]),known),"GPU preparation replaced authored PreviewImage");shelf(resources[1],known);
 const auto generated=blend_material_preview(resources[2]);require(!generated.isNull()&&generated!=material_sphere_preview(MaterialValues{},nullptr,64),"Native preview used default gray values");const auto center=generated.pixelColor(generated.width()/2,generated.height()/2);require(center.red()>center.green()*2&&center.red()>center.blue()*2,"Native fallback ignored linked evaluated red Color and used blue socket default");shelf(resources[2],generated);require(generated.save(output+"/evaluated-preview.png"),"Cannot save independent cold-open preview oracle");
 const auto passes=blender::GPU_pass_global_compilation_count();const auto published_hash=resources[2].hash();const auto status=host.native_material_status(resources[2].id);int resident_ready=0;loader.ensure(resources[2],{},[&](const auto &asset){require(asset.hash()==published_hash,"Resident preview asset changed its published content identity");++resident_ready;},[&](const auto &error){failure=error;});require(resident_ready==1&&!loader.pending()&&host.native_material_status(resources[2].id).maximum_step_ms==status.maximum_step_ms&&blender::GPU_pass_global_compilation_count()==passes,"Re-ensuring published preview repeated GPU preparation/compilation");
 // Recover an older native bundle without a saved preview through the same
 // Shelf owner bridge; the callback caches the result without changing libraries.
 auto legacy=resources[2];const auto bundle=read_native_material_bundle(legacy.bytes);legacy.id=QUuid::createUuid();legacy.bytes=native_material_bundle_bytes(bundle.blend,bundle.images);legacy.fill_preset->material_resource=legacy.id.toString(QUuid::WithoutBraces).toStdString();QObject owner;int scheduled=0;asset_thumbnail_native_generator(&owner,[&](const StudioAsset &asset,std::function<void(QImage)> ready){++scheduled;loader.ensure(asset,{},[ready](const auto &completed){ready(blend_material_preview(completed));},[&](const QString &error){failure=error;},true);});
 QElapsedTimer legacy_clock;legacy_clock.start();while(asset_thumbnail(legacy)!=asset_thumbnail(resources[2])){frame();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(legacy_clock.elapsed()<120000,"Legacy native Shelf preview did not finish");}require(scheduled==1&&failure.isEmpty(),"Legacy native preview was not scheduled exactly once");auto broken=legacy;broken.id=QUuid::createUuid();broken.bytes="not-a-Blender-source";broken.fill_preset->material_resource=broken.id.toString(QUuid::WithoutBraces).toStdString();QString expected_error;int error_requests=0;asset_thumbnail_native_generator(&owner,[&](const StudioAsset &asset,std::function<void(QImage)> ready){++error_requests;loader.ensure(asset,{},[ready](const auto &completed){ready(blend_material_preview(completed));},[&,ready](const QString &error){expected_error=error;ready({});},true);});QElapsedTimer error_clock;error_clock.start();while(asset_thumbnail(broken).pixelColor(0,0)!=QColor(bts::theme::ErrorSurface)){frame();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(error_clock.elapsed()<30000,"Native Shelf preview error did not resolve its pending placeholder");}require(error_requests==1&&!expected_error.isEmpty()&&!loader.pending(),"Native preview failure stayed pending or lost its reason");asset_thumbnail_native_generator(nullptr,{});
 NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;const auto imported=host.new_project((qEnvironmentVariable("BTS_ROOT")+"/mpainter/native/tests/fixtures/single_tile.obj").toStdString(),qEnvironmentVariable("BTS_ENVIRONMENT").toStdString(),&options);TextureDocument document(options,imported,host.scene_snapshot());for(const auto &set:document.texture_sets()){host.set_selected_material(set.material_session_uid);host.apply_texture_set(set,128);}QElapsedTimer initialize;initialize.start();while(host.texture_updates_pending()){frame();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(initialize.elapsed()<120000,"Preview project TextureSet initialization timed out");}for(auto &asset:resources)asset.scope=AssetScope::Project;host.begin_save_project(document,project.toStdString(),resources);QElapsedTimer clock;clock.start();while(host.save_project_pending()){frame();host.poll_save_project();QCoreApplication::processEvents(QEventLoop::AllEvents,2);require(clock.elapsed()<120000,"Preview project save timed out");}
 return {{"saved_preview_exact",true},{"uniform_shelf_exact",true},{"native_shelf_exact",true},{"no_preview_evaluated_channels",true},{"legacy_shelf_generation",true},{"published_asset_reensure_no_gpu_work",true},{"legacy_preview_error_resolved",true},{"generated_center",QJsonArray{center.red(),center.green(),center.blue()}},{"packed_project",project}};
}
}
