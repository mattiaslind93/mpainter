#pragma once
#include <QProcess>
#include <QProcessEnvironment>
#include <QUrl>
namespace bts {
inline bool open_desktop_url(const QUrl &url){
  auto environment=QProcessEnvironment::systemEnvironment();
  for(const auto *name:{"LD_LIBRARY_PATH","LD_PRELOAD","QT_PLUGIN_PATH","QT_QPA_PLATFORM_PLUGIN_PATH","FONTCONFIG_FILE","FONTCONFIG_PATH"})environment.remove(name);
  QProcess process;process.setProcessEnvironment(environment);
#ifdef Q_OS_MACOS
  process.setProgram("open");
#else
  process.setProgram("xdg-open");
#endif
  process.setArguments({url.toString(QUrl::FullyEncoded)});
  return process.startDetached();
}
}
