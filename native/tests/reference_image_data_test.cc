#include "ReferenceImage.hh"
#include <cassert>
#include <cmath>
#include <cstdio>

// This target deliberately links Qt Core/Gui only. Document serialization and
// reference-image sampling must not acquire a QWidget/OpenGL dependency.
int main()
{
  using namespace bts;
  LoadedReferenceImage image;
  image.width = 2;
  image.height = 1;
  image.display_image = QImage(2, 1, QImage::Format_ARGB32);
  image.display_image.fill(Qt::black);
  image.linear_rgba = {-.25f, .125f, 2.f, 0.f, .75f, 1.5f, .25f, .5f};
  assert(image.valid());
  assert((sample_reference_image_uv_rgba(image, .25, .5) ==
          PickerColor{-.25f, .125f, 2.f, 1.f}));
  assert((sample_reference_image_uv_rgba(image, 1., .5) ==
          PickerColor{.75f, 1.5f, .25f, 1.f}));
  assert(!sample_reference_image_uv_rgba(image, -.01, .5));
  const QRectF pane(100, 20, 320, 180);
  const auto extent = reference_image_fit_vertical_extent(pane.size(), 320, 180);
  const auto rect = reference_image_screen_rect(pane, 320, 180, {.5, .5}, extent);
  assert(std::abs(rect.width() / rect.height() - 16. / 9.) < 1e-12);
  const auto uv = reference_image_uv_at(pane, 320, 180, {.5, .5}, extent, pane.center());
  assert(uv && std::abs(uv->x() - .5) < 1e-12 && std::abs(uv->y() - .5) < 1e-12);
  std::puts("reference-image data: Core/Gui only, exact HDR/signed sampling, aspect and hit-test PASS");
}
