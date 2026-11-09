// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "AssetCatalog.hh"
#include <functional>
class QWidget;
namespace bts {
// Ordinary application is direct. The demo/legacy harness can require a report.
inline constexpr bool spsm_confirmation_required(bool demo, bool legacy, bool accepted)
{
  return !accepted && (demo || legacy);
}
// Displays the source preparation report only in the explicit test mode.
// Approval changes metadata only; it never modifies the original source bytes.
bool confirm_spsm_source(QWidget *, StudioAsset &);
// Explicit Shelf context action; normal material application never opens this.
void show_spsm_source_report(QWidget *, const StudioAsset &);
// Shutdown boundary: stop delivery and join source workers before the viewport
// retires Blender color management/types. Accepted document edits are unaffected.
void finish_spsm_applications(QWidget *);
// Cold Shelf sources are prepared on a worker. Validity is checked before and
// after the report dialog, so changing project/set/selection cancels application.
// Optional owner-thread GPU preparation advances once per timer tick; no recipe
// is installed until every shared texture is ready. Release runs once on exit.
void prepare_spsm_application(QWidget *,
                              StudioAsset,
                              std::function<bool()> valid,
                              std::function<void(StudioAsset)> ready,
                              std::function<void(QString)> error,
                              std::function<bool(const StudioAsset &)> prepare = {},
                              std::function<void()> release = {});
}  // namespace bts
