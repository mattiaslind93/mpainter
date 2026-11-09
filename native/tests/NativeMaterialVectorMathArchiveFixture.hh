// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialMappingFixture.hh"
#include "NativeMaterialSource.hh"
#include "NativeMaterialArchive.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialResource.hh"
#include "NativeMaterialHeightResource.hh"
#include "NativeMaterialMappingCompatibility.hh"
#include "BKE_global.hh"
#include <QFile>
#include <QTemporaryDir>
#include <future>
#include <optional>
namespace bts::native_mapping_test {
struct ArchivedVectorMath {
 StudioAsset current,legacy;
 std::shared_ptr<NativeMaterialGraph> current_graph,legacy_graph;
 std::optional<NativeMaterialHeightResource> current_height,legacy_height,cross_layout_height;
 bool original_hidden=false;
};
// Captured by value on the test owner before its worker starts. No global color
// state is changed or borrowed on the worker; all graph/image storage is owned.
inline auto vector_math_test_color_context(){using namespace blender;if(!G_MAIN)throw std::runtime_error("Vector Math fixture has no initialized owner context");return G_MAIN->colorspace;}
template<class ColorContext> ArchivedVectorMath archived_vector_math_fixture(ColorContext context){
 using namespace blender;std::atomic_bool cancel=false;auto fixture=vector_math_fixture("UV",NODE_VECTOR_MATH_ADD);fixture.owner->colorspace=context;
 bNodeTree *group=nullptr;for(auto &node:fixture.material->nodetree->nodes)if(STREQ(node.idname,"ShaderNodeGroup"))group=reinterpret_cast<bNodeTree *>(node.id);
 if(!group)throw std::runtime_error("Vector Math Height fixture group missing");bNode *principled=nullptr,*separate=nullptr;
 for(auto &node:group->nodes){if(STREQ(node.idname,"ShaderNodeBsdfPrincipled"))principled=&node;if(STREQ(node.idname,"ShaderNodeSeparateXYZ"))separate=&node;}
 if(!principled||!separate)throw std::runtime_error("Vector Math Height fixture source missing");
 auto *bump=bke::node_add_node(nullptr,*group,UString("ShaderNodeBump"));bke::node_add_link(*group,*separate,socket(*separate,"X",false),*bump,socket(*bump,"Height",true));bke::node_add_link(*group,*bump,socket(*bump,"Normal",false),*principled,socket(*principled,"Normal",true));
 BKE_ntree_update_after_single_tree_change(*fixture.owner,*group);BKE_ntree_update_after_single_tree_change(*fixture.owner,*fixture.material->nodetree);
 DetachedNativeMaterial detached;detached.material=fixture.material;detached.main=std::move(fixture.owner);auto graph=finish_native_material_source(std::move(detached),cancel);
 QTemporaryDir files;if(!files.isValid())throw std::runtime_error("Vector Math fixture temporary directory failed");const auto original=files.filePath("own-vector-source.blend");auto archive=archive_native_material(*graph,original,cancel);
 QFile file(original);if(!file.open(QIODevice::WriteOnly)||file.write(archive.bytes)!=archive.bytes.size())throw std::runtime_error("Cannot write owned Vector Math source fixture");file.close();
 ArchivedVectorMath result;result.current=native_material_asset("Owned Vector Math current",archive.bytes,archive.description);auto legacy=archive.description;
 for(auto &p:legacy.parameters){p.mapping=false;p.editable=false;legacy.warnings.append(p.name+": linked "+p.source_node+" is retained, not replaced by a constant");}
 result.legacy=native_material_asset("Owned Vector Math historical",archive.bytes,legacy);
 if(result.current.bytes!=result.legacy.bytes||result.current.hash()!=result.legacy.hash())throw std::runtime_error("Legacy capability fixture changed encoded source bytes");
 if(!QFile::rename(original,original+".hidden")||QFile::exists(original))throw std::runtime_error("Cannot hide owned Vector Math source fixture");result.original_hidden=true;
 result.current_graph=hydrate_native_material_resource(result.current,cancel);result.legacy_graph=hydrate_native_material_resource(result.legacy,cancel);
 const auto rows=[](const auto &d){return native_material_description_json(d)["parameters"];};
 if(rows(result.current_graph->description)!=rows(archive.description)||rows(result.legacy_graph->description)!=rows(legacy))throw std::runtime_error("Actual native archive hydration changed current/legacy policy");
 for(const auto &channel:result.legacy_graph->channels)if(channel.tree)for(const auto &node:channel.tree->nodes)if(STREQ(node.idname,"ShaderNodeAttribute")&&node.storage&&std::string_view(static_cast<const NodeShaderAttribute *>(node.storage)->name).starts_with(native_mapping_attribute_prefix))throw std::runtime_error("Legacy source received a new runtime Mapping adapter");
 result.current_height.emplace(prepare_native_material_height_resource(result.current,{},cancel));result.legacy_height.emplace(prepare_native_material_height_resource(result.legacy,{},cancel));result.cross_layout_height.emplace(prepare_native_material_height_resource(result.current,{},cancel));
 return result;
}
}
