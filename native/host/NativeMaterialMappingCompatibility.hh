#pragma once
#include "NativeMaterialParameters.hh"
namespace bts {
// Called only after describing an actual retained source. Add/Multiply and the
// typed Vector edge have already been checked there. A historical immutable
// descriptor is not upgraded merely because a newer importer knows this source.
// This changes only the two newly derived capability flags; every surrounding
// full source-interface comparison remains strict.
// Immutable source SHA stays unchanged. Runtime adapter layouts for the same
// encoded graph must not borrow each other's Entry. No instance values or
// authored transform rows enter this key. Sources without this adapter keep
// their historical cache key; stable index + length-delimited identifier fixes
// parameter identity/order independently of the encoded graph's hash.
inline std::string native_vector_math_mapping_layout_key(const std::string &hash,const NativeMaterialDescription &description){
 std::string key=hash;bool domain=false;
 for(std::size_t i=0;i<description.parameters.size();++i){const auto &p=description.parameters[i];
  if(p.source_node!="ShaderNodeVectorMath"||p.source_socket!="Vector")continue;
  if(!domain){static constexpr char tag[]="\0BTS:vector-mapping-layout-v1\0";key.append(tag,sizeof(tag)-1);domain=true;}
  const auto id=p.identifier.toUtf8();key+=std::to_string(i)+":"+std::to_string(id.size())+":";key.append(id.constData(),id.size());key+=p.mapping?":1":":0";key+=p.editable?"1;":"0;";
 }
 return key;
}

inline void preserve_legacy_vector_math_mapping_policy(NativeMaterialDescription &actual,const NativeMaterialDescription &retained){
 if(actual.parameters.size()!=retained.parameters.size())return;
 const auto actual_rows=native_material_description_json(actual)["parameters"].toArray();
 const auto retained_rows=native_material_description_json(retained)["parameters"].toArray();
 for(std::size_t i=0;i<actual.parameters.size();++i){
  auto &now=actual.parameters[i];const auto &old=retained.parameters[i];
  if(old.mapping||old.editable||!now.mapping||!now.editable||!old.mapping_transform.identity()||!now.mapping_transform.identity()||
     now.type!=NativeParameterType::Vector||now.source_node!="ShaderNodeVectorMath"||now.source_socket!="Vector")continue;
  auto row=actual_rows[int(i)].toObject();row["mapping"]=false;row["editable"]=false;
  if(row!=retained_rows[int(i)].toObject())continue;
  now.mapping=false;now.editable=false;
 }
}
}
