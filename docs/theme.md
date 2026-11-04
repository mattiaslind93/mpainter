# Turquoise Dark

The source is `assets/theme/Turquoise_Dark.xml`, supplied by Mattias from Blender 5.1. `Theme.hh` maps the XML to Qt palette and QSS roles. `Theme.cc` applies the palette before splash and supplies widget-specific rules.

| Qt token | Blender source | RGB |
| --- | --- | --- |
| Canvas | Outliner and asset browser region | #282828 |
| Surface / Header | Properties region and header | #424242 |
| Panel | panel_back and panel_header | #3d3d3d |
| Raised | wcol_regular.inner | #585858 |
| Border | wcol_regular.outline | #373737 |
| Input | wcol_text.inner | #1f1f1f |
| Number | wcol_num.inner | #595959 |
| Accent / Selection | wcol_tool.inner_sel | #22947d |
| ListAlternate | wcol_list_item.inner RGB | #2d2d2d |
| Curve | wcol_curve.inner | #545454 |
| Viewport / ViewportEdge | View3D radial gradient | #727272 / #404147 |
| ListSelection | outliner.selected_highlight | #33524b |
| Text / Heading | panel_text / text_hi | #e6e6e6 / #ffffff |
| ScrollHandle / ScrollHover | wcol_scroll.item / inner_sel | #676767 / #b3b3b3 |
| Tab / TabText | wcol_tab.inner / text | #2b2b2b / #989898 |
| MenuBar | topbar.header | #232323 |
| Status | statusbar.header | #303030 |

Opaque Qt surfaces use the RGB component of Blender's RGBA colors. Palette selections use the opaque tool selection color; list rows use the Outliner selection. Image and material pixels are not interface colors. Startup branding remains the approved artwork.

Run `scripts/host/check-theme-colors.py` for source mapping and literal-color checks. UI tests must run in bts-build under `env -u WAYLAND_DISPLAY xvfb-run -a`, never on the desktop display.

The private-Xvfb UI capture is in `docs/parity/validation/mask-generators-ui.png`. It was visually compared with the supplied Blender reference; `theme-image-colors.json` records exact RGB pixel counts for corresponding interface roles. The asset layout and rendered scene are intentionally not pixel-matched.

The 3D camera uses the View3D radial background independently of the Studio HDR.
Properties, toolbars, dialog bodies and workspace regions use Surface; the darker
Canvas role is reserved for lists and asset regions. Standard tool buttons use
wcol_tool.inner, while the icon rails use wcol_toolbar_item.inner.

All QSliders and custom scalar/ramp/curve/color-wheel handles register their
visible geometry through SliderHitArea.hh. The application input filter doubles
width and height in logical pixels, including the area outside the widget, and
retains the original pointer offset throughout a drag. Paint geometry, track
clicks, keyboard and wheel behavior remain unchanged. Hidden or disabled handles
and handles clipped out by a scroll area are excluded. Mouse and pen input use
the same routing. `bts-theme-tests` exercises the actual event path and snapshots
handle appearance, including both orientations, RTL, inverted ranges, endpoints,
custom gradients, Levels and curve handles.
