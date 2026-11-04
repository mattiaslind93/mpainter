#include "NativeMaterialParameters.hh"
#include "NativeMaterialMappingCompatibility.hh"
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>

namespace {
template<class Function> bool rejects(Function function){try{function();return false;}catch(const std::invalid_argument &){return true;}}
std::uint64_t scarce_memory(){return 1;}
}
int main(){
 using namespace bts;
 NativeMaterialDescription source;source.material="Retained source";source.group="Independent input";source.surface_type="ShaderNodeGroup";
 NativeMaterialParameter parameter;parameter.identifier="Input_2";parameter.name="Mapping";parameter.type=NativeParameterType::Vector;parameter.mapping=true;parameter.source_node="ShaderNodeTexCoord";parameter.source_socket="UV";parameter.value={.125,-.5,3,0};source.parameters={parameter};
 const auto original=native_material_description_json(source);assert(original["version"]==2);assert(!original["parameters"].toArray()[0].toObject().contains("mapping_transform"));
 const auto shared=std::make_shared<const NativeMaterialDescription>(source);auto instance=*shared;
 auto &mapping=instance.parameters[0].mapping_transform;mapping.offset={-2,.25,4};mapping.rotation_xyz_radians={.4,-.2,.7};mapping.scale={-3,0,2};
 const auto edited=native_material_description_json(instance);assert(edited["version"]==3);assert(native_material_description_json(read_native_material_description(edited))==edited);
 assert(instance.parameters[0].value==parameter.value&&instance.parameters[0].source_node==parameter.source_node&&instance.parameters[0].source_socket==parameter.source_socket&&instance.parameters[0].identifier==parameter.identifier);
 assert(native_material_description_json(*shared)==original&&shared->parameters[0].mapping_transform.identity());
 const auto rows=mapping.rows();assert(rows[0][0]==-2&&rows[0][3]==0&&rows[1][2]==float(.7)&&rows[2][0]==-3&&rows[2][1]==0&&rows[2][3]==0);
 for(const QString basis:{"UV","Generated","Object"}){auto direct=instance;direct.parameters[0].source_socket=basis;assert(direct.parameters[0].mapping_transform_supported());direct.validate();}
 for(int mutation=0;mutation<5;++mutation){auto invalid=instance;auto &p=invalid.parameters[0];switch(mutation){case 0:p.source_socket="Reflection";break;case 1:p.source_node="ShaderNodeVectorMath";break;case 2:p.mapping=false;break;case 3:p.editable=false;break;case 4:p.type=NativeParameterType::Color;break;}assert(!p.mapping_transform_supported()&&rejects([&]{invalid.validate();}));}
 auto retained_mapping=instance;retained_mapping.parameters[0].source_node="ShaderNodeMapping";retained_mapping.parameters[0].source_socket="Vector";assert(retained_mapping.parameters[0].mapping_transform_supported());retained_mapping.validate();const auto mapped_json=native_material_description_json(retained_mapping);assert(native_material_description_json(read_native_material_description(mapped_json))==mapped_json);
 auto retained_math=instance;retained_math.parameters[0].source_node="ShaderNodeVectorMath";retained_math.parameters[0].source_socket="Vector";assert(retained_math.parameters[0].mapping_transform_supported());retained_math.validate();const auto math_json=native_material_description_json(retained_math);assert(native_material_description_json(read_native_material_description(math_json))==math_json);
 auto other_math=retained_math;other_math.parameters[0].mapping_transform.offset[1]=.625;assert(native_material_description_json(retained_math)==math_json&&native_material_description_json(*shared)==original);
 retained_math.parameters[0].mapping_transform={};const auto math_identity=native_material_description_json(retained_math);assert(math_identity["version"]==2&&!math_identity["parameters"].toArray()[0].toObject().contains("mapping_transform"));
 for(const QString wrong:{"Value","UV","Normal"}){auto bad=retained_math;bad.parameters[0].source_socket=wrong;bad.parameters[0].mapping_transform.offset[0]=1;assert(!bad.parameters[0].mapping_transform_supported()&&rejects([&]{bad.validate();}));}
 // Historical sources remain byte-identical and uneditable. A source-derived
 // capability is suppressed only when every other typed parameter field agrees.
 auto legacy_math=retained_math;legacy_math.parameters[0].mapping=false;legacy_math.parameters[0].editable=false;const auto legacy_json=native_material_description_json(legacy_math);
 auto restored_math=retained_math;preserve_legacy_vector_math_mapping_policy(restored_math,legacy_math);assert(native_material_description_json(restored_math)==legacy_json&&!restored_math.parameters[0].mapping_transform_supported());
 for(int mutation=0;mutation<10;++mutation){auto changed=retained_math;auto &p=changed.parameters[0];switch(mutation){case 0:p.identifier+="other";break;case 1:p.name+="other";break;case 2:p.value[1]+=.25;break;case 3:p.minimum-=1;break;case 4:p.maximum+=1;break;case 5:p.source_node="ShaderNodeMapping";break;case 6:p.source_socket="UV";break;case 7:p.ramp["unexpected"]=true;break;case 8:p.mapping_transform.offset[0]=.25;break;case 9:p.type=NativeParameterType::Color;break;}const auto before=native_material_description_json(changed);preserve_legacy_vector_math_mapping_policy(changed,legacy_math);assert(native_material_description_json(changed)==before&&native_material_description_json(changed)!=legacy_json);}
 auto downgrade=legacy_math;preserve_legacy_vector_math_mapping_policy(downgrade,retained_math);assert(native_material_description_json(downgrade)==legacy_json&&native_material_description_json(downgrade)!=math_identity);
 const std::string source_hash="immutable-encoded-source";assert(native_vector_math_mapping_layout_key(source_hash,source)==source_hash);
 const auto current_key=native_vector_math_mapping_layout_key(source_hash,retained_math),legacy_key=native_vector_math_mapping_layout_key(source_hash,legacy_math);assert(current_key!=legacy_key);
 auto per_instance=retained_math;per_instance.parameters[0].mapping_transform.offset[2]=.7;assert(native_vector_math_mapping_layout_key(source_hash,per_instance)==current_key);per_instance.parameters[0].value[0]+=.25;assert(native_vector_math_mapping_layout_key(source_hash,per_instance)==current_key);per_instance.parameters[0].identifier+="other";assert(native_vector_math_mapping_layout_key(source_hash,per_instance)!=current_key);
 for(double invalid:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),double(std::numeric_limits<float>::max())*2})for(int row=0;row<3;++row){auto value=mapping;auto &components=row==0?value.offset:row==1?value.rotation_xyz_radians:value.scale;components[1]=invalid;assert(rejects([&]{value.validate();}));}
 auto json=native_material_mapping_json(mapping);json["unrecognized"]=true;assert(rejects([&]{read_native_material_mapping(json);}));json=native_material_mapping_json(mapping);json.remove("scale");assert(rejects([&]{read_native_material_mapping(json);}));json=native_material_mapping_json(mapping);json["scale"]=QJsonArray{1,2};assert(rejects([&]{read_native_material_mapping(json);}));json["scale"]=QJsonArray{1,"2",3};assert(rejects([&]{read_native_material_mapping(json);}));assert(rejects([&]{read_native_material_mapping(QJsonArray{});}));
 for(int version:{1,2}){auto old=edited;old["version"]=version;assert(rejects([&]{read_native_material_description(old);}));auto compatible=original;compatible["version"]=version;assert(read_native_material_description(compatible).parameters[0].mapping_transform.identity());}
 instance.parameters[0].mapping_transform={};assert(native_material_description_json(instance)==original);
 for(std::size_t i:{std::size_t(0),std::size_t(3),std::size_t(1000000)})for(int row=0;row<3;++row){const auto parsed=read_native_mapping_attribute(native_mapping_attribute(i,row),i+1);assert(parsed&&parsed->parameter==i&&parsed->row==row);}
 assert(!read_native_mapping_attribute("BTS_Param0",1));for(const char *invalid:{"BTS_Map","BTS_Map_0","BTS_Map0_3","BTS_Map1_0","BTS_Map-1_0","BTS_Map0_00","BTS_Map0x_0"})assert(rejects([&]{read_native_mapping_attribute(invalid,1);}));assert(rejects([&]{native_mapping_attribute(0,3);}));
 assert(native_mapping_parameter_rows(17,false)==17&&native_mapping_parameter_rows(17,true)==68);
 if(sizeof(std::size_t)==sizeof(std::uint64_t)){bool overflow=false;try{native_mapping_parameter_rows(std::numeric_limits<std::size_t>::max(),true);}catch(const std::overflow_error &){overflow=true;}assert(overflow);}
 {TextureRamQueryScope memory(scarce_memory);bool denied=false;try{instance.validate();}catch(const std::length_error &){denied=true;}assert(!denied);}
 std::cout<<"Native linked Mapping: typed v3, exact v2 identity/reset, source COW, explicit direct bases, finite float32 and checked dynamic rows PASS\n";
}
