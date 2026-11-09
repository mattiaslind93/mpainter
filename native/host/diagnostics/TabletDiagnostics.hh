// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QApplication>
#include <QInputDevice>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <cstdio>
namespace bts::tablet {
// Qt 6.8.3's xcb tablet path otherwise reconstructs ButtonPress coordinates
// from cached absolute valuators. XWayland may omit those valuators on the
// first press of a newly proximate tool; native event/root coordinates are
// already correct. Pressure and tilt continue to use their real valuators.
inline void configure_xwayland_coordinates()
{
#ifdef Q_OS_LINUX
  const auto platform = qEnvironmentVariable("QT_QPA_PLATFORM", "xcb");
  if (platform.startsWith("xcb") && qEnvironmentVariableIsSet("WAYLAND_DISPLAY") &&
      !qEnvironmentVariableIsSet("QT_XCB_TABLET_LEGACY_COORDINATES"))
    qputenv("QT_XCB_TABLET_LEGACY_COORDINATES", "1");
#endif
}
inline bool logging()
{
  static const bool value = qEnvironmentVariableIntValue("BTS_TABLET_LOG") == 1;
  return value;
}
inline bool pen(const QPointingDevice *device)
{
  return device && (device->pointerType() == QPointingDevice::PointerType::Pen ||
                    device->pointerType() == QPointingDevice::PointerType::Eraser ||
                    device->type() == QInputDevice::DeviceType::Stylus ||
                    device->type() == QInputDevice::DeviceType::Airbrush);
}
inline QJsonObject device_json(const QInputDevice *d)
{
  QJsonObject out{{"name", d ? d->name() : QString{}},
                  {"type", d ? int(d->type()) : 0},
                  {"id", d ? QString::number(d->systemId()) : QString{}},
                  {"capabilities", d ? int(d->capabilities()) : 0}};
  if (auto *p = qobject_cast<const QPointingDevice *>(d))
    out["pointer_type"] = int(p->pointerType());
  return out;
}
inline void log(QJsonObject data)
{
  if (!logging())
    return;
  const auto bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
  std::fprintf(stderr, "BTS_TABLET %s\n", bytes.constData());
}
inline void event(const QSinglePointEvent &e, bool contact, bool dispatch, float used)
{
  if (!logging())
    return;
  auto out = device_json(e.pointingDevice());
  out["event"] = int(e.type());
  out["timestamp"] = QString::number(e.timestamp());
  out["button"] = int(e.button());
  out["buttons"] = int(e.buttons());
  out["contact"] = contact;
  out["dispatch"] = dispatch;
  out["used_pressure"] = used;
  if (auto *t = dynamic_cast<const QTabletEvent *>(&e)) {
    out["pressure"] = t->pressure();
    out["tilt"] = QJsonArray{t->xTilt(), t->yTilt()};
    out["source"] = "tablet";
  }
  else if (auto *m = dynamic_cast<const QMouseEvent *>(&e))
    out["source"] = int(m->source());
  log(out);
}
// Qt exposes no public device-added signal. Compare the live registry, never a
// cached system ID (XWayland can reuse it), and also inspect proximity events.
class DeviceLog final : public QObject {
  QTimer timer_;
  QByteArray previous_;
  void scan()
  {
    QJsonArray devices;
    for (auto *d : QInputDevice::devices())
      devices.append(device_json(d));
    const auto key = QJsonDocument(devices).toJson(QJsonDocument::Compact);
    if (key != previous_) {
      log({{"event", "devices"}, {"devices", devices}});
      previous_ = key;
    }
  }
  bool eventFilter(QObject *, QEvent *e) override
  {
    if (e->type() == QEvent::TabletEnterProximity || e->type() == QEvent::TabletLeaveProximity) {
      if (auto *t = dynamic_cast<QTabletEvent *>(e))
        bts::tablet::event(*t, false, false, 0);
      scan();
    }
    return false;
  }

 public:
  explicit DeviceLog(QObject *parent) : QObject(parent)
  {
    if (!logging())
      return;
    qApp->installEventFilter(this);
    log({{"event", "backend"},
         {"platform", QGuiApplication::platformName()},
         {"xwayland_root_coordinates",
          qEnvironmentVariableIsSet("QT_XCB_TABLET_LEGACY_COORDINATES")}});
    scan();
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, [this] { scan(); });
    timer_.start();
  }
};
}  // namespace bts::tablet
