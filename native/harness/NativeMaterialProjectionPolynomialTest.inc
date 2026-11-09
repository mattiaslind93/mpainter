// SPDX-License-Identifier: GPL-3.0-or-later
QJsonObject polynomial_report;
{
 for(auto &point:points){point.position={.2f,.3f,.4f};point.normal={.6f,0,.8f};point.footprint=0;point.valid=true;}upload_projection_points();
 auto *tree=bke::node_tree_add_tree(main.get(),"Native nonlinear plane oracle","ShaderNodeTree");tree->flag|=NTREE_IS_GPU_SHADER_INTERNAL;
 auto *attribute=bke::node_add_node(nullptr,*tree,UString("ShaderNodeAttribute"));STRNCPY(static_cast<NodeShaderAttribute *>(attribute->storage)->name,"BTS_Geometry_Position");
 auto *mapping=bke::node_add_node(nullptr,*tree,UString("ShaderNodeMapping"));mapping->custom1=NODE_MAPPING_TYPE_POINT;
 const std::array<float,3> own_scale{.8f,1.1f,.7f},own_offset{.07f,-.03f,.02f};
 std::copy_n(own_scale.data(),3,static_cast<bNodeSocketValueVector *>(socket(*mapping,"Scale",true).default_value)->value);std::copy_n(own_offset.data(),3,static_cast<bNodeSocketValueVector *>(socket(*mapping,"Location",true).default_value)->value);
 auto *xyz=bke::node_add_node(nullptr,*tree,UString("ShaderNodeSeparateXYZ"));bke::node_add_link(*tree,*attribute,socket(*attribute,"Vector",false),*mapping,socket(*mapping,"Vector",true));bke::node_add_link(*tree,*mapping,socket(*mapping,"Vector",false),*xyz,socket(*xyz,"Vector",true));
 const auto math=[&](int operation,bNode &left,const char *left_id,bNode *right,const char *right_id,float constant){auto *node=bke::node_add_node(nullptr,*tree,UString("ShaderNodeMath"));node->custom1=operation;auto *input=static_cast<bNodeSocket *>(node->inputs.first);bke::node_add_link(*tree,left,socket(left,left_id,false),*node,*input);if(right)bke::node_add_link(*tree,*right,socket(*right,right_id,false),*node,*input->next);else static_cast<bNodeSocketValueFloat *>(input->next->default_value)->value=constant;return node;};
 auto *square=math(NODE_MATH_MULTIPLY,*xyz,"X",xyz,"X",0),*y=math(NODE_MATH_MULTIPLY,*xyz,"Y",nullptr,"",.3f),*z=math(NODE_MATH_MULTIPLY,*xyz,"Z",nullptr,"",.1f);auto *xy=math(NODE_MATH_ADD,*square,"Value",y,"Value",0),*sum=math(NODE_MATH_ADD,*xy,"Value",z,"Value",0);
 auto *parameter=bke::node_add_node(nullptr,*tree,UString("ShaderNodeAttribute"));STRNCPY(static_cast<NodeShaderAttribute *>(parameter->storage)->name,"BTS_Param0");auto *offset=math(NODE_MATH_ADD,*sum,"Value",parameter,"Fac",0);
 auto *aov=bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputAOV"));STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name,"BTS_Channel");bke::node_add_link(*tree,*offset,socket(*offset,"Value",false),*aov,socket(*aov,"Value",true));bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(*main,*tree);NativeMaterialGpuProgram program(*tree,1,4);
 const auto polynomial=[&](double x,double y,double z){x=x*own_scale[0]+own_offset[0];y=y*own_scale[1]+own_offset[1];z=z*own_scale[2]+own_offset[2];return x*x+double(.3f)*y+double(.1f)*z;};
 std::uint64_t checked_polynomial=0;double nonlinear_error=0;const auto compilations=GPU_pass_global_compilation_count();
 for(bool footprint:{false,true}){
  for(auto &point:points)point.footprint=footprint?.12f:0;upload_projection_points();
  FillProjection p;p.mode=1;p.hardness=0;p.filtering=footprint?0:1;context.projection=native_material_projection(p,&atlas,set);
  double expected=0;const int taps=footprint?4:1;for(int axis:{0,2})for(int tap=0;tap<taps;++tap){const int x=(axis+1)%3,y=(axis+2)%3;const std::array<double,3> local{.1,.15,.2};const double dx=footprint?((tap&1)?1:-1)*.25*.12*.5:0,dy=footprint?((tap&2)?1:-1)*.25*.12*.5:0;expected+=polynomial(local[x]+.5+dx,local[y]+.5+dy,local[axis])*(axis==0?.6:.8)/1.4/taps;}
  for(float shift:{-2.f,0.f,3.f})for(int tile:{1001,1002,1011}){context.tile=tile;const std::array<std::array<float,4>,1> values{{{shift,shift,shift,1}}};program.dispatch(target.get(),values,context,{0,0,width,height});auto *actual=pixels();require(actual,"Nonlinear tri-plane RawHeight readback missing");const auto release=qScopeGuard([&]{MEM_delete(actual);});for(int i=0;i<width*height;++i){const double error=std::abs(actual[i*4]-(expected+shift));nonlinear_error=std::max(nonlinear_error,error);require(error<2e-6&&std::abs(actual[i*4+3]-1)<2e-6,"Native triplanar evaluated averaged coordinates, repeated source Mapping, or clamped raw Height");++checked_polynomial;}}
 }
 require(GPU_pass_global_compilation_count()==compilations,"Footprint or raw scalar edits recompiled native graph");
 polynomial_report={{"probes",qint64(checked_polynomial)},{"maximum_error",nonlinear_error},{"fixed_bound",2e-6},{"actual_graph_per_plane",true},{"source_mapping_once",true},{"hq_footprint_taps",true},{"signed_hdr_raw_height",true}};
}
