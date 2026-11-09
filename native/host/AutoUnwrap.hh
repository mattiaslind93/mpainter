#pragma once
#include "ProjectSettings.hh"
class QWidget;
namespace blender {
struct Main;
struct Scene;
struct bContext;
}  // namespace blender
namespace bts {
bool edit_auto_unwrap_options(QWidget *, AutoUnwrapOptions &);
// Operates exclusively on the unadopted import Main. Uses native editor UV
// algorithms, with no live-context switch, windows, undo stack or subprocess.
void generate_missing_uvs(blender::Main &,
                          blender::Scene &,
                          blender::bContext *,
                          const AutoUnwrapOptions &);
}  // namespace bts
