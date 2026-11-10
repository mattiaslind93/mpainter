// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <vector>

namespace bts {

enum class ViewLayout { SceneOnly, UvOnly, Split };
enum class ViewKind { Scene, Uv };

struct ViewLayoutState {
  ViewLayout mode = ViewLayout::SceneOnly;
  bool uv_first = false;

  void swap_split()
  {
    if (mode == ViewLayout::Split) {
      uv_first = !uv_first;
    }
  }
};

// Physical pixels with a top-left origin, matching Qt capture coordinates.
struct ViewPane {
  ViewKind kind;
  int x, y, width, height;
};

inline std::vector<ViewPane> view_panes(const ViewLayoutState &layout,
                                        int width,
                                        int height,
                                        int divider_width = 2)
{
  if (width < 1 || height < 1) {
    return {};
  }
  if (layout.mode != ViewLayout::Split) {
    return {
        {layout.mode == ViewLayout::UvOnly ? ViewKind::Uv : ViewKind::Scene, 0, 0, width, height}};
  }
  const int divider = std::clamp(divider_width, 0, std::max(0, width - 2));
  const int left_width = (width - divider) / 2;
  if (left_width < 1) {
    return {};
  }
  const ViewKind first = layout.uv_first ? ViewKind::Uv : ViewKind::Scene;
  const ViewKind second = layout.uv_first ? ViewKind::Scene : ViewKind::Uv;
  return {{first, 0, 0, left_width, height},
          {second, left_width + divider, 0, width - left_width - divider, height}};
}

}  // namespace bts
