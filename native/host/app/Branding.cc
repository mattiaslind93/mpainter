// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include "Branding.hh"
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QEvent>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace bts {
QString product_version()
{
  return QStringLiteral(BTS_PRODUCT_VERSION);
}
QIcon product_icon()
{
  QIcon icon;
  for (int size : {16, 24, 32, 48, 64, 128, 256, 512, 1024})
    icon.addFile(QString(":/branding/mpainter-icon-%1.png").arg(size));
  return icon;
}
QString splash_featured_path()
{
  const auto override = qEnvironmentVariable("MPAINTER_SPLASH_IMAGE");
  if (!override.isEmpty())
    return override;
  QSettings settings("mPainter", "Preferences");
  const auto saved = settings.value("Branding/featuredImage").toString();
  if (!saved.isEmpty())
    return saved;
  return QDir(QCoreApplication::applicationDirPath()).filePath("branding/splash-featured.png");
}
QPointF splash_focus(const QString &featured)
{
  auto sidecar = featured;
  const int dot = sidecar.lastIndexOf('.');
  if (dot > sidecar.lastIndexOf('/'))
    sidecar.truncate(dot);
  sidecar += ".focus";
  QFile file(sidecar);
  if (!file.open(QIODevice::ReadOnly))
    return {.52, .23};
  const auto values = QString::fromUtf8(file.read(256)).simplified().split(' ');
  if (values.size() != 2)
    return {.52, .23};
  bool x_ok = false, y_ok = false;
  const double x = values[0].toDouble(&x_ok), y = values[1].toDouble(&y_ok);
  return x_ok && y_ok && std::isfinite(x) && std::isfinite(y) ?
             QPointF(std::clamp(x, 0., 1.), std::clamp(y, 0., 1.)) :
             QPointF(.52, .23);
}
QImage compose_splash(const QString &featured, QPointF /*focus*/, int scale)
{
  scale = scale >= 2 ? 2 : 1;
  QImage canvas(940 * scale, 529 * scale, QImage::Format_RGB32);
  canvas.fill(QColor(bts::theme::Canvas));
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  QImage artwork(featured);
  if (artwork.isNull())
    artwork.load(":/branding/splash-featured.png");
  // The supplied artwork is already cropped. Fit the entire image above the
  // footer, preserving its aspect ratio and every edge (including the brushes).
  const QRectF image_area(230, 0, 710, 443);
  const QSizeF fitted = QSizeF(artwork.size()).scaled(image_area.size(), Qt::KeepAspectRatio);
  const QRectF target(image_area.center() - QPointF(fitted.width() / 2, fitted.height() / 2),
                      fitted);
  painter.scale(scale, scale);
  painter.drawImage(target, artwork);
  painter.setPen(QColor(bts::theme::Heading));
  QFont title = QApplication::font();
  title.setPixelSize(34);
  title.setWeight(QFont::DemiBold);
  painter.setFont(title);
  painter.drawText(28, 76, "mPainter");
  QFont detail = QApplication::font();
  detail.setPixelSize(14);
  painter.setFont(detail);
  painter.setPen(QColor(bts::theme::Text));
  painter.drawText(28, 108, "3D texture painting");
  painter.setPen(QPen(QColor(bts::theme::Text), 1));
  painter.drawLine(28, 130, 202, 130);
  return canvas;
}
StartupSplash::StartupSplash(const QString &featured, int scale) : QSplashScreen()
{
  setObjectName("mPainterSplash");
  setWindowTitle("mPainter");
  setWindowIcon(product_icon());
  auto image = compose_splash(featured, splash_focus(featured), scale);
  auto pixmap = QPixmap::fromImage(image);
  pixmap.setDevicePixelRatio(scale);
  setPixmap(pixmap);
  QFont font = QApplication::font();
  font.setPixelSize(12);
  setFont(font);
}
void StartupSplash::drawContents(QPainter *painter)
{
  painter->setFont(font());
  painter->setPen(QColor(bts::theme::Text));
  painter->drawText(
      QRect(28, 470, 210, 22), Qt::AlignLeft | Qt::AlignTop, "Version " + product_version());
  painter->drawText(
      QRect(258, 470, 654, 42), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, status_);
}
// Qt 6.8 QSplashScreen::event(Show) waits up to one second for mapping.
// The application owns startup pacing; never wait or run nested events here.
bool StartupSplash::event(QEvent *event)
{
  if (event->type() == QEvent::Show)
    return QWidget::event(event);
  return QSplashScreen::event(event);
}
void StartupSplash::stage(const QString &status)
{
  status_ = status;
  QWidget::repaint();
}
BrandingStartup::BrandingStartup(bool enabled)
{
  timer_.start();
  featured_ = splash_featured_path();
  if (!enabled)
    return;
  const int scale = QGuiApplication::primaryScreen() &&
                            QGuiApplication::primaryScreen()->devicePixelRatio() > 1 ?
                        2 :
                        1;
  splash_ = std::make_unique<StartupSplash>(featured_, scale);
  compose_ms_ = timer_.nsecsElapsed() / 1e6;
  splash_->stage("Starting mPainter…");
  splash_->show();
  QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  if (qEnvironmentVariableIsSet("BTS_BRANDING_CAPTURE"))
    splash_->setProperty("startupCapture", splash_->grab().toImage());
}
void BrandingStartup::stage(const QString &status)
{
  if (splash_)
    splash_->stage(status);
}
void BrandingStartup::ready(QMainWindow *window)
{
  if (ready_)
    return;
  ready_ = true;
  const double elapsed = timer_.nsecsElapsed() / 1e6;
  const QImage capture = splash_ ? splash_->property("startupCapture").value<QImage>() : QImage{};
  if (splash_) {
    splash_->hide();
    splash_.reset();
  }
  std::printf("MPAINTER_READY startup_ms=%.3f splash_compose_ms=%.3f version=%s\n",
              elapsed,
              compose_ms_,
              qPrintable(product_version()));
  std::fflush(stdout);
  if (const auto path = qEnvironmentVariable("BTS_BRANDING_CAPTURE"); !path.isEmpty()) {
    QTimer::singleShot(
        1500,
        window,
        [window, path, elapsed, capture, compose = compose_ms_, featured = featured_] {
          QDir().mkpath(path);
          if (qEnvironmentVariableIsSet("MPAINTER_RELEASE_CHECK")) {
            if (auto *categories = window->findChild<QListWidget *>("shelfCategories"))
              for (int i = 0; i < categories->count(); ++i)
                if (categories->item(i)->text() == "Materials") {
                  categories->setCurrentRow(i);
                  break;
                }
            auto *assets = window->findChild<QListWidget *>("shelfAssets");
            auto *empty = window->findChild<QWidget *>("emptyMaterials");
            auto *material_count = window->findChild<QLabel *>("layerMaterialLibraryCount");
            auto *blend_materials = window->findChild<QToolButton *>(
                "editUniformMaterialLayering");
            const bool layers_valid = material_count && material_count->isHidden() &&
                                      blend_materials &&
                                      blend_materials->text() == "Blend materials…";
            const bool valid = layers_valid && assets && assets->count() == 0 && empty &&
                               empty->isVisible() &&
                               window->findChild<QPushButton *>("downloadFreeMaterialsShelf") &&
                               window->findChild<QPushButton *>("downloadFreeMaterialsEmpty") &&
                               window->findChild<QAction *>("downloadFreeMaterialsHelp");
            QFile result(path + "/empty-materials.json");
            if (result.open(QIODevice::WriteOnly))
              result.write(
                  QJsonDocument(QJsonObject{{"passed", valid},
                                            {"material_count", assets ? assets->count() : -1},
                                            {"empty_state_visible", empty && empty->isVisible()},
                                            {"layer_material_count_hidden",
                                             material_count && material_count->isHidden()},
                                            {"layer_controls_valid", layers_valid},
                                            {"download_controls", valid ? 3 : 0}})
                      .toJson());
            if (!valid) {
              std::fprintf(stderr, "MPAINTER_RELEASE_CHECK failed\n");
              QCoreApplication::exit(3);
              return;
            }
          }
          if (!capture.isNull())
            capture.save(path + "/splash.png");
          window->grab().save(path + "/workspace.png");
          window->windowIcon().pixmap(128, 128).save(path + "/window-icon.png");
          QFile file(path + "/branding.json");
          if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(
                           QJsonObject{{"display_name", QGuiApplication::applicationDisplayName()},
                                       {"internal_name", QCoreApplication::applicationName()},
                                       {"title", window->windowTitle()},
                                       {"version", product_version()},
                                       {"startup_ms", elapsed},
                                       {"splash_compose_ms", compose},
                                       {"featured", featured},
                                       {"icon", !window->windowIcon().isNull()},
                                       {"splash_closed", true}})
                           .toJson());
          if (qEnvironmentVariableIsSet("BTS_BRANDING_QUIT"))
            QCoreApplication::quit();
        });
  }
}
void add_product_about(QMainWindow *window)
{
  auto *menu = window->menuBar()->addMenu("&Help");
  auto *download = menu->addAction("Download free materials");
  download->setObjectName("downloadFreeMaterialsHelp");
  QObject::connect(download, &QAction::triggered, window, [] {
    QDesktopServices::openUrl(QUrl("https://polyhaven.com/textures"));
  });
  auto *action = menu->addAction("About mPainter");
  action->setObjectName("aboutMPainter");
  QObject::connect(action, &QAction::triggered, window, [window] {
    QMessageBox dialog(window);
    dialog.setWindowTitle("About mPainter");
    const bool dark = dialog.palette().color(QPalette::Window).lightness() < 128;
    const QPixmap logo(dark ? ":/branding/about-logo.png" : ":/branding/about-logo-light.png");
    dialog.setIconPixmap(logo.scaled(300, 93, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    dialog.setText("mPainter " + product_version());
    dialog.setInformativeText(
        "Copyright Mattias Lind\nGPL-3.0-or-later\nBuilt with Blender and Qt.\n\nNot affiliated with or endorsed by Adobe. Substance 3D is a trademark of Adobe Inc.");
    dialog.setStandardButtons(QMessageBox::Ok);
    dialog.exec();
  });
}
}  // namespace bts
