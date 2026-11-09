// SPDX-License-Identifier: GPL-3.0-or-later
// Private standard-node codec. Included after Builder, never a separate TU.
// Same coordinate/interpolation graph for runtime AP1 and saved raw encoding.
// Only saved encoding has the admitted OCIO moncurve before the four-way mix.
QJsonObject srgb_filter_source_json(const FillTexture &source){
 auto value=fill_texture_json(std::make_shared<const FillTexture>(source)).toObject();value["input_space"]=QString::fromStdString(source.input_space);value["floating_point"]=source.floating_point;return value;
}
std::shared_ptr<const FillTexture> srgb_filter_source(bNodeTree &tree,const std::string &prefix,bool &encoded){
 auto *n=bke::node_find_node_by_name(tree,prefix+"SrgbRepresentation");
 if(!n||!STREQ(n->idname,"ShaderNodeValue")||(n->flag&NODE_MUTED)||n->id||!n->prop||n->prop->type!=IDP_GROUP||BLI_listbase_count(&n->prop->data.group)!=1)throw std::invalid_argument("sRGB graph value provenance is missing");
 auto *property=IDP_GetPropertyFromGroup(n->prop,"source");if(!property||property->type!=IDP_STRING||property->len<1||!IDP_string_get(property)||IDP_string_get(property)[property->len-1]!=0)throw std::invalid_argument("sRGB graph value provenance changed");
 QJsonParseError error;const auto json=QJsonDocument::fromJson(QByteArray::fromRawData(IDP_string_get(property),property->len-1),&error);if(error.error!=QJsonParseError::NoError||!json.isObject())throw std::invalid_argument("Invalid sRGB graph metadata");
 auto result=std::make_shared<FillTexture>(*read_fill_texture_json(json.object(),false));result->input_space=json.object()["input_space"].toString().toStdString();result->floating_point=json.object()["floating_point"].toBool(true);
 if(json.object()!=srgb_filter_source_json(*result)||!material_layer_source_color_transform(*result).srgb)throw std::invalid_argument("Unsupported sRGB graph provenance");
 const auto *socket=static_cast<const bNodeSocket *>(n->outputs.first);if(!socket||socket->type!=SOCK_FLOAT||!socket->default_value)throw std::invalid_argument("sRGB graph representation marker changed");const auto value=static_cast<const bNodeSocketValueFloat *>(socket->default_value)->value;if(value!=0&&value!=1)throw std::invalid_argument("Invalid sRGB graph representation");encoded=value==1;return result;
}
struct SrgbFilter {
 Builder &b;std::string prefix;const FillTexture &source;bool encoded,check;
 std::set<bNode *> nodes;std::set<std::pair<bNodeSocket *,bNodeSocket *>> edges;
 bNode &node(const char *type,const std::string &suffix){
  const auto name=prefix+suffix;auto *n=bke::node_find_node_by_name(b.tree,name);
  if(check){if(!n||!STREQ(n->idname,type)||(n->flag&NODE_MUTED)||(n->prop&&suffix!="SrgbRepresentation")||(!STREQ(type,"ShaderNodeTexImage")&&n->id))throw std::invalid_argument("sRGB filter node provenance changed");}
  else n=&b.node(type,name);
  nodes.insert(n);return *n;
 }
 void op(bNode &n,int operation){if(check){if(n.custom1!=operation||n.custom2)throw std::invalid_argument("sRGB filter operation changed");}else{b.operation(n,operation);if(n.custom2){n.custom2=0;BKE_ntree_update_tag_node_property(&b.tree,&n);b.changed=true;}}}
 void scalar(bNodeSocket &s,float value){
  if(check){if(s.type!=SOCK_FLOAT||!s.default_value||static_cast<const bNodeSocketValueFloat *>(s.default_value)->value!=value)throw std::invalid_argument("sRGB filter scalar changed");for(const auto &l:b.tree.links)if(l.tosock==&s)throw std::invalid_argument("sRGB filter constant is linked");}
  else b.scalar(s,value);
 }
 void vector(bNodeSocket &s,std::array<float,3> value){
  if(check){if(s.type!=SOCK_VECTOR||!s.default_value||!std::equal(value.begin(),value.end(),static_cast<const bNodeSocketValueVector *>(s.default_value)->value))throw std::invalid_argument("sRGB filter vector changed");for(const auto &l:b.tree.links)if(l.tosock==&s)throw std::invalid_argument("sRGB filter constant is linked");}
  else b.vector(s,value);
 }
 void edge(Source from,bNode &to,bNodeSocket &socket){
  auto &output=b.out(from);const int output_type=STREQ(from.socket,"Color")?SOCK_RGBA:STREQ(from.socket,"Vector")?SOCK_VECTOR:SOCK_FLOAT;
  const int input_type=STREQ(to.idname,"ShaderNodeMixRGB")?(&socket==to.inputs.first?SOCK_FLOAT:SOCK_RGBA):(STREQ(to.idname,"ShaderNodeMath")||STREQ(to.idname,"ShaderNodeCombineXYZ"))?SOCK_FLOAT:SOCK_VECTOR;
  if(output.type!=output_type||socket.type!=input_type||(output.flag&SOCK_UNAVAIL)||(socket.flag&SOCK_UNAVAIL))throw std::invalid_argument("sRGB filter socket type or availability changed");edges.emplace(&output,&socket);
  if(check){int count=0;for(const auto &l:b.tree.links)if(l.tosock==&socket){if(l.fromnode!=from.node||l.fromsock!=&output||l.tonode!=&to||!(l.flag&NODE_LINK_VALID)||(l.flag&NODE_LINK_MUTED))throw std::invalid_argument("sRGB filter edge changed");++count;}if(count!=1)throw std::invalid_argument("sRGB filter edge is missing");}
  else b.connect(from,to,socket);
 }
 Source constant(const std::string &name,float value){auto &n=node("ShaderNodeValue",name);scalar(b.out({&n,"Value"}),value);return {&n,"Value"};}
 Source math(const std::string &name,int operation,Source a,Source c){auto &n=node("ShaderNodeMath",name);op(n,operation);edge(a,n,b.in(n,0));edge(c,n,b.in(n,1));return {&n,"Value"};}
 Source vector(const std::string &name,int operation,Source a,std::array<float,3> value){auto &n=node("ShaderNodeVectorMath",name);op(n,operation);edge(a,n,b.in(n,0));vector(b.in(n,1),value);return {&n,"Vector"};}
 Source mix(const std::string &name,Source factor,Source a,Source c){auto &n=node("ShaderNodeMixRGB",name);op(n,MA_RAMP_BLEND);edge(factor,n,b.in(n,0));edge(a,n,b.in(n,1));edge(c,n,b.in(n,2));return {&n};}
 Source curve(int tap,Source sampled,const MaterialLayerSourceColorTransform &transform){
  const auto p="SrgbDecode"+std::to_string(tap);auto &split=node("ShaderNodeSeparateXYZ",p+"Split");edge(sampled,split,b.in(split,"Vector"));auto &combine=node("ShaderNodeCombineXYZ",p+"RGB");
  for(const char *component:{"X","Y","Z"}){
   const auto q=p+component;const Source x{&split,component};
   const auto toe=math(q+"Toe",NODE_MATH_MULTIPLY,x,constant(q+"Slope",float(transform.slope)));
   const auto offset=math(q+"Offset",NODE_MATH_ADD,x,constant(q+"OffsetValue",float(transform.offset)));
   const auto base=math(q+"Base",NODE_MATH_DIVIDE,offset,constant(q+"Divisor",float(1+transform.offset)));
   // Both GLSL branches may evaluate. Never feed negative AP1 or negative raw
   // values to fractional pow, including its inactive branch.
   const auto safe=math(q+"SafeBase",NODE_MATH_MAXIMUM,base,constant(q+"Zero",0));
   const auto power=math(q+"Power",NODE_MATH_POWER,safe,constant(q+"Gamma",float(transform.gamma)));
   const auto below=math(q+"Below",NODE_MATH_LESS_THAN,x,constant(q+"Break",float(transform.breakpoint)));
   const auto inverse=math(q+"Above",NODE_MATH_SUBTRACT,constant(q+"One",1),below);
   const auto low=math(q+"Low",NODE_MATH_MULTIPLY,below,toe),high=math(q+"High",NODE_MATH_MULTIPLY,inverse,power);
   const auto result=math(q+"Value",NODE_MATH_ADD,low,high);edge(result,combine,b.in(combine,component));
  }
  return {&combine,"Vector"};
 }
 Source build(Image *image=nullptr){
  auto &representation=node("ShaderNodeValue","SrgbRepresentation");scalar(b.out({&representation,"Value"}),encoded?1.f:0.f);
  if(check){bool old_encoded=false;const auto recorded=srgb_filter_source(b.tree,prefix,old_encoded);if(old_encoded!=encoded||srgb_filter_source_json(*recorded)!=srgb_filter_source_json(source))throw std::invalid_argument("sRGB graph source differs from its typed binding");}
  else{IDPropertyTemplate empty{};if(!representation.prop)representation.prop=IDP_New(IDP_GROUP,&empty,"sRGB source provenance");const auto json=QJsonDocument(srgb_filter_source_json(source)).toJson(QJsonDocument::Compact);IDP_ReplaceInGroup(representation.prop,IDP_NewString(json.constData(),"source"));b.changed=true;}
  const auto transform=material_layer_source_color_transform(source);if(!transform.srgb||source.width<1||source.height<1)throw std::invalid_argument("sRGB filter needs its verified byte source");
  auto *mapping=bke::node_find_node_by_name(b.tree,prefix+"Mapping");if(!mapping||!STREQ(mapping->idname,"ShaderNodeMapping")||(mapping->flag&NODE_MUTED))throw std::invalid_argument("sRGB filter mapping is missing");
  const auto pixel=vector("SrgbPixels",NODE_VECTOR_MATH_MULTIPLY,{mapping,"Vector"},{float(source.width),float(source.height),0});
  const auto centered=vector("SrgbCentered",NODE_VECTOR_MATH_ADD,pixel,{-.5f,-.5f,0});
  const auto floor=vector("SrgbFloor",NODE_VECTOR_MATH_FLOOR,centered,{0,0,0});
  const auto fraction=vector("SrgbFraction",NODE_VECTOR_MATH_FRACTION,centered,{0,0,0});auto &xy=node("ShaderNodeSeparateXYZ","SrgbFractionXY");edge(fraction,xy,b.in(xy,"Vector"));
  std::array<Source,4> samples;ID *identity=nullptr;
  for(int i=0;i<4;++i){const auto center=vector("SrgbCenter"+std::to_string(i),NODE_VECTOR_MATH_ADD,floor,{i%2?1.5f:.5f,i/2?1.5f:.5f,0});const auto uv=vector("SrgbUv"+std::to_string(i),NODE_VECTOR_MATH_MULTIPLY,center,{1.f/source.width,1.f/source.height,0});
   auto &n=node("ShaderNodeTexImage",i?"ColorTap"+std::to_string(i):"Color");
   if(!n.storage)throw std::invalid_argument("sRGB image settings missing");auto &settings=*static_cast<NodeTexImage *>(n.storage);
   if(check){if(settings.extension!=SHD_IMAGE_EXTENSION_REPEAT||settings.interpolation!=SHD_INTERP_CLOSEST||settings.projection!=SHD_PROJ_FLAT||!n.id)throw std::invalid_argument("sRGB closest sampler changed");}
   else{if(!image)throw std::invalid_argument("sRGB image missing");settings.extension=SHD_IMAGE_EXTENSION_REPEAT;settings.interpolation=SHD_INTERP_CLOSEST;settings.projection=SHD_PROJ_FLAT;if(n.id!=&image->id){if(n.id)id_us_min(n.id);n.id=&image->id;id_us_plus(n.id);b.changed=true;}BKE_ntree_update_tag_node_property(&b.tree,&n);}
   const auto &user=settings.iuser;const auto &m=settings.base.tex_mapping;
   if(user.scene||user.frames!=100||user.sfra!=1||user.offset||user.cycl||user.multiview_eye||user.pass||user.multi_index||user.view||user.layer||(user.tile!=0&&user.tile!=1001)||(user.flag&~(IMA_NEED_FRAME_RECALC|IMA_USER_FRAME_IN_RANGE))||m.ob||m.type!=TEXMAP_TYPE_POINT||m.projx!=PROJ_X||m.projy!=PROJ_Y||m.projz!=PROJ_Z||m.mapping!=MTEX_FLAT||(m.flag&~TEXMAP_UNIT_MATRIX))throw std::invalid_argument("sRGB image context or hidden mapping changed");
   for(int c=0;c<3;++c)if(m.loc[c]!=0||m.rot[c]!=0||m.size[c]!=1)throw std::invalid_argument("sRGB hidden SRT changed");for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(m.mat[c][r]!=(c==r?1.f:0.f))throw std::invalid_argument("sRGB hidden matrix changed");
   if(identity&&n.id!=identity)throw std::invalid_argument("sRGB taps do not share one Image");identity=n.id;edge(uv,n,b.in(n,"Vector"));samples[i]=encoded?curve(i,{&n},transform):Source{&n};
  }
  const auto low=mix("SrgbMixLow",{&xy,"X"},samples[0],samples[1]),high=mix("SrgbMixHigh",{&xy,"X"},samples[2],samples[3]);
  return mix("SrgbMixed",{&xy,"Y"},low,high);
 }
 void audit(){
  auto *final=bke::node_find_node_by_name(b.tree,prefix+"SrgbMixed");
  for(const auto &n:b.tree.nodes){const std::string name=n.name;if((name.starts_with(prefix+"Srgb")||name.starts_with(prefix+"ColorTap"))&&!nodes.contains(const_cast<bNode *>(&n)))throw std::invalid_argument("Unknown sRGB filter node cannot be replaced");}
  for(const auto &l:b.tree.links)if(nodes.contains(l.fromnode)||nodes.contains(l.tonode)){
   if(edges.contains({l.fromsock,l.tosock}))continue;
   bool adapter=false;if(l.fromnode==final&&STREQ(l.fromsock->identifier,"Color"))for(int r=0;r<3;++r){auto *dot=bke::node_find_node_by_name(b.tree,prefix+"ColorToScene"+std::to_string(r));if(l.tonode==dot&&l.tosock==dot->inputs.first)adapter=true;}
   if(!adapter)throw std::invalid_argument("sRGB filter has an unowned use");
  }
 }
 void remove_curves(Main &main){for(auto *node:nodes)if(std::string(node->name).starts_with(prefix+"SrgbDecode")){bke::node_remove_node(&main,b.tree,*node,true);b.changed=true;}}
 void remove(Main &main){for(auto *node:nodes)bke::node_remove_node(&main,b.tree,*node,true);b.changed=true;}
};
