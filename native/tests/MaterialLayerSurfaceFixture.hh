// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Test-only portable fixture, consumed later through the REAL product Import.
// No Main/Image/GPU/document is created or changed here; only fresh package IO.
#include "MaterialLayerSurfaceReference.hh" // Frozen independent literal recipe.
#include "MaterialLayerParameterIO.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include "IMB_metadata.hh"
#include "DNA_scene_types.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <atomic>
#include <memory>
#include <set>
#include <string_view>

namespace bts::pd149_surface_test {

inline constexpr int surface_fixture_size=128;
inline constexpr std::array<int,2> surface_fixture_tiles{1001,1002};

struct SurfaceFixture {
 QString directory;
 QJsonObject manifest,evidence;
};

inline void fixture_cancelled(const std::atomic_bool &cancel){
 if(cancel.load())throw std::runtime_error("Surface package fixture cancelled");
}

inline QByteArray encode_uniform_mask32(float mask,const std::atomic_bool &cancel){
 using namespace blender;using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
 using pd149_test::require;
 fixture_cancelled(cancel);require(std::isfinite(mask)&&mask>=0&&mask<=1,
                                  "Surface fixture mask outside 0..1");
 // Same bounded float-image staging as parameter Export, with no GPU readback
 // or production compositor. R is authoritative; G/B=0 and alpha=1 explicitly.

 Buffer image(IMB_allocImBuf(surface_fixture_size,surface_fixture_size,ImBufFlags::FloatData),IMB_freeImBuf);
 if(!image)throw std::bad_alloc();auto *pixels=image->float_data_for_write();
 require(pixels&&image->channels==4,"Surface fixture requires actual RGBA float storage");
 for(int y=0;y<surface_fixture_size;++y){fixture_cancelled(cancel);
  for(int x=0;x<surface_fixture_size;++x){const auto at=(std::size_t(y)*surface_fixture_size+x)*4;
   pixels[at]=mask;pixels[at+1]=pixels[at+2]=0;pixels[at+3]=1;}
 }
 image->color_mode=ImColorMode::RGBA;image->ftype=IMB_FTYPE_OPENEXR;
 image->foptions.flag=R_IMF_EXR_CODEC_ZIP;image->foptions.compress=15;
 require(!(image->foptions.flag&OPENEXR_HALF),"Surface fixture must not encode half precision");
 const char *data_space=IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA);
 require(data_space&&*data_space,"Surface fixture native DATA space unavailable");
 IMB_colormanagement_assign_float_colorspace(image.get(),data_space);
 constexpr auto packed=ImBufFlags::FloatData|ImBufFlags::AlphaChannelPacked;
 const auto bytes=IMB_save_image_to_buffer(image.get(),packed);
 require(!bytes.is_empty(),"Surface fixture float EXR encoding failed");
 require((image->flags&ImBufFlags::AlphaChannelPacked)!=ImBufFlags::Zero,
         "Surface fixture encode alpha policy differs");

 const QByteArray result(reinterpret_cast<const char *>(bytes.data()),bytes.size());
 // This is an explicit fixture self-check, not a source/mask shader oracle.
 // HALF absence is checked after actual EXR decode, not inferred from values
 // that happen to be exactly representable at lower precision as well.
 constexpr auto flags=ImBufFlags::AlphaChannelPacked|ImBufFlags::NoColorspaceConvert|ImBufFlags::Metadata;
 Buffer decoded(IMB_load_image_from_memory(reinterpret_cast<const unsigned char *>(result.constData()),
                result.size(),flags,"surface-opacity.exr","surface-opacity.exr"),IMB_freeImBuf);
 require(decoded&&decoded->ftype==IMB_FTYPE_OPENEXR&&decoded->x==surface_fixture_size&&
  decoded->y==surface_fixture_size&&decoded->channels==4&&decoded->float_data()&&
  !(decoded->foptions.flag&OPENEXR_HALF),"Surface fixture decoded EXR is not RGBA FLOAT32");
 require((decoded->flags&ImBufFlags::AlphaChannelPacked)!=ImBufFlags::Zero,
         "Surface fixture decode would apply an alpha conversion");
 char interop[64]{};require(IMB_metadata_get_field(decoded->metadata,"colorInteropID",interop,sizeof(interop))&&
                           std::string_view(interop)=="data","Surface fixture lost native DATA metadata");
 const auto *actual=decoded->float_data();
 for(int y=0;y<surface_fixture_size;++y){fixture_cancelled(cancel);
  for(int x=0;x<surface_fixture_size;++x){const auto at=(std::size_t(y)*surface_fixture_size+x)*4;
   require(actual[at]==mask&&actual[at+1]==0&&actual[at+2]==0&&actual[at+3]==1,
           "Surface fixture literal R/zeroGB/alpha1 changed during encoding");}
 }
 return result;
}

