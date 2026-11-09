// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
namespace bts {
class BlenderHost;
// Explicit batch-only private fixture. No catalog, source fallback or self-test.
int run_smart_mask_cold_open_proof(BlenderHost &,
                                   const QString &project,
                                   const QString &directory);
}  // namespace bts
