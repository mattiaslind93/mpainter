// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UvPreview.hh"
#include <QJsonObject>
#include <memory>
namespace bts {
struct ViewFrame;
class UvGpuOverlay {
  struct Impl;
  std::unique_ptr<Impl> impl_;

 public:
  UvGpuOverlay();
  ~UvGpuOverlay();
  void draw(const UvPreviewSummary &, const ViewFrame &);
  void free();
  QJsonObject stats() const;
};
}  // namespace bts
