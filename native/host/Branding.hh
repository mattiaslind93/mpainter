#pragma once
#include <QSplashScreen>
#include <QElapsedTimer>
#include <QImage>
#include <QIcon>
#include <QJsonObject>
#include <memory>
class QMainWindow;
namespace bts {
QString product_version();
QIcon product_icon();
QImage compose_splash(const QString &featured, QPointF focus, int scale);
QString splash_featured_path();
QPointF splash_focus(const QString &featured);
class StartupSplash final : public QSplashScreen {
  QString status_;
  void drawContents(QPainter *painter) override;
  bool event(QEvent *event) override;

 public:
  explicit StartupSplash(const QString &featured, int scale);
  void stage(const QString &status);
};
class BrandingStartup {
  QElapsedTimer timer_;
  std::unique_ptr<StartupSplash> splash_;
  QString featured_;
  double compose_ms_ = 0;
  bool ready_ = false;

 public:
  explicit BrandingStartup(bool enabled);
  void stage(const QString &status);
  void ready(QMainWindow *window);
};
void add_product_about(QMainWindow *window);
}  // namespace bts