// Pass the actual target working-space identity/matrix copied on its owner.
// This helper retains no Main pointers, changes no global OCIO setting, and may
// run as explicit test-fixture preparation before requesting public Import.
inline SurfaceFixture write_surface_fixture(const QString &directory,const QString &working_space,
 const std::array<double,9> &scene_linear_to_xyz,const std::atomic_bool &cancel){
 using pd149_test::require;fixture_cancelled(cancel);
 require(QDir::isAbsolutePath(directory)&&!QFileInfo::exists(directory)&&
         QDir(QFileInfo(directory).absolutePath()).exists(),"Surface fixture needs a fresh private package path");
 MaterialLayerParameters parameters;parameters.name="Independent uniform surface fixture";
 parameters.enabled=true;parameters.working_space=working_space;parameters.scene_linear_to_xyz=scene_linear_to_xyz;
 parameters.shader.normal_intensity=1;parameters.shader.ao_intensity=float(uniform_ao_intensity);
 for(int tile:surface_fixture_tiles)parameters.tiles.emplace(tile,std::array<int,2>{surface_fixture_size,surface_fixture_size});
 for(int index=0;index<4;++index){auto &input=parameters.materials[index];const auto &literal=uniform_inputs[index];
  input.name="Independent surface input "+std::to_string(index+1);
  input.values.base_color={float(literal.color[0]),float(literal.color[1]),float(literal.color[2]),1};
  input.values.metallic=float(literal.metallic);input.values.roughness=float(literal.roughness);
  input.values.ambient_occlusion=float(literal.ao);input.values.normal={0,0,1};input.values.bump=.5f;
  // All six source slots and both Color processor tables stay empty: v1 only.
  input.validate();
 }
 std::set<QString> unique_encodings;
 for(int stack=0;stack<3;++stack){fixture_cancelled(cancel);
  const QByteArray bytes=encode_uniform_mask32(float(uniform_masks[stack]),cancel);
  const QString checksum=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
  unique_encodings.insert(checksum);MaterialParameterOriginal original;original.asset=QUuid::createUuid();
  auto mask=std::make_shared<FillTexture>();mask->asset=original.asset.toString(QUuid::WithoutBraces).toStdString();
  mask->name="Uniform surface opacity "+std::to_string(stack+1);mask->raw=true;
  mask->width=mask->height=surface_fixture_size;
  auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();
  std::vector<TextureSourceFile> files;
  for(int tile:surface_fixture_tiles){MaterialParameterFile file;file.bytes=bytes;
   file.descriptor={tile,QString("surface-opacity%1.%2.exr").arg(stack+1).arg(tile),{},checksum,bytes.size()};
   auto member=std::make_shared<FillTexture>(*mask);member->hash=checksum.toStdString();member->validate();
   members->emplace(tile,std::move(member));files.push_back(file.descriptor);original.files.push_back(std::move(file));
  }
  mask->hash=texture_sources_hash(files).toStdString();mask->tiles=std::move(members);mask->validate();
  parameters.masks[stack]=std::move(mask);parameters.originals.push_back(std::move(original));
 }
 const QJsonObject manifest=material_layer_parameters_json(parameters);
 require(manifest["version"]==1&&manifest["source_members_version"]==3&&parameters.originals.size()==3&&
         unique_encodings.size()==3,"Surface fixture must be a v1 package with three unique mask payloads");
 for(int input=0;input<4;++input){require(parameters.color_processors[input].isEmpty()&&parameters.color_members[input].empty(),
                                       "Surface fixture unexpectedly has a Color source processor");
  for(const auto &texture:parameters.materials[input].textures)require(!texture,"Surface fixture has a material source texture");
 }
 // The existing writer owns checksum-verified staging and atomic publication.
 // Use its reader too: metadata-only relative paths are never rewritten here.
 write_material_layer_parameter_package(directory,parameters,cancel);
 const auto portable=read_material_layer_parameter_package(directory,cancel);
 require(material_layer_parameters_json(portable)==manifest&&QDir(directory+"/payloads").entryList(QDir::Files).size()==3,
         "Surface fixture portable v1 reader/payload layout differs");
 SurfaceFixture result;result.directory=directory;result.manifest=manifest;
 result.evidence={{"package_version",1},{"source_members_version",3},{"uniform_inputs",4},
  {"source_textures",0},{"mask_resources",3},{"logical_mask_members",6},{"unique_payload_files",3},
  {"destination_tiles",QJsonArray{1001,1002}},{"width",128},{"height",128},
  {"literal_masks",QJsonArray{.25,.5,.125}},{"encoded_storage","RGBA FLOAT32 EXR; raw R/zeroGB/alpha1"},
  {"unique_images_roundtrip_checked",3},{"unique_R_texels_checked",3*128*128},
  {"alpha_channel_packed_encode_and_decode",true},{"native_data_interop_checked",true},
  {"public_package_write_read_metadata_exact",true},{"runtime_mask_precision_proved_here",false},
  {"consumption","Future real product Import; no diagnostic publication"}};
 return result;
}
}
