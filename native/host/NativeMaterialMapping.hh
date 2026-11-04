#pragma once
#include "ResourceAvailability.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
namespace bts {
// Authored after a retained linked coordinate value; never replaces its source.
struct NativeMaterialMapping {
 std::array<double,3> offset{},rotation_xyz_radians{},scale{1,1,1};
 bool operator==(const NativeMaterialMapping &)const=default;
 bool identity()const{return *this==NativeMaterialMapping{};}
 void validate()const{
  for(const auto *values:{&offset,&rotation_xyz_radians,&scale})for(double value:*values)
   if(!std::isfinite(value)||!std::isfinite(float(value)))throw std::invalid_argument("Native Mapping requires finite float32 values");
 }
 std::array<std::array<float,4>,3> rows()const{
  validate();std::array<std::array<float,4>,3> result{};const std::array values{offset,rotation_xyz_radians,scale};
  for(int row=0;row<3;++row)for(int c=0;c<3;++c)result[row][c]=float(values[row][c]);return result;
 }
};
inline QJsonObject native_material_mapping_json(const NativeMaterialMapping &value){
 value.validate();const auto vector=[](const auto &v){return QJsonArray{v[0],v[1],v[2]};};
 return {{"offset",vector(value.offset)},{"rotation_xyz_radians",vector(value.rotation_xyz_radians)},{"scale",vector(value.scale)}};
}
inline NativeMaterialMapping read_native_material_mapping(const QJsonValue &json){
 if(!json.isObject())throw std::invalid_argument("Invalid native Mapping transform");const auto object=json.toObject();
 if(object.size()!=3||!object.contains("offset")||!object.contains("rotation_xyz_radians")||!object.contains("scale"))throw std::invalid_argument("Unknown or missing native Mapping transform field");
 NativeMaterialMapping result;const auto read=[&](const char *name,auto &target){const auto source=object[name].toArray();if(!object[name].isArray()||source.size()!=3)throw std::invalid_argument("Native Mapping requires XYZ components");for(int c=0;c<3;++c){if(!source[c].isDouble())throw std::invalid_argument("Invalid native Mapping component");target[c]=source[c].toDouble();}};
 read("offset",result.offset);read("rotation_xyz_radians",result.rotation_xyz_radians);read("scale",result.scale);result.validate();return result;
}
// Three dynamic vectors follow the unchanged N-row ordinary parameter prefix.
inline constexpr std::string_view native_mapping_attribute_prefix="BTS_Map";
struct NativeMappingAttribute {std::size_t parameter;int row;};
inline std::string native_mapping_attribute(std::size_t parameter,int row){
 if(row<0||row>2)throw std::invalid_argument("Invalid native Mapping row");return std::string(native_mapping_attribute_prefix)+std::to_string(parameter)+"_"+std::to_string(row);
}
inline std::optional<NativeMappingAttribute> read_native_mapping_attribute(std::string_view name,std::size_t count){
 if(!name.starts_with(native_mapping_attribute_prefix))return std::nullopt;const auto suffix=name.substr(native_mapping_attribute_prefix.size());const auto separator=suffix.find('_');
 if(separator==std::string_view::npos||suffix.size()!=separator+2||suffix.back()<'0'||suffix.back()>'2')throw std::invalid_argument("Invalid native Mapping attribute");
 std::size_t index=0;const auto parsed=std::from_chars(suffix.data(),suffix.data()+separator,index);
 if(parsed.ec!=std::errc{}||parsed.ptr!=suffix.data()+separator||separator==0||index>=count)throw std::invalid_argument("Invalid native Mapping parameter binding");
 return NativeMappingAttribute{index,int(suffix.back()-'0')};
}
inline std::uint64_t native_mapping_parameter_rows(std::size_t count,bool mappings){return resource_product(count,mappings?4:1);}
}
