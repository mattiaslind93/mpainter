// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_LINUX
#  include <unistd.h>
#endif
#include <cstdio>
namespace bts {
inline bool cpu_test_arguments(const QStringList &args)
{
  const QStringList allowed{"--cpu-render-status-test",
                            "--cpu-asset-dragdrop-test",
                            "--cpu-workspace-shutdown-test",
                            "--cpu-native-material-test",
                            "--cpu-bake-source-test",
                            "--cpu-skew-test",
                            "--cpu-texture-import-test",
                            "--cpu-export-test"};
  bool cpu = false;
  for (const auto &arg : args) {
    if (!arg.startsWith("--"))
      continue;
    const auto option = arg.section('=', 0, 0);
    if (allowed.contains(option)) {
      cpu = true;
      continue;
    }
    if (option.contains("demo") || option.startsWith("--cpu-") ||
        option.startsWith("--headless-") || option == "--workspace" || option == "--screenshot" ||
        option == "--state-json" || option == "--quit-after" || option == "--exit-after")
      return false;
  }
  return cpu;
}
inline bool cpu_test_arguments(int argc, char **argv)
{
  QStringList args;
  for (int i = 0; i < argc; ++i)
    args.append(QString::fromLocal8Bit(argv[i]));
  return cpu_test_arguments(args);
}
inline bool gui_test_arguments(const QStringList &args)
{
  // An explicitly requested CUDA compute test creates no display/OpenGL context.
  // BTS_NO_BACKGROUND_GPU still rejects it; its output/config are caller-owned.
  for (const auto &arg : args)
    if (arg.section('=', 0, 0) == "--headless-cycles-cuda-test")
      return false;
  for (const auto &arg : args)
    if ((arg.startsWith("--") && arg.contains("demo")) || arg.startsWith("--cpu-") ||
        arg.startsWith("--headless-") || arg == "--screenshot" || arg == "--state-json" ||
        arg.startsWith("--quit-after") || arg == "--exit-after")
      return true;
  return false;
}
inline QList<qint64> graphics_session_pids(const QString &proc = "/proc")
{
  QList<qint64> result;
#ifdef Q_OS_LINUX
  for (const auto &name : QDir(proc).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool ok = false;
    const auto pid = name.toLongLong(&ok);
    if (!ok || pid == QCoreApplication::applicationPid())
      continue;
    const auto path = proc + "/" + name;
    QFile comm(path + "/comm");
    if (!comm.open(QIODevice::ReadOnly))
      continue;
    const auto process = comm.readAll().trimmed();
    if (process != "bts-host" && process != "mpainter" && process != "mPainter" &&
        process != "blender")
      continue;
    QFile status(path + "/status");
    bool zombie = false;
    if (status.open(QIODevice::ReadOnly))
      for (const auto &line : status.readAll().split('\n'))
        if (line.startsWith("State:") && line.mid(6).trimmed().startsWith('Z'))
          zombie = true;
    if (zombie)
      continue;
    QFile cmd(path + "/cmdline");
    if (!cmd.open(QIODevice::ReadOnly))
      continue;
    QStringList args;
    for (const auto &part : cmd.readAll().split('\0'))
      if (!part.isEmpty())
        args.append(QString::fromUtf8(part));
    if (process != "blender" && cpu_test_arguments(args))
      continue;
    result.append(pid);
  }
#endif
  return result;
}
inline QList<qint64> user_session_pids(const QString &proc = "/proc")
{
  QList<qint64> result;
#ifdef Q_OS_LINUX
  for (const auto &name : QDir(proc).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool ok = false;
    const auto pid = name.toLongLong(&ok);
    if (!ok || pid == QCoreApplication::applicationPid())
      continue;
    const auto path = proc + "/" + name;
    QFile comm(path + "/comm");
    if (!comm.open(QIODevice::ReadOnly))
      continue;
    const auto process = comm.readAll().trimmed();
    if (process != "bts-host" && process != "mpainter" && process != "mPainter")
      continue;
    QFile cmd(path + "/cmdline");
    if (!cmd.open(QIODevice::ReadOnly))
      continue;
    QStringList args;
    for (auto part : cmd.readAll().split('\0'))
      if (!part.isEmpty())
        args.append(QString::fromUtf8(part));
    if (process == "bts-host" && !args.contains("--workspace"))
      continue;
    QFile env(path + "/environ");
    QString config;
    if (env.open(QIODevice::ReadOnly))
      for (auto part : env.readAll().split('\0'))
        if (part.startsWith("XDG_CONFIG_HOME="))
          config = QString::fromUtf8(part.mid(16));
    const bool isolated = (config.contains("/bts/build/") ||
                           config.contains("/mpainter-test-sessions/")) &&
                          (config.endsWith("/config") || config.endsWith("/.config"));
    if (!gui_test_arguments(args) || !isolated)
      result.append(pid);
  }
#endif
  return result;
}
// A private xcb test is allowed only on its owned Xvfb server. The explicit
// display, server command, uid, launcher ancestry and X11 lock must all agree. A flag alone
// never permits a test window on the artist's display.
inline bool owned_private_xvfb(const QString &proc = "/proc", const QString &locks = "/tmp")
{
#ifdef Q_OS_LINUX
  const auto platform = qEnvironmentVariable("QT_QPA_PLATFORM");
  if ((platform != "xcb" && platform != "offscreen") ||
      !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
    return false;
  const auto config = qEnvironmentVariable("XDG_CONFIG_HOME");
  if ((!config.contains("/bts/build/") && !config.contains("/mpainter-test-sessions/")) ||
      !(config.endsWith("/config") || config.endsWith("/.config")))
    return false;
  auto display = qEnvironmentVariable("DISPLAY");
  if (display.endsWith(".0"))
    display.chop(2);
  bool number_ok = false, pid_ok = false;
  const int number = display.startsWith(':') ? display.mid(1).toInt(&number_ok) : -1;
  const auto pid = qEnvironmentVariable("BTS_PRIVATE_XVFB_PID").toLongLong(&pid_ok);
  if (!number_ok || number <= 1 || number > 65535 || !pid_ok || pid <= 1)
    return false;
  const auto base = proc + "/" + QString::number(pid);
  if (QFileInfo(base).ownerId() != ::getuid())
    return false;
  QFile command(base + "/cmdline"), comm(base + "/comm"), status(base + "/status"),
      lock(locks + "/.X" + QString::number(number) + "-lock");
  if (!command.open(QIODevice::ReadOnly) || !comm.open(QIODevice::ReadOnly) ||
      !status.open(QIODevice::ReadOnly) || !lock.open(QIODevice::ReadOnly))
    return false;
  const auto args = command.readAll().split('\0');
  if (args.isEmpty() || QFileInfo(QString::fromLocal8Bit(args.front())).fileName() != "Xvfb" ||
      !args.contains(display.toLocal8Bit()) || comm.readAll().trimmed() != "Xvfb" ||
      lock.readAll().trimmed().toLongLong() != pid)
    return false;
  // AppImage/driver cleanup launchers may remain between xvfb-run and the app.
  qint64 owner = 0;
  for (const auto &line : status.readAll().split('\n'))
    if (line.startsWith("PPid:"))
      owner = line.mid(5).trimmed().toLongLong();
  for (qint64 ancestor = ::getppid(), depth = 0; ancestor > 1 && depth < 32; ++depth) {
    if (ancestor == owner)
      return true;
    QFile parent_status(proc + "/" + QString::number(ancestor) + "/status");
    if (!parent_status.open(QIODevice::ReadOnly))
      break;
    qint64 next = 0;
    for (const auto &line : parent_status.readAll().split('\n'))
      if (line.startsWith("PPid:"))
        next = line.mid(5).trimmed().toLongLong();
    if (next == ancestor)
      break;
    ancestor = next;
  }
  return false;
#else
  return false;
#endif
}
inline bool display_dpms_off(const QString &drm = "/sys/class/drm")
{
#ifdef Q_OS_LINUX
  int connected = 0, active = 0;
  for (const auto &name : QDir(drm).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const auto base = drm + "/" + name;
    QFile status(base + "/status");
    if (!status.open(QIODevice::ReadOnly) || status.readAll().trimmed() != "connected")
      continue;
    ++connected;
    QFile dpms(base + "/dpms"), enabled(base + "/enabled");
    const auto dpms_val = dpms.open(QIODevice::ReadOnly) ? dpms.readAll().trimmed() :
                                                           QByteArray("On");
    const auto en_val = enabled.open(QIODevice::ReadOnly) ? enabled.readAll().trimmed() :
                                                            QByteArray("enabled");
    if (dpms_val == "On" && en_val != "disabled")
      ++active;
  }
  return connected > 0 && active == 0;
#else
  return false;
#endif
}
inline bool prepare_test_session(int argc, char **argv)
{
  QStringList args;
  for (int i = 0; i < argc; ++i)
    args.append(QString::fromLocal8Bit(argv[i]));
  const bool cpu = cpu_test_arguments(args);
  if (qEnvironmentVariable("BTS_NO_BACKGROUND_GPU") == "1" && !cpu) {
    std::fprintf(stderr,
                 "BTS_GUI_SKIPPED BTS_NO_BACKGROUND_GPU=1; no GPU/GUI/OpenGL test started\n");
    return false;
  }
  if (!cpu && gui_test_arguments(args) && !owned_private_xvfb() && display_dpms_off()) {
    std::fprintf(stderr, "BTS_GUI_SKIPPED display_dpms_off; no GPU/GUI/OpenGL test started\n");
    return false;
  }
  if (!gui_test_arguments(args))
    return true;
  if (cpu) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QPA_OFFSCREEN_NO_GLX", "1");
  }
  else if (!owned_private_xvfb()) {
    const auto active = graphics_session_pids();
    if (!active.empty()) {
      std::fprintf(
          stderr,
          "BTS_GUI_SKIPPED graphics_session_active pid=%lld; no GPU/GUI/OpenGL test started\n",
          static_cast<long long>(active.front()));
      return false;
    }
  }
  const auto root = qEnvironmentVariable("XDG_CACHE_HOME", QDir::homePath() + "/.cache") +
                    "/mpainter-test-sessions/";
  const auto config = qEnvironmentVariable("XDG_CONFIG_HOME");
  const bool isolated = (config.contains("/bts/build/") ||
                         config.contains("/mpainter-test-sessions/")) &&
                        (config.endsWith("/config") || config.endsWith("/.config"));
  const auto temp = root + "session-tests/" + QString::number(QCoreApplication::applicationPid());
  qputenv("BTS_TEST_CONTINUOUS_FRAMES", "1");
  if (!isolated)
    qputenv("XDG_CONFIG_HOME", (temp + "/config").toUtf8());
  if (!qEnvironmentVariable("BTS_RECOVERY_DIR").contains("/bts/build/") &&
      !qEnvironmentVariable("BTS_RECOVERY_DIR").contains("/mpainter-test-sessions/"))
    qputenv("BTS_RECOVERY_DIR", (temp + "/recovery").toUtf8());
  return true;
}
}  // namespace bts
