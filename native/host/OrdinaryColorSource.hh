// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"

namespace bts {
// Ordinary image/uniform assignment supplies canonical Color. A marked source
// instead carries an explicit source-linear interpretation, even when its
// matrix is disabled. Keep that choice until the user removes its boundary.
inline void require_ordinary_color_source_replacement(const TextureLayer &layer,
                                                       int channel) {
  if(channel==0&&raw_linear_color_source(layer))
    throw std::invalid_argument("Remove the Linear RGB interpretation effect before assigning a different Base Color source.");
}
}
