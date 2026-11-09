// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QPalette>
#include <QString>
class QApplication;
namespace bts::theme {
// Turquoise Dark, Blender 5.1 interface_theme. See assets/theme/Turquoise_Dark.xml.
// Region, panel and widget roles intentionally retain Blender's different grays.
// Black/White are endpoints for value ramps, never panel backgrounds.
inline constexpr auto Canvas = "#282828";
inline constexpr auto Surface = "#424242";
inline constexpr auto Raised = "#585858";
inline constexpr auto Hover = "#505050";
inline constexpr auto OptionInner = "#666666";
inline constexpr auto OptionOutline = "#373737";
inline constexpr auto OptionItem = "#FFFFFF";
inline constexpr auto Border = "#373737";
inline constexpr auto Separator = "#161616";
inline constexpr auto Text = "#E6E6E6";
inline constexpr auto Heading = "#FFFFFF";
inline constexpr auto Muted = "#A6A6A6";
inline constexpr auto Disabled = "#838383";
inline constexpr auto Accent = "#22947D";
inline constexpr auto Selection = "#22947D";
inline constexpr auto OnAccent = "#FFFFFF";
inline constexpr auto Warning = "#E3C16E";
inline constexpr auto Error = "#FF3352";
inline constexpr auto ErrorSurface = "#771111";
inline constexpr auto Success = "#8BDC00";
inline constexpr auto AxisX = "#FF3352";
inline constexpr auto AxisY = "#8BDC00";
inline constexpr auto AxisZ = "#2890FF";
inline constexpr auto PlotPurple = "#9B80FF";
inline constexpr auto Black = "#000000";
inline constexpr auto White = "#FFFFFF";
inline constexpr auto Viewport = "#727272";
inline constexpr auto ViewportEdge = "#404147";
inline constexpr auto Tool = "#585858";
inline constexpr auto MenuButton = "#2C2C2C";
inline constexpr auto Panel = "#3D3D3D";
inline constexpr auto Header = "#424242";
// A property section heading sits one step up the surface ladder from the panel
// it divides, the way a Blender panel header does. The old token was the same
// value as the panel, so the band could not be seen at all.
inline constexpr auto SectionHeader = "#4C4C4C";
inline constexpr auto Input = "#1F1F1F";
inline constexpr auto Number = "#595959";
inline constexpr auto ButtonText = "#D9D9D9";
inline constexpr auto ListAlternate = "#2D2D2D";
inline constexpr auto Curve = "#545454";
inline constexpr auto ListSelection = "#33524B";
inline constexpr auto ListActive = "#186858";
inline constexpr auto ScrollHandle = "#676767";
inline constexpr auto ScrollHover = "#B3B3B3";
inline constexpr auto Menu = "#1F1F1F";
inline constexpr auto MenuBar = "#232323";
inline constexpr auto Status = "#303030";
inline constexpr auto Tab = "#2B2B2B";
inline constexpr auto TabText = "#989898";
inline constexpr auto ToolbarItem = "#313131";
// Geometry tokens. Blender's wcol roundness is a fraction of a widget's height,
// so the XML value cannot be copied as a pixel radius. Turquoise Dark rounds
// nearly every widget (roundness 0.75), which maps here to one shared control
// radius: buttons, tool buttons, fields and painted cells agree instead of
// mixing 0, 1, 2 and 3 px. Circles (slider handles, the Fill invert button)
// stay half of their own fixed size and are not this token.
inline constexpr auto Radius = "3px";
inline constexpr int RadiusPx = 3;
// Spacing scale in logical pixels. Layouts pick one of these instead of an
// ad-hoc number, so panels line up with each other.
inline constexpr int SpaceTight = 2;
inline constexpr int Space = 4;
inline constexpr int SpaceWide = 6;
inline constexpr int SpaceLoose = 8;
// Between two titled sections in a panel, and between major regions in a
// dialog. Sections are separated by space before they are separated by a line.
inline constexpr int SpaceSection = 12;
inline constexpr int SpaceRegion = 16;
// Small square buttons that sit inside a list row, such as the mesh map remove
// button. Themed padding alone sized those as wide as the text beside them, so
// a row button states its own size and the column it lives in matches.
inline constexpr int RemoveButtonPx = 20;
inline constexpr int RemoveColumnPx = 28;
// A colour swatch states its own size: a small filled rectangle, which is how
// Krita, Substance Painter and Blender all show a brush colour. A panel-wide
// button with the hex printed on it reads as a button rather than as a colour,
// and cost two lines per colour once its caption is counted.
inline constexpr int SwatchWidthPx = 44;
inline constexpr int SwatchHeightPx = 20;
inline QColor alpha(const char *token, int opacity) { QColor result(token); result.setAlpha(opacity); return result; }
inline QString qss(QString source) {
  source.replace("@Viewport@", QLatin1String(Viewport));
  source.replace("@ViewportEdge@", QLatin1String(ViewportEdge));
  source.replace("@Tool@", QLatin1String(Tool));
  source.replace("@MenuButton@", QLatin1String(MenuButton));
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
  source.replace("@Panel@", QLatin1String(Panel));
  source.replace("@SectionHeader@", QLatin1String(SectionHeader));
  source.replace("@Header@", QLatin1String(Header));
  source.replace("@Input@", QLatin1String(Input));
  source.replace("@Number@", QLatin1String(Number));
  source.replace("@ButtonText@", QLatin1String(ButtonText));
  source.replace("@ListAlternate@", QLatin1String(ListAlternate));
  source.replace("@Curve@", QLatin1String(Curve));
  source.replace("@ListSelection@", QLatin1String(ListSelection));
  source.replace("@ListActive@", QLatin1String(ListActive));
  source.replace("@ScrollHandle@", QLatin1String(ScrollHandle));
  source.replace("@ScrollHover@", QLatin1String(ScrollHover));
  source.replace("@Menu@", QLatin1String(Menu));
  source.replace("@MenuBar@", QLatin1String(MenuBar));
  source.replace("@Status@", QLatin1String(Status));
  source.replace("@Tab@", QLatin1String(Tab));
  source.replace("@TabText@", QLatin1String(TabText));
  source.replace("@ToolbarItem@", QLatin1String(ToolbarItem));
  source.replace("@Radius@", QLatin1String(Radius));
  return source;
}
QPalette palette();
void apply(QApplication &app);
} // namespace bts::theme
