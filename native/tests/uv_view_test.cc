#include "UvView.hh"
#include "Theme.hh"

#include <QApplication>
#include <QImage>
#include <QRgb>

#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <QPainter>
#include <cmath>
#include <iostream>
#include <limits>

// Offscreen check for the read-only UV/Base Color widget. The interactive
// viewport has a separate GPU texture/contour path; this binary checks
// the diagnostic widget and its safe annotation-only paint_into mode.
int main(int argc, char **argv)
{
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  bts::UvView view;
  view.resize(256, 256);
  bts::UvTriangle triangle;
  triangle.uv = {QPointF(0, 0), QPointF(1, 0), QPointF(0, 1)};
  triangle.base_color = Qt::red;
  assert(view.set_triangles({triangle}));
  view.frame_all();
  assert(std::isfinite(view.pixels_per_uv()) && view.pixels_per_uv() > 1);
  const QPointF widget = view.uv_to_widget(QPointF(0.5, 0.5));
  const QPointF roundtrip = view.widget_to_uv(widget);
  assert(std::abs(roundtrip.x() - 0.5) < 1e-6 && std::abs(roundtrip.y() - 0.5) < 1e-6);

  QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::black);
  view.render(&image);
  int red = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb pixel = image.pixel(x, y);
      if (qRed(pixel) > 200 && qGreen(pixel) < 40 && qBlue(pixel) < 40) ++red;
    }
  }
  assert(red > 10);
  // A live texture is already rendered underneath this annotation pass.
  // Neither triangle colors nor the fallback grid may recolor its interior.
  const QColor source(37,83,149);QImage underlay(view.size(),QImage::Format_ARGB32_Premultiplied);underlay.fill(source);
  {QPainter painter(&underlay);view.paint_into(painter,underlay.rect(),true);}
  for(const auto uv:{QPointF(.2,.2),QPointF(.3,.2),QPointF(.2,.3)})assert(underlay.pixelColor(view.uv_to_widget(uv).toPoint())==source);

  int marker = 0;
  double before_x = 0;
  int before_n = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QRgb pixel = image.pixel(x, y);
      if (qRed(pixel) > 200 && qGreen(pixel) < 80 && qBlue(pixel) < 80) { before_x += x; ++before_n; }
      if (QColor::fromRgba(pixel) == QColor(bts::theme::Error)) ++marker;
    }
  }
  assert(marker >= 8 && before_n > 10);
  auto moved = view.triangles();
  for (auto &corner : moved.front().uv) corner.rx() += 0.45;
  assert(view.set_triangles(std::move(moved)));
  QImage edited(view.size(), QImage::Format_ARGB32_Premultiplied);
  edited.fill(Qt::black);
  view.render(&edited);
  double after_x = 0;
  int after_n = 0;
  for (int y = 0; y < edited.height(); ++y) for (int x = 0; x < edited.width(); ++x) {
    const QRgb pixel = edited.pixel(x, y);
    if (qRed(pixel) > 200 && qGreen(pixel) < 80 && qBlue(pixel) < 80) { after_x += x; ++after_n; }
  }
  assert(after_n > 10);
  assert(after_x / after_n > before_x / before_n + 20);
  std::cout << "UV edit moved preview centroid " << (before_x / before_n) << " -> " << (after_x / after_n) << "\n";

  bts::UvTriangle invalid = triangle;
  invalid.uv[1].setX(std::numeric_limits<double>::quiet_NaN());
  assert(!view.set_triangles({invalid}));
  assert(view.triangles().size() == 1);
  assert(view.triangles().front().uv[0] == QPointF(0.45, 0));
  std::cout << "UV preview widget: red_pixels=" << red << " scale=" << view.pixels_per_uv() << " rejected_nan=pass\n";
}
