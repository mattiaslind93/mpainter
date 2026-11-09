#pragma once
#include <QString>
#include <QImage>
namespace bts {
struct NativeMaterialPreparationStatus {
  bool pending = false, ready = false;
  QString message, error;
  double maximum_step_ms = 0;
  QImage generated_preview;
};
}  // namespace bts
