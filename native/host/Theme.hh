#pragma once
#include <QColor>
#include <QPalette>
#include <QString>
class QApplication;
namespace bts::theme {
// Single source of UI color values. Neutral/status colors follow mattias-tech-design
// tokens; Canvas and Accent also match brand-final/icon.svg. Surface is its raised role.
// Black/White are endpoints for value ramps, never panel backgrounds.
inline constexpr auto Canvas = "#111315";
inline constexpr auto Surface = "#1B1E21";
inline constexpr auto Raised = Surface;
inline constexpr auto Hover = "#2A2F34";
inline constexpr auto Border = "#717B86";
inline constexpr auto Separator = "#090A0B";
inline constexpr auto Text = "#B4BAC1";
inline constexpr auto Heading = "#F2F4F5";
inline constexpr auto Muted = "#8B949E";
inline constexpr auto Disabled = "#68737D";
inline constexpr auto Accent = "#62C7D4";
inline constexpr auto Selection = "#244950";
inline constexpr auto OnAccent = "#111315";
inline constexpr auto Warning = "#E5BC78";
inline constexpr auto Error = "#FF9B9B";
inline constexpr auto ErrorSurface = "#392628";
inline constexpr auto Success = "#8FC8A5";
inline constexpr auto AxisX = "#DF6969";
inline constexpr auto AxisY = "#83BC71";
inline constexpr auto AxisZ = "#7C9FDD";
inline constexpr auto PlotPurple = "#C378D7";
inline constexpr auto Black = "#000000";
inline constexpr auto White = "#FFFFFF";
inline QColor alpha(const char *token, int opacity) { QColor result(token); result.setAlpha(opacity); return result; }
inline QString qss(QString source) {
  source.replace("@Canvas@", QLatin1String(Canvas));
  source.replace("@Surface@", QLatin1String(Surface));
  source.replace("@Raised@", QLatin1String(Raised));
  source.replace("@Hover@", QLatin1String(Hover));
  source.replace("@Border@", QLatin1String(Border));
  source.replace("@Separator@", QLatin1String(Separator));
  source.replace("@Text@", QLatin1String(Text));
  source.replace("@Heading@", QLatin1String(Heading));
  source.replace("@Muted@", QLatin1String(Muted));
  source.replace("@Disabled@", QLatin1String(Disabled));
  source.replace("@Accent@", QLatin1String(Accent));
  source.replace("@Selection@", QLatin1String(Selection));
  source.replace("@OnAccent@", QLatin1String(OnAccent));
  source.replace("@Warning@", QLatin1String(Warning));
  source.replace("@Error@", QLatin1String(Error));
  source.replace("@ErrorSurface@", QLatin1String(ErrorSurface));
  source.replace("@Success@", QLatin1String(Success));
  source.replace("@AxisX@", QLatin1String(AxisX));
  source.replace("@AxisY@", QLatin1String(AxisY));
  source.replace("@AxisZ@", QLatin1String(AxisZ));
  source.replace("@PlotPurple@", QLatin1String(PlotPurple));
  source.replace("@Black@", QLatin1String(Black));
  source.replace("@White@", QLatin1String(White));
  return source;
}
QPalette palette();
void apply(QApplication &app);
} // namespace bts::theme
