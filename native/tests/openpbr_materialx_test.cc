// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenPbrMaterialX.hh"
#include "ResourceAvailability.hh"
#include "OpenPbrReferenceData.hh"
#include <QCryptographicHash>
#include <MaterialXCore/Document.h>
#include <MaterialXFormat/Util.h>
#include <MaterialXFormat/XmlIo.h>
#include <QCoreApplication>
#include <QLocale>
#include <QXmlStreamReader>
#include <cassert>
#include <clocale>
#include <cstdio>
namespace mx=MaterialX;
using namespace bts;
template<class F>void rejected(F edit){bool failed=false;try{edit();}catch(const std::invalid_argument &){failed=true;}assert(failed);}
int main(int argc,char **argv){QCoreApplication app(argc,argv);assert(QCryptographicHash::hash(QByteArray(openpbr_reference::xml,int(sizeof(openpbr_reference::xml)-1)),QCryptographicHash::Sha256).toHex()=="c15674aaa82ef4bf0a27388b3b3a8f0f171982f53df6ebbceed679ce63699f51");assert(QCryptographicHash::hash(openpbr_materialx_license(),QCryptographicHash::Sha256).toHex()=="c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4");OpenPbrMaterialX request;request.material_name="Copper & Ice \"A\"";request.tiles={1001,1002};request.parameters.set("geometry_thin_walled",{1,0,0});request.parameters.set("thin_film_weight",{.4f,0,0});request.parameters.set("transmission_dispersion_scale",{.5f,0,0});request.parameters.set("emission_color",{2,3,4});request.displacement_midlevel=0;request.displacement_scale=1;
 request.images={{"base_color",{"textures/A & B_color.<UDIM>.exr",OpenPbrImageSpace::AcesCg}},{"base_metalness",{"textures/control.<UDIM>.png",OpenPbrImageSpace::Raw}},{"specular_roughness",{"textures/control.<UDIM>.png",OpenPbrImageSpace::Raw}},{"bump",{"textures/scalar_bump.<UDIM>.exr",OpenPbrImageSpace::Raw}},{"displacement",{"textures/scalar_displacement.<UDIM>.exr",OpenPbrImageSpace::Raw}}};
 const auto original=request.parameters;const auto *comma=std::setlocale(LC_NUMERIC,"sv_SE.UTF-8");const auto bytes=openpbr_materialx_document(request);if(comma){assert(bytes.contains("value=\"1.5\""));assert(!bytes.contains("value=\"1,5\""));}std::setlocale(LC_NUMERIC,"C");assert(request.parameters==original);
 QXmlStreamReader standard_xml(bytes);while(!standard_xml.atEnd())standard_xml.readNext();assert(!standard_xml.hasError());assert(bytes.contains("&lt;UDIM&gt;")&&bytes.contains("A &amp; B")&&bytes.contains("&quot;A&quot;"));
 auto document=mx::createDocument();mx::readFromXmlString(document,bytes.toStdString());auto library=mx::createDocument();mx::loadLibraries({"libraries"},mx::FileSearchPath(BTS_MATERIALX_BASE),library);document->importLibrary(library);std::string message;if(!document->validate(&message)){std::fprintf(stderr,"MaterialX validation: %s\n",message.c_str());return 1;}
 auto surface=document->getNode("StudioSurface");assert(surface&&surface->getNodeDef()&&surface->getNodeDef()->getName()=="ND_BTS_OpenPBR_1_1_1"&&surface->getNodeDef()->getVersionString()=="1.1.1");
 assert(surface->getNodeDef()->getImplementation()&&surface->getNodeDef()->getImplementation()->getName()=="NG_BTS_OpenPBR_1_1_1");
 for(const auto &p:openpbr_parameters)assert(surface->getInput(p.id));assert(surface->getInput("geometry_thin_walled")->getValueString()=="true");assert(surface->getInput("thin_film_weight")->getValue()->asA<float>()==.4f);assert(surface->getInput("transmission_dispersion_scale")->getValue()->asA<float>()==.5f);
 auto image=surface->getInput("base_color")->getConnectedNode();assert(image&&image->getColorSpace()=="acescg"&&image->getInput("file")->getValueString()=="textures/A & B_color.<UDIM>.exr");
 auto metal=surface->getInput("base_metalness")->getConnectedNode(),roughness=surface->getInput("specular_roughness")->getConnectedNode();assert(metal==roughness&&metal->getType()=="float"&&metal->getColorSpace()=="raw");
 auto bump=surface->getInput("geometry_normal")->getConnectedNode();assert(bump&&bump->getCategory()=="bump"&&bump->getInput("height")->getConnectedNode()->getType()=="float");
 auto material=document->getNode("StudioMaterial");assert(material->getAttribute("uiname")==request.material_name.toStdString());auto displacement=material->getInput("displacementshader")->getConnectedNode();assert(displacement&&displacement->getCategory()=="displacement"&&displacement->getInput("scale")->getValue()->asA<float>()==1);auto height=displacement->getInput("displacement")->getConnectedNode();assert(height->getCategory()=="subtract"&&height->getInput("in2")->getValue()->asA<float>()==0&&height->getInput("in1")->getConnectedNode()->getColorSpace()=="raw");
 int images=0;for(auto node:document->getNodes())if(node->getCategory()=="image"){++images;assert(node->getInput("file")->getValueString().find("_normal.")==std::string::npos);}assert(images==4);assert(openpbr_materialx_license().contains("Apache License")&&openpbr_materialx_license().contains("Version 2.0"));
 auto invalid=request;invalid.images["geometry_normal"]={"textures/no_normal.exr",OpenPbrImageSpace::Raw};rejected([&]{openpbr_materialx_document(invalid);});invalid=request;invalid.images["geometry_thin_walled"]={"textures/bool.png",OpenPbrImageSpace::Raw};rejected([&]{openpbr_materialx_document(invalid);});
 for(const auto &path:{QString("../outside.exr"),QString("/absolute.exr"),QString("C:\\absolute.exr"),QString("a//b.exr"),QString("a/./b.exr"),QString("a")+QChar(0)+".exr"}){invalid=request;invalid.images["bump"].filename=path;rejected([&]{openpbr_materialx_document(invalid);});}
 invalid=request;invalid.images["specular_roughness"].space=OpenPbrImageSpace::Srgb;rejected([&]{openpbr_materialx_document(invalid);});invalid=request;invalid.images["base_color"].space=OpenPbrImageSpace::Raw;rejected([&]{openpbr_materialx_document(invalid);});
 request.displacement_enabled=false;auto disabled=mx::createDocument();mx::readFromXmlString(disabled,openpbr_materialx_document(request).toStdString());assert(!disabled->getNode("StudioMaterial")->getInput("displacementshader")&&disabled->getNode("StudioDisplacement")->getInput("scale")->getValue()->asA<float>()==1);
 // XML labels and relative file metadata have no host-imposed length cap.
 {auto named=request;named.material_name=QString(5000,'M');named.images["bump"].filename="textures/"+QString(10000,'b')+".exr";const auto xml_bytes=openpbr_materialx_document(named);auto text_doc=mx::createDocument();mx::readFromXmlString(text_doc,xml_bytes.toStdString());assert(text_doc->getNode("StudioMaterial")->getAttribute("uiname")==named.material_name.toStdString());bool filename_retained=false;for(const auto &node:text_doc->getNodes())if(node->getCategory()=="image"&&node->getInput("file")->getValueString()==named.images["bump"].filename.toStdString())filename_retained=true;assert(filename_retained);}
 // Count admission follows native tile IDs and RAM, rather than 256 entries.
 OpenPbrMaterialX large;large.tiles.clear();for(int tile=1001;tile<=2000;++tile)large.tiles.insert(tile);
 const auto large_bytes=openpbr_materialx_document(large);auto large_doc=mx::createDocument();mx::readFromXmlString(large_doc,large_bytes.toStdString());const auto tile_values=large_doc->getGeomInfo("StudioGeometry")->getGeomProp("udimset")->getValue()->asA<mx::StringVec>();assert(tile_values.size()==1000&&tile_values.front()=="1001"&&tile_values.back()=="2000");
 for(int outside:{1000,2001}){auto bad=large;bad.tiles.insert(outside);rejected([&]{openpbr_materialx_document(bad);});}
 {TextureRamQueryScope low_memory(+[]()->std::uint64_t{return 1024;});bool denied=false;try{openpbr_materialx_document(large);}catch(const std::length_error &){denied=true;}assert(!denied);}
 std::puts("OpenPBR MaterialX 1.1.1: official 1.39.4 semantic validation, all 37 parameters including full-model fields, locale-safe floats, shared image bindings, explicit color spaces/raw scalars, UDIM, bump without normal files, signed displacement subtraction/enable, XML escaping, relative paths and bundled license PASS; metadata transport only");}
