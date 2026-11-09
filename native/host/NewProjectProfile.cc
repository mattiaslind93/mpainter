// SPDX-License-Identifier: GPL-3.0-or-later
#include "NewProjectProfile.hh"
#include <cmath>
#include <stdexcept>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include "BKE_main.hh"
#include "BKE_idprop.hh"
#include "BKE_context.hh"
#include "BKE_studiolight.h"
#include "BLI_string.h"
#include "BLI_listbase.h"
#include "BLI_colorspace.hh"
#include "DNA_scene_types.h"
#include "DNA_view3d_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include "RNA_access.hh"
#include "RNA_prototypes.hh"
namespace bts {
using namespace blender;
namespace {
const QJsonObject &profile()
{
  static const auto result = QJsonDocument::fromJson(
#include "NewProjectProfile.inc"
  ).object();
  return result;
}
QJsonObject settings(bContext *context, PointerRNA pointer, const QJsonObject &values, bool write, bool validate)
{
  QJsonObject result;
  for (auto it = values.begin(); it != values.end(); ++it) {
    const QByteArray name = it.key().toUtf8();
    auto *property = RNA_struct_find_property(&pointer, name.constData());
    if (!property || RNA_property_array_length(&pointer, property)) {
      throw std::runtime_error("Missing scalar native profile property: " + name.toStdString());
    }
    const auto value = it.value();
    const auto fail = [&] { throw std::runtime_error("Invalid native profile value: " + name.toStdString()); };
    switch (RNA_property_type(property)) {
      case PROP_BOOLEAN:
        if (!value.isBool()) fail();
        if (write) RNA_property_boolean_set(&pointer, property, value.toBool());
        result[it.key()] = RNA_property_boolean_get(&pointer, property);
        break;
      case PROP_INT: {
        int low, high; RNA_property_int_range(&pointer, property, &low, &high);
        if (!value.isDouble() || value.toDouble() != value.toInt() || value.toInt() < low || value.toInt() > high) fail();
        if (write) RNA_property_int_set(&pointer, property, value.toInt());
        result[it.key()] = RNA_property_int_get(&pointer, property);
        break;
      }
      case PROP_FLOAT: {
        float low, high; RNA_property_float_range(&pointer, property, &low, &high);
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < low || value.toDouble() > high) fail();
        if (write) RNA_property_float_set(&pointer, property, float(value.toDouble()));
        result[it.key()] = RNA_property_float_get(&pointer, property);
        break;
      }
      case PROP_ENUM: {
        int enum_value;
        const auto text = value.toString().toUtf8();
        if (!value.isString() || !RNA_property_enum_value(context, &pointer, property, text.constData(), &enum_value)) fail();
        if (write) RNA_property_enum_set(&pointer, property, enum_value);
        const char *identifier = nullptr;
        if (!RNA_property_enum_identifier(context, &pointer, property, RNA_property_enum_get(&pointer, property), &identifier)) fail();
        result[it.key()] = identifier;
        break;
      }
      default: fail();
    }
    if (write && validate && result[it.key()] != value) {
      // JSON doubles represent extracted native float values exactly.
      fail();
    }
  }
  return result;
}
QJsonObject native_settings(Scene &scene, View3D &view, bool write, bool preparing = false)
{
  std::unique_ptr<bContext, decltype(&CTX_free)> context(CTX_create(), CTX_free);
  CTX_data_scene_set(context.get(), &scene);
  auto scene_pointer = RNA_id_pointer_create(&scene.id);
  auto eevee = RNA_pointer_get(&scene_pointer, "eevee");
  auto rays = RNA_pointer_get(&eevee, "ray_tracing_options");
  auto shading = RNA_pointer_create_discrete(&scene.id, RNA_View3DShading, &view.shading);
  ColorManagedViewSettings display_copy = scene.view_settings;
  auto display = preparing ? RNA_pointer_create_discrete(&scene.id, RNA_ColorManagedViewSettings, &display_copy) :
                              RNA_pointer_get(&scene_pointer, "view_settings");
  QJsonObject result;
  for (const auto &[name, pointer] : {std::pair{"eevee", eevee}, {"ray_tracing_options", rays},
                                     {"shading", shading}}) {
    result[name] = settings(context.get(), pointer, profile()[name].toObject(), write, true);
  }
  // Look membership depends on the view transform. Validate on a local settings copy during
  // preparation, then apply in dependency order after committing the new native scene.
  if (write || preparing) settings(context.get(), display, {{"view_transform", profile()["display"].toObject()["view_transform"]}}, true, true);
  result["display"] = settings(context.get(), display, profile()["display"].toObject(), write || preparing, true);
  return result;
}
}
struct NewProjectProfile::Impl {
  StudioLight *light = nullptr;
  bool owned = false;
  ~Impl() { if (owned && light) BKE_studiolight_remove(light); }
};
NewProjectProfile::NewProjectProfile(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
NewProjectProfile::~NewProjectProfile() = default;
std::unique_ptr<NewProjectProfile> NewProjectProfile::prepare(const std::string &path, Scene &scene, View3D &view)
{
  native_settings(scene, view, false, true); // Validate every name/type/range/enum before changing Main.
  if (IMB_colormanagement_working_space_get_named_index("ACEScg") < 0) {
    throw std::runtime_error("The active native OCIO config does not offer ACEScg");
  }
  QFile file(QString::fromStdString(path));
  if (!file.open(QIODevice::ReadOnly) || file.size()<=0)throw std::runtime_error("Select a readable HDR environment");
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if(!hash.addData(&file))throw std::runtime_error("Cannot read environment");
  const QFileInfo info(file);
  ImBuf *image = IMB_load_image_from_filepath(path.c_str(), ImBufFlags::AlphaIgnore);
  const bool decoded = image && image->x > 0 && image->y > 0 && image->float_buffer.data;
  if (image) IMB_freeImBuf(image);
  if (!decoded) throw std::runtime_error("The selected environment failed native HDR decoding");
  auto impl = std::make_unique<Impl>();
  for (StudioLight &light : BKE_studiolight_listbase()) {
    if (info.fileName() == light.name) {
      QFile existing(QString::fromUtf8(light.filepath));
      if (!(light.flag & STUDIOLIGHT_TYPE_WORLD) || !existing.open(QIODevice::ReadOnly) ||
          existing.size()!=file.size() || QCryptographicHash::hash(existing.readAll(),QCryptographicHash::Sha256).toHex()!=hash.result().toHex()) {
        throw std::runtime_error("A different Studio Light uses the selected environment name");
      }
      impl->light = &light;
    }
  }
  if (!impl->light) {
    impl->light = BKE_studiolight_load(path.c_str(), STUDIOLIGHT_TYPE_WORLD);
    impl->owned = true;
  }
  if (!impl->light) throw std::runtime_error("Native Studio Light registration failed");
  return std::unique_ptr<NewProjectProfile>(new NewProjectProfile(std::move(impl)));
}
void NewProjectProfile::initialize_working_space(Main &main)
{
  if (IMB_colormanagement_working_space_get_named_index("ACEScg") < 0) {
    throw std::runtime_error("The active native OCIO config does not offer ACEScg");
  }
  IMB_colormanagement_working_space_set_from_name("ACEScg"); // Returns whether it changed.
  if (!STREQ(IMB_colormanagement_working_space_get(), "ACEScg")) {
    throw std::runtime_error("Native ACEScg activation failed");
  }
  IMB_colormanagement_working_space_convert(&main, main.colorspace.scene_linear_to_xyz,
                                            colorspace::xyz_to_scene_linear, true);
  STRNCPY(main.colorspace.scene_linear_name, "ACEScg");
  main.colorspace.scene_linear_to_xyz = colorspace::scene_linear_to_xyz;
}
void NewProjectProfile::apply(Main &main, Scene &scene, View3D &view)
{
  initialize_working_space(main);
  STRNCPY(scene.r.engine, "BLENDER_EEVEE");
  native_settings(scene, view, true);
  // Product default: camera-relative Material Preview lighting (Mattias 2026-10-19).
  view.shading.flag|=V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION;
  STRNCPY(view.shading.lookdev_light, impl_->light->name);
  IDP_ReplaceInGroup(IDP_EnsureProperties(&scene.id),
                    bke::idprop::create("mpainter_environment_orientation", 1).release());
  impl_->owned = false; // Registry now owns the resource used by the accepted document.
}
void NewProjectProfile::restore_environment_orientation(Scene &scene, View3D &view)
{
  auto *properties=IDP_EnsureProperties(&scene.id);
  if(IDP_GetPropertyFromGroup(properties,"mpainter_environment_orientation"))return;
  // Earlier mPainter Studio projects inherited 0 instead of mfPainter's +pi/2.
  // Do not reset a user's non-default angle, custom HDR, or World/Local choice.
  auto *hash=IDP_GetPropertyFromGroup(properties,"bts_environment_sha256");
  if(STREQ(view.shading.lookdev_light,"default-studio.hdr") && hash && hash->type==IDP_STRING &&
     STREQ(IDP_string_get(hash),"48505ffc80c0ca026775c77621b5a85a4c44b0fb7819ddadfa9e7bc6f5732031") &&
     view.shading.studiolight_rot_z==0.0f)
    view.shading.studiolight_rot_z=float(profile()["shading"].toObject()["studiolight_rotate_z"].toDouble());
  IDP_ReplaceInGroup(properties,bke::idprop::create("mpainter_environment_orientation",1).release());
}
QJsonObject NewProjectProfile::inspect(Main &main, Scene &scene, View3D &view)
{
  auto result = native_settings(scene, view, false);
  result["working_space"] = main.colorspace.scene_linear_name;
  result["environment"] = view.shading.lookdev_light;
  return result;
}
}
