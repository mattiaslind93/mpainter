#pragma once
#include <array>
#include <string>

#ifdef QT_GUI_LIB
#  include "PickerColor.hh"
#  include <QColor>
#  include <QFile>
#  include <QImage>
#  include <QPointF>
#  include <QRectF>
#  include <QSizeF>
#  include <QString>
#  include <algorithm>
#  include <cmath>
#  include <cstdint>
#  include <optional>
#  include <vector>
#endif

namespace bts {
class TextureDocument;

struct ReferenceImageState {
  bool enabled = false;
  std::string path;
  std::array<double, 2> center{0.5, 0.5};
  double vertical_extent = 1.15;
  bool operator==(const ReferenceImageState &) const = default;
};

#ifdef QT_GUI_LIB

struct LoadedReferenceImage {
  QString path;
  int width = 0;
  int height = 0;
  QImage display_image;
  std::vector<float> linear_rgba;
  bool valid() const
  {
    return width > 0 && height > 0 && !display_image.isNull() &&
           linear_rgba.size() == std::size_t(width) * std::size_t(height) * 4;
  }
};

inline QRectF reference_image_screen_rect(const QRectF &pane_rect,
                                          int image_width,
                                          int image_height,
                                          QPointF center,
                                          double vertical_extent)
{
  const double aspect = (image_width > 0 && image_height > 0) ?
                            double(image_width) / double(image_height) :
                            1.0;
  const double extent = std::max(0.01, std::isfinite(vertical_extent) ? vertical_extent : 1.15);
  const double draw_h = pane_rect.height() / extent;
  const double draw_w = draw_h * aspect;
  const double u_center = std::isfinite(center.x()) ? center.x() : 0.5;
  const double v_center = std::isfinite(center.y()) ? center.y() : 0.5;
  const double left = pane_rect.center().x() - u_center * draw_w;
  const double top = pane_rect.center().y() - (1.0 - v_center) * draw_h;
  return QRectF(left, top, draw_w, draw_h);
}

inline double reference_image_fit_vertical_extent(const QSizeF &pane_size,
                                                  int image_width,
                                                  int image_height)
{
  const double img_aspect = (image_width > 0 && image_height > 0) ?
                                double(image_width) / double(image_height) :
                                1.0;
  const double pane_aspect = (pane_size.width() > 0.0 && pane_size.height() > 0.0) ?
                                 pane_size.width() / pane_size.height() :
                                 1.0;
  return 1.1 * std::max(1.0, img_aspect / std::max(0.05, pane_aspect));
}

inline std::optional<QPointF> reference_image_uv_at(const QRectF &pane_rect,
                                                    int image_width,
                                                    int image_height,
                                                    QPointF center,
                                                    double vertical_extent,
                                                    QPointF screen_pos)
{
  if (!pane_rect.contains(screen_pos) || image_width <= 0 || image_height <= 0) {
    return std::nullopt;
  }
  const QRectF rect = reference_image_screen_rect(
      pane_rect, image_width, image_height, center, vertical_extent);
  if (rect.width() <= 0.0 || rect.height() <= 0.0) {
    return std::nullopt;
  }
  const double u = (screen_pos.x() - rect.left()) / rect.width();
  const double v = 1.0 - (screen_pos.y() - rect.top()) / rect.height();
  if (!std::isfinite(u) || !std::isfinite(v) || u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) {
    return std::nullopt;
  }
  return QPointF(u, v);
}

inline std::optional<PickerColor> sample_reference_image_uv_rgba(const LoadedReferenceImage &image,
                                                                 double u,
                                                                 double v)
{
  if (!image.valid() || !std::isfinite(u) || !std::isfinite(v) || u < 0.0 || u > 1.0 || v < 0.0 ||
      v > 1.0)
  {
    return std::nullopt;
  }
  const int px = std::clamp(int(std::floor(u * double(image.width))), 0, image.width - 1);
  const int py = std::clamp(
      int(std::floor((1.0 - v) * double(image.height))), 0, image.height - 1);
  const std::size_t idx = (std::size_t(py) * std::size_t(image.width) + std::size_t(px)) * 4;
  return PickerColor{
      image.linear_rgba[idx + 0], image.linear_rgba[idx + 1], image.linear_rgba[idx + 2], 1.0f};
}

inline std::optional<PickerColor> sample_reference_image_uv_rgba(const QImage &image,
                                                                 double u,
                                                                 double v)
{
  if (image.isNull() || image.width() <= 0 || image.height() <= 0 || !std::isfinite(u) ||
      !std::isfinite(v) || u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0)
  {
    return std::nullopt;
  }
  const int px = std::clamp(int(std::floor(u * double(image.width()))), 0, image.width() - 1);
  const int py = std::clamp(
      int(std::floor((1.0 - v) * double(image.height()))), 0, image.height() - 1);
  return picker_from_srgb(image.pixelColor(px, py));
}

inline LoadedReferenceImage make_reference_image(const QImage &source, const QString &path = {})
{
  LoadedReferenceImage result;
  if (source.isNull() || source.width() <= 0 || source.height() <= 0) {
    return result;
  }
  result.path = path;
  result.width = source.width();
  result.height = source.height();
  result.display_image = source.convertToFormat(QImage::Format_ARGB32);
  result.linear_rgba.resize(std::size_t(result.width) * std::size_t(result.height) * 4);
  std::array<PickerColor, 256> lut_r{}, lut_g{}, lut_b{};
  for (int i = 0; i < 256; ++i) {
    lut_r[i] = picker_from_srgb(QColor(i, 0, 0));
    lut_g[i] = picker_from_srgb(QColor(0, i, 0));
    lut_b[i] = picker_from_srgb(QColor(0, 0, i));
  }
  for (int y = 0; y < result.height; ++y) {
    const QRgb *line = reinterpret_cast<const QRgb *>(result.display_image.constScanLine(y));
    for (int x = 0; x < result.width; ++x) {
      const QRgb p = line[x];
      const int r = qRed(p), g = qGreen(p), b = qBlue(p);
      const std::size_t idx = (std::size_t(y) * std::size_t(result.width) + std::size_t(x)) * 4;
      result.linear_rgba[idx + 0] = lut_r[r][0] + lut_g[g][0] + lut_b[b][0];
      result.linear_rgba[idx + 1] = lut_r[r][1] + lut_g[g][1] + lut_b[b][1];
      result.linear_rgba[idx + 2] = lut_r[r][2] + lut_g[g][2] + lut_b[b][2];
      result.linear_rgba[idx + 3] = 1.0f;
    }
  }
  return result;
}

LoadedReferenceImage load_reference_image(const QString &path);
ReferenceImageState document_reference_image(const TextureDocument &document);
void set_document_reference_image(const TextureDocument &document, ReferenceImageState state);

#endif

}  // namespace bts
