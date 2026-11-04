#pragma once
#include "NativeMaterialImageBindings.hh"
#include <QUuid>
#include <optional>
#include <algorithm>

namespace bts {
struct NativeImageVariantOperation {
  NativeImageBinding binding;
  QString replacement_sha256;
  // Explicit resolved input interpretation, including a named Data space.
  QString source_space;
  QString source_space_policy; // "named" or "original-declared", never inferred.
  QString precision_policy="native-float32-replacement";
};
struct NativeImageVariantProvenance {
  // Always the FIRST original source, never the preceding derived variant.
  QUuid original_id;
  QString original_sha256;
  std::vector<NativeImageVariantOperation> operations;
};
namespace native_image_variant_metadata {
inline void sha256(const QString &value){
  if(value.size()!=64||!std::all_of(value.begin(),value.end(),[](QChar c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))throw std::invalid_argument("Invalid native image variant SHA256");
}
inline std::uint64_t operation_bytes(const NativeImageVariantOperation &value){
  const auto &b=value.binding;auto units=resource_sum(resource_sum(b.output.size(),b.input.size()),resource_sum(b.consumer_type.size(),b.image.size()));
  units=resource_sum(units,b.source_settings["source_space"].toString().size());
  units=resource_sum(units,resource_sum(value.replacement_sha256.size(),resource_sum(value.source_space.size(),resource_sum(value.source_space_policy.size(),value.precision_policy.size()))));
  for(const auto &group:b.groups)units=resource_sum(units,group.group.size());
  return resource_sum(resource_product(units,16),resource_product(resource_sum(b.groups.size(),1),2048));
}
inline void operation(const NativeImageVariantOperation &value){
  sha256(value.replacement_sha256);native_image_binding_json(value.binding);
  if(value.precision_policy!="native-float32-replacement")throw std::invalid_argument("Unsupported native image replacement precision policy");
  native_image_binding_metadata::text(value.source_space);
  if(value.source_space_policy!="named"&&value.source_space_policy!="original-declared")throw std::invalid_argument("Unknown native image replacement source-space policy");
  if(value.source_space_policy=="original-declared"&&value.source_space!=value.binding.source_settings["source_space"].toString())throw std::invalid_argument("Native image source-space policy does not match its original declaration");
}
}
inline std::uint64_t native_image_variant_provenance_bytes(const NativeImageVariantProvenance &value){
  auto bytes=resource_sum(1024,resource_product(value.operations.size(),sizeof(NativeImageVariantOperation)+512));
  for(const auto &op:value.operations)bytes=resource_sum(bytes,native_image_variant_metadata::operation_bytes(op));return bytes;
}
inline QJsonObject native_image_variant_provenance_json(const NativeImageVariantProvenance &value){
  if(value.original_id.isNull()||value.operations.empty())throw std::invalid_argument("Native image variant has no original Reset source/operation");
  native_image_variant_metadata::sha256(value.original_sha256);

  QJsonArray operations;
  for(const auto &op:value.operations){native_image_variant_metadata::operation(op);operations.append(QJsonObject{{"binding",native_image_binding_json(op.binding)},{"replacement_sha256",op.replacement_sha256},{"source_space",op.source_space},{"source_space_policy",op.source_space_policy},{"precision_policy",op.precision_policy}});}
  return {{"version",1},{"original_id",value.original_id.toString(QUuid::WithoutBraces)},{"original_sha256",value.original_sha256},{"operations",operations}};
}
inline NativeImageVariantProvenance read_native_image_variant_provenance(const QJsonObject &o){
  using namespace native_image_binding_metadata;
  keys(o,{"version","original_id","original_sha256","operations"});
  if(integer(o["version"])!=1||!o["operations"].isArray()||o["operations"].toArray().isEmpty())invalid();
  NativeImageVariantProvenance value;value.original_id=QUuid(text(o["original_id"]));value.original_sha256=text(o["original_sha256"]);
  const auto operations=o["operations"].toArray();
  auto used=resource_sum(1024,resource_product(operations.size(),sizeof(NativeImageVariantOperation)+512));
  for(const auto &entry:operations){if(!entry.isObject())invalid();const auto op=entry.toObject();keys(op,{"binding","replacement_sha256","source_space","source_space_policy","precision_policy"});if(!op["binding"].isObject())invalid();NativeImageVariantOperation decoded{read_native_image_binding(op["binding"].toObject()),text(op["replacement_sha256"]),text(op["source_space"]),text(op["source_space_policy"]),text(op["precision_policy"])};used=resource_sum(used,native_image_variant_metadata::operation_bytes(decoded));value.operations.push_back(std::move(decoded));}
  native_image_variant_provenance_json(value);return value;
}
}
