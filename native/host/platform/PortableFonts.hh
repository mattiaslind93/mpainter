#pragma once
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_LINUX
#  include <fontconfig/fontconfig.h>
#endif
namespace bts {
// Bundled fontconfig may have a build-prefix default. Configure this process
// through its API instead of exporting FONTCONFIG_FILE to desktop children.
inline void configure_portable_fonts(const char *executable)
{
#ifdef Q_OS_LINUX
  const auto path = QFileInfo(QString::fromLocal8Bit(executable)).absolutePath() +
                    "/../share/fontconfig/fonts.conf";
  if (!QFileInfo::exists(path))
    return;
  FcConfig *config = FcConfigCreate();
  if (!config)
    return;
  if (FcConfigParseAndLoad(config,
                           reinterpret_cast<const FcChar8 *>(QFile::encodeName(path).constData()),
                           FcTrue) &&
      FcConfigBuildFonts(config))
    FcConfigSetCurrent(config);
  FcConfigDestroy(config);
#else
  (void)executable;
#endif
}
}  // namespace bts
