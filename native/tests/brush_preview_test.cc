// SPDX-License-Identifier: GPL-3.0-or-later
#include "BrushPreview.hh"
#include <QApplication>
#include <cassert>
#include <vector>
// Renderer protocol oracle only. The production raster is verified separately
// by actual GPU thumbnail tests, never a fake CPU implementation of the shader.
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  bts::BrushPreview preview(nullptr);
  std::vector<std::function<void(QImage)>> complete;
  std::vector<bts::BrushSettings> requested;
  preview.set_renderer([&](const auto &brush, auto ready) {
    requested.push_back(brush);
    complete.push_back(std::move(ready));
  });
  bts::BrushSettings b;
  preview.synchronize(b);
  app.processEvents();
  assert(requested.size() == 1 && preview.generations() == 0);
  b.spacing = 41;
  preview.synchronize(b);
  b.spacing = 73;
  preview.synchronize(b);
  app.processEvents();
  assert(requested.size() == 1);
  QImage old(48, 48, QImage::Format_RGB32);
  old.fill(Qt::red);
  complete[0](old);
  app.processEvents();
  assert(preview.generations() == 0 && requested.size() == 2 && requested[1].spacing == 73);
  QImage latest(48, 48, QImage::Format_RGB32);
  latest.fill(Qt::green);
  complete[1](latest);
  assert(preview.generations() == 1 && preview.image() == latest);
  preview.synchronize(b);
  app.processEvents();
  assert(requested.size() == 2);
  b.pressure_flow = true;
  preview.synchronize(b);
  app.processEvents();
  assert(requested.size() == 3);
  complete[2](latest);
  b.stencil.offset = {.2, .3};
  preview.synchronize(b);
  app.processEvents();
  assert(requested.size() == 4);
  complete[3](latest);
  std::function<void(QImage)> dead;
  {
    bts::BrushPreview cancelled(nullptr);
    cancelled.set_renderer([&](const auto &, auto ready) { dead = std::move(ready); });
    cancelled.synchronize(b);
    app.processEvents();
  }
  assert(bool(dead));
  dead(latest);
}
