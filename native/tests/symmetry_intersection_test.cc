// SPDX-License-Identifier: GPL-3.0-or-later
#include "SymmetryIntersection.hh"
#include "OrbitCamera.hh"
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cassert>
using namespace bts;
// Preserve the previous real draw path as a CPU/raster benchmark reference.
void reference(QPainter &painter,
               const std::vector<UvFaceTriangle> &triangles,
               const OrbitCamera &camera,
               int axis,
               double offset,
               QSize size)
{
  const auto project = [&](QVector3D point) {
    auto clip = camera.projection_matrix(size) * camera.view_matrix() * QVector4D(point, 1);
    return QPointF((clip.x() / clip.w() + 1) * .5 * size.width(),
                   (1 - clip.y() / clip.w()) * .5 * size.height());
  };
  for (const auto &triangle : triangles) {
    const auto &p = triangle.positions;
    QVector3D a(p[0][0], p[0][1], p[0][2]), b(p[1][0], p[1][1], p[1][2]),
        c(p[2][0], p[2][1], p[2][2]);
    const auto eye = camera.view_matrix().inverted().column(3).toVector3D();
    if (QVector3D::dotProduct(QVector3D::crossProduct(b - a, c - a), eye - a) <= 0)
      continue;
    std::vector<QVector3D> points;
    for (int i = 0; i < 3; ++i) {
      int n = (i + 1) % 3;
      double d = p[i][axis] - offset, e = p[n][axis] - offset;
      QVector3D start(p[i][0], p[i][1], p[i][2]), end(p[n][0], p[n][1], p[n][2]);
      if (std::abs(d) < 1e-7)
        points.push_back(start);
      if (d * e < 0)
        points.push_back(start + (end - start) * float(d / (d - e)));
    }
    if (points.size() < 2)
      continue;
    auto first = points[0], last = points[1];
    for (auto point : points)
      if ((point - first).lengthSquared() > (last - first).lengthSquared())
        last = point;
    painter.drawLine(project(first), project(last));
  }
}
void cached(QPainter &painter,
            SymmetryIntersectionCache &cache,
            const std::vector<UvFaceTriangle> &triangles,
            const OrbitCamera &camera,
            int axis,
            double offset,
            QSize size)
{
  const auto eye = camera.view_matrix().inverted().column(3).toVector3D();
  const auto matrix = camera.projection_matrix(size) * camera.view_matrix();
  const auto project = [&](QVector3D point) {
    auto clip = matrix * QVector4D(point, 1);
    return QPointF((clip.x() / clip.w() + 1) * .5 * size.width(),
                   (1 - clip.y() / clip.w()) * .5 * size.height());
  };
  QVector<QLineF> lines;
  for (const auto &line : cache.get(triangles, axis, offset))
    if (QVector3D::dotProduct(line.normal, eye - line.origin) > 0)
      lines.append(QLineF(project(line.a), project(line.b)));
  painter.drawLines(lines);
}
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  std::vector<UvFaceTriangle> triangles(1);
  triangles[0].positions = {{{-1, 0, 0}, {1, 0, 0}, {0, 1, 1}}};
  SymmetryIntersectionCache cache;
  auto lines = cache.get(triangles, 0, 0);
  assert(lines.size() == 1 && lines[0].a.x() == 0 && lines[0].b.x() == 0);
  cache.get(triangles, 0, 0);
  assert(cache.builds() == 1);
  cache.get(triangles, 0, .25);
  assert(cache.builds() == 2);
  cache.invalidate();
  cache.get(triangles, 0, .25);
  assert(cache.builds() == 3);
  if (argc > 1) {
    QFile file(argv[1]);
    assert(file.open(QIODevice::ReadOnly));
    auto bytes = file.readAll();
    assert(bytes.size() % (9 * sizeof(float)) == 0);
    triangles.resize(bytes.size() / (9 * sizeof(float)));
    const auto *values = reinterpret_cast<const float *>(bytes.constData());
    for (auto &triangle : triangles)
      for (auto &point : triangle.positions)
        for (float &value : point)
          value = *values++;
  }
  QVector3D lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
  for (auto &triangle : triangles)
    for (auto &p : triangle.positions)
      for (int c = 0; c < 3; ++c) {
        lo[c] = std::min(lo[c], p[c]);
        hi[c] = std::max(hi[c], p[c]);
      }
  OrbitCamera camera;
  QSize size(1000, 700);
  assert(camera.frame_bounds(lo, hi, size));
  QImage before(size, QImage::Format_ARGB32_Premultiplied), after = before;
  before.fill(Qt::black);
  after.fill(Qt::black);
  QElapsedTimer clock;
  clock.start();
  {
    QPainter p(&before);
    p.setPen(QPen(Qt::red, 1.2));
    reference(p, triangles, camera, 0, 0, size);
  }
  double old_ms = clock.nsecsElapsed() / 1e6;
  cache.invalidate();
  clock.restart();
  cache.get(triangles, 0, 0);
  double build_ms = clock.nsecsElapsed() / 1e6;
  double new_ms = 0;
  for (int i = 0; i < 10; ++i) {
    after.fill(Qt::black);
    clock.restart();
    {
      QPainter p(&after);
      p.setPen(QPen(Qt::red, 1.2));
      cached(p, cache, triangles, camera, 0, 0, size);
    }
    new_ms += clock.nsecsElapsed() / 1e6;
  }
  new_ms /= 10;
  assert(before == after);
  QJsonObject result{{"triangles", int(triangles.size())},
                     {"segments", int(cache.get(triangles, 0, 0).size())},
                     {"before_ms", old_ms},
                     {"cache_build_ms", build_ms},
                     {"after_ms", new_ms},
                     {"raster_exact", true}};
  puts(QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
  return 0;
}
