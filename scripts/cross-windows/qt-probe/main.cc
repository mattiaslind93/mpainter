// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <cstdio>
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QLabel label("mPainter Windows Qt smoke test");
  label.resize(400, 80);
  QImage image(label.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  label.render(&image);
  if (image.isNull()) return 1;
  std::printf("Qt=%s platform=%s widget_render=ok pointer_bits=%zu\n", qVersion(),
              qPrintable(QApplication::platformName()), sizeof(void *) * 8);
  return 0;
}
