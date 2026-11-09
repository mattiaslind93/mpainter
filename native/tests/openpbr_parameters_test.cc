// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenPbrParameters.hh"
#include <QCoreApplication>
#include <QJsonDocument>
#include <cassert>
#include <cstdio>
#include <limits>
#include <set>
using namespace bts;
template<class F>void rejected(F edit){bool failed=false;try{edit();}catch(const std::invalid_argument &){failed=true;}assert(failed);}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);OpenPbrParameters p;p.validate();
 assert(openpbr_parameters.size()==37);std::set<QString> identifiers,groups;
 for(const auto &entry:openpbr_parameters){assert(identifiers.insert(entry.id).second);groups.insert(entry.group);assert(!QString(entry.id).contains("normal"));}
 assert(groups.size()==9);
 assert(p.get("base_color")==OpenPbrParameters::Value({.8f,.8f,.8f}));
 assert(p.get("specular_ior")[0]==1.5f&&p.get("coat_ior")[0]==1.6f);
 assert(p.get("coat_darkening")[0]==1&&p.get("subsurface_radius_scale")==OpenPbrParameters::Value({1,.5f,.25f}));
 assert(openpbr_thin_film_nanometers(p)==500);
 const auto initial=p;
 for(const auto &name:{"specular_ior","coat_ior","thin_film_ior","transmission_dispersion_abbe_number"})rejected([&]{p.set(name,{0,0,0});});
 rejected([&]{p.set("base_weight",{1.01f,0,0});});rejected([&]{p.set("base_color",{0,0,-.1f});});
 rejected([&]{p.set("geometry_thin_walled",{.5f,0,0});});rejected([&]{p.set("specular_ior",{1.5f,1,0});});
 rejected([&]{p.set("base_roughness",{.5f,0,0});});
 for(float invalid:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})rejected([&]{p.set("emission_luminance",{invalid,0,0});});
 assert(p==initial); // Rejected changes are atomic.
 p.set("transmission_scatter_anisotropy",{-1,0,0});p.set("subsurface_scatter_anisotropy",{1,0,0});
 p.set("geometry_thin_walled",{1,0,0});p.set("emission_color",{10,100,1000});p.set("emission_luminance",{100000,0,0});
 const auto bytes=QJsonDocument(p.to_json()).toJson(QJsonDocument::Compact);
 assert(OpenPbrParameters::from_json(QJsonDocument::fromJson(bytes).object())==p);
 auto bad=p.to_json();bad["specification"]="1.2";rejected([&]{OpenPbrParameters::from_json(bad);});
 bad=p.to_json();bad["version"]=1.5;rejected([&]{OpenPbrParameters::from_json(bad);});
 bad=p.to_json();bad["extra"]=true;rejected([&]{OpenPbrParameters::from_json(bad);});
 for(const auto &entry:openpbr_parameters){
  bad=p.to_json();auto values=bad["values"].toObject();values.remove(entry.id);bad["values"]=values;rejected([&]{OpenPbrParameters::from_json(bad);});
  bad=p.to_json();values=bad["values"].toObject();values[entry.id]=QString("0.5");bad["values"]=values;rejected([&]{OpenPbrParameters::from_json(bad);});
 }
 bad=p.to_json();auto values=bad["values"].toObject();values["base_color"]=QJsonArray{0,0,0,1};bad["values"]=values;rejected([&]{OpenPbrParameters::from_json(bad);});
 values["base_color"]=QJsonArray{0,0,0};values["geometry_thin_walled"]=1;bad["values"]=values;rejected([&]{OpenPbrParameters::from_json(bad);});
 assert(QString(openpbr_export_channel(0))=="base_color"&&QString(openpbr_export_channel(2))=="specular_roughness");
 assert(QString(openpbr_export_channel(4))=="bump"&&QString(openpbr_export_channel(14))=="displacement"&&QString(openpbr_export_channel(3)).isEmpty());
 std::puts("OpenPBR 1.1.1: 37 typed parameters/9 groups, specified defaults, strict finite bounds/positive IOR, atomic invalid edits, HDR/anisotropy/bool JSON, units and hidden-normal export policy PASS; data model only, no BSDF conformance claim");
}
