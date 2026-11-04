#pragma once
#include "PaintPixels.hh"
namespace bts {
// Immutable procedural source. GPU consumers evaluate requested pages from
// concrete source metadata. CPU materialization is an explicit export/oracle
// boundary; creating or copying the source never samples pixels.
struct PaintRasterSource {
  virtual ~PaintRasterSource()=default;
  virtual PaintPixels evaluate_cpu() const=0;
  virtual std::vector<const PaintPixels *> cpu_inputs() const {return {};}
};
}
