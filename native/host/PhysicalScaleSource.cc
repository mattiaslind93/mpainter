#include "ResourceAvailability.hh"
#include "PhysicalScale.hh"
#include "UsdScene.hh"
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <stdexcept>
namespace bts {
SourceUnits inspect_source_units(const std::string &path){
  const auto name=QString::fromStdString(path);const auto suffix=QFileInfo(name).suffix().toLower();double meters=1;
  if(suffix=="obj"||suffix=="abc")return {};
  if(suffix=="gltf"||suffix=="glb"||suffix=="fbx")return {1,100}; // Importers normalize coordinates to meters.
  if(suffix=="blend")return {1,100}; // Append preserves native coordinates, not source scene display units.
  if(is_usd_scene(path))meters=inspect_usd_scene(path,{}).meters_per_unit;
  else if(suffix=="dae"){
    QFile file(name);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read Collada unit metadata");QXmlStreamReader xml(&file);bool asset=false;
    while(!xml.atEnd()){xml.readNext();if(xml.isStartElement()){if(xml.name()==u"asset")asset=true;else if(asset&&xml.name()==u"unit"){const auto value=xml.attributes().value("meter");if(!value.isEmpty()){bool valid=false;meters=value.toDouble(&valid);if(!valid)throw std::runtime_error("Invalid Collada meter scale");}break;}}else if(xml.isEndElement()&&xml.name()==u"asset")break;}
    if(xml.hasError())throw std::runtime_error("Malformed Collada unit metadata");
  }else throw std::invalid_argument("Unit metadata requires OBJ, Blender, Alembic, FBX, glTF, Collada or USD");
  SourceUnits result{meters,100*meters};validate_units({},result);return result;
}
}
