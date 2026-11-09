#include "OpenPbrMaterialX.hh"
#include "ResourceAvailability.hh"
#include "OpenPbrReferenceData.hh"
#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>
#include <QDir>
#include <QXmlStreamWriter>
#include <functional>
#include <tuple>
namespace bts {
namespace {
namespace mx = MaterialX;
std::string number(float value)
{
  return QString::number(double(value), 'g', std::numeric_limits<float>::max_digits10)
      .toStdString();
}
const char *space_name(OpenPbrImageSpace value)
{
  switch (value) {
    case OpenPbrImageSpace::Raw:
      return "raw";
    case OpenPbrImageSpace::AcesCg:
      return "acescg";
    case OpenPbrImageSpace::Srgb:
      return "srgb_texture";
    case OpenPbrImageSpace::LinearRec709:
      return "lin_rec709";
  }
  throw std::invalid_argument("Unknown MaterialX image color space");
}
void filename(const QString &path)
{
  if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains('\\') || path.contains(':') ||
      QString::fromUtf8(path.toUtf8()) != path)
    throw std::invalid_argument("MaterialX images require portable relative filenames");
  for (auto c : path)
    if (c.unicode() < 32 || c.unicode() == 127)
      throw std::invalid_argument("Invalid control character in MaterialX filename");
  for (const auto &part : path.split('/'))
    if (part.isEmpty() || part == "." || part == "..")
      throw std::invalid_argument("MaterialX image path must stay inside the package");
}
mx::InputPtr literal(mx::NodePtr node,
                     const std::string &name,
                     const std::string &type,
                     const std::string &value)
{
  auto input = node->addInput(name, type);
  input->setValueString(value);
  return input;
}
mx::InputPtr connected(mx::NodePtr node,
                       const std::string &name,
                       const std::string &type,
                       const std::string &source)
{
  auto input = node->getInput(name);
  if (!input)
    input = node->addInput(name, type);
  input->removeAttribute("value");
  input->setNodeName(source);
  return input;
}
}  // namespace
QByteArray openpbr_materialx_license()
{
  return QByteArray(openpbr_reference::license, int(sizeof(openpbr_reference::license) - 1));
}
QByteArray openpbr_materialx_document(const OpenPbrMaterialX &request)
{
  request.parameters.validate();
  if (request.material_name.isEmpty() || request.material_name.contains(QChar(0)) ||
      QString::fromUtf8(request.material_name.toUtf8()) != request.material_name)
    throw std::invalid_argument("Invalid OpenPBR material label");
  if (!std::isfinite(request.bump_scale) || request.bump_scale < 0 ||
      !std::isfinite(request.displacement_scale) || request.displacement_scale < 0 ||
      !std::isfinite(request.displacement_midlevel) || request.displacement_midlevel < 0 ||
      request.displacement_midlevel > 1 || request.tiles.empty() ||
      *request.tiles.begin() < 1001 || *request.tiles.rbegin() > 2000)
    throw std::invalid_argument("Invalid OpenPBR material geometry settings");
  // Blender image tiles use 1001..2000. MaterialX stores the admitted set as
  // strings; its count is limited by current RAM, not an invented 256 tiles.
  auto required = resource_sum(
      resource_product(request.material_name.size(), 16),
      resource_sum(resource_product(sizeof(openpbr_reference::xml), 16),
                   resource_product(request.tiles.size(), 4 * (sizeof(std::string) + 32))));
  for (const auto &[id, image] : request.images)
    required = resource_sum(required,
                            resource_sum(resource_product(image.filename.size(), 8), 1024));

  for (const auto &[id, image] : request.images) {
    filename(image.filename);
    const bool scalar = id == "bump" || id == "displacement";
    const auto type = scalar ? OpenPbrType::Number :
                               openpbr_parameters[OpenPbrParameters::index(id)].type;
    if (type == OpenPbrType::Boolean)
      throw std::invalid_argument("Boolean OpenPBR parameters cannot use image maps");
    space_name(image.space);
    if ((type == OpenPbrType::Number) != (image.space == OpenPbrImageSpace::Raw))
      throw std::invalid_argument(
          "Scalar maps must be raw; color maps need an explicit color space");
  }
  auto doc = mx::createDocument();
  mx::readFromXmlString(doc, openpbr_reference::xml);
  auto definition = doc->getNodeDef("ND_open_pbr_surface_surfaceshader");
  auto graph = doc->getNodeGraph("NG_open_pbr_surface_surfaceshader");
  if (!definition || !graph || definition->getVersionString() != "1.1.1")
    throw std::runtime_error("Pinned OpenPBR reference is unavailable");
  definition->setName("ND_BTS_OpenPBR_1_1_1");
  graph->setName("NG_BTS_OpenPBR_1_1_1");
  graph->setNodeDefString(definition->getName());
  doc->setColorSpace("acescg");
  doc->setVersionString("1.39");
  doc->setAttribute(
      "doc",
      "ASWF OpenPBR 1.1.1, revision f8d6d947dfae4c9b599965a86c22826ea7a8dbfb. See OpenPBR-LICENSE.txt. Reference graph names are made package-local; the viewport Principled approximation is not this BSDF.");
  auto surface = doc->addNode("open_pbr_surface", "StudioSurface", "surfaceshader");
  surface->setNodeDefString(definition->getName());
  surface->setVersionString("1.1.1");
  for (std::size_t i = 0; i < openpbr_parameters.size(); ++i) {
    const auto &p = openpbr_parameters[i];
    const auto &value = request.parameters.values[i];
    std::string text = number(value[0]);
    const char *type = "float";
    if (p.type == OpenPbrType::Color) {
      type = "color3";
      text += ", " + number(value[1]) + ", " + number(value[2]);
    }
    else if (p.type == OpenPbrType::Boolean) {
      type = "boolean";
      text = value[0] ? "true" : "false";
    }
    literal(surface, p.id, type, text);
  }
  using Key = std::tuple<QString, std::string, int>;
  std::map<Key, std::string> shared;
  std::map<std::string, std::string> images;
  for (const auto &[id, binding] : request.images) {
    const bool color = id != "bump" && id != "displacement" &&
                       openpbr_parameters[OpenPbrParameters::index(id)].type == OpenPbrType::Color;
    const std::string type = color ? "color3" : "float";
    Key key{binding.filename, type, int(binding.space)};
    auto found = shared.find(key);
    std::string image_name;
    if (found != shared.end())
      image_name = found->second;
    else {
      image_name = "StudioImage_" + std::to_string(shared.size());
      auto image = doc->addNode("image", image_name, type);
      image->setColorSpace(space_name(binding.space));
      literal(image, "file", "filename", binding.filename.toUtf8().toStdString());
      shared.emplace(std::move(key), image_name);
    }
    images[id] = image_name;
    if (id != "bump" && id != "displacement")
      connected(surface, id, type, image_name);
  }
  if (images.contains("bump")) {
    auto bump = doc->addNode("bump", "StudioBump", "vector3");
    connected(bump, "height", "float", images.at("bump"));
    literal(bump, "scale", "float", number(request.bump_scale));
    connected(surface, "geometry_normal", "vector3", bump->getName());
  }
  auto material = doc->addNode("surfacematerial", "StudioMaterial", "material");
  material->setAttribute("uiname", request.material_name.toUtf8().toStdString());
  connected(material, "surfaceshader", "surfaceshader", surface->getName());
  if (images.contains("displacement")) {
    auto height = doc->addNode("subtract", "StudioDisplacementHeight", "float");
    connected(height, "in1", "float", images.at("displacement"));
    literal(height, "in2", "float", number(request.displacement_midlevel));
    auto displacement = doc->addNode("displacement", "StudioDisplacement", "displacementshader");
    connected(displacement, "displacement", "float", height->getName());
    literal(displacement, "scale", "float", number(request.displacement_scale));
    if (request.displacement_enabled)
      connected(material, "displacementshader", "displacementshader", displacement->getName());
  }
  mx::StringVec tiles;
  for (int tile : request.tiles)
    tiles.push_back(std::to_string(tile));
  doc->addGeomInfo("StudioGeometry")->setGeomPropValue<mx::StringVec>("udimset", tiles);
  // MaterialX 1.39.4 writes raw attribute values (including <UDIM> and &).
  // Serialize its validated element tree with a standards-compliant XML writer.
  QByteArray bytes;
  QXmlStreamWriter xml(&bytes);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  const std::function<void(mx::ConstElementPtr)> element = [&](mx::ConstElementPtr node) {
    xml.writeStartElement(QString::fromStdString(node->getCategory()));
    if (!node->getName().empty())
      xml.writeAttribute("name", QString::fromStdString(node->getName()));
    for (const auto &name : node->getAttributeNames())
      xml.writeAttribute(QString::fromStdString(name),
                         QString::fromStdString(node->getAttribute(name)));
    for (const auto &child : node->getChildren())
      element(child);
    xml.writeEndElement();
  };
  element(doc);
  xml.writeEndDocument();
  if (xml.hasError())
    throw std::runtime_error("Cannot serialize OpenPBR XML");
  return bytes;
}
}  // namespace bts
