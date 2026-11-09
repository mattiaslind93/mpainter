#pragma once
#include "BlenderHost.hh"
#include "TextureDocument.hh"
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <functional>
#include <memory>
#include <vector>
class QWidget;
namespace bts {
struct StudioAsset;
// Private test-main adapter only. None of these callbacks is a production API.
struct MaterialLayeringArtistHooks {
  BlenderHost &host;
  std::function<const TextureDocument *()> document;
  std::function<std::uint32_t()> material;
  std::function<QJsonObject()> state;  // Metadata/counters only; no implicit pixel readback.
  std::function<void()> uv_view;
  std::function<void(std::array<double, 2>, std::array<double, 2>)> stroke;
  std::function<QPointF(QPointF)>
      uv_point;  // Actual viewport projection, logical widget coordinates.
  std::function<void(const QString &)> save,
      open;  // Existing owner Save/Open routes, diagnostic only.
  // Fragment-only Bump diagnostic: actual scene projection and scoped viewport
  // state. The returned restoration must run before history, Save or shutdown.
  std::function<QPointF(std::array<double, 3>)> scene_point;
  std::function<std::function<void()>()> begin_fragment_view;
  // Diagnostic full-document publication: retained on false while GPU resource
  // preparation polls; only the owner may consume it after identity/revision checks.
  std::function<bool(std::uint64_t,
                     std::uint32_t,
                     std::unique_ptr<TextureDocument> &,
                     const std::vector<StudioAsset> &)>
      publish_parameters;
};
// Returned retirement is idempotent and owner-context safe, and must run before
// Host shutdown both in main's late cleanup and its before_native_close hook.
std::function<void()> start_material_layering_ui_artist(
    QWidget *, QWidget *, const QString &, const QString &, int, MaterialLayeringArtistHooks);
}  // namespace bts
