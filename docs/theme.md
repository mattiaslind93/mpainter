# Dark interface

`native/host/Theme.hh` owns the semantic color tokens. `Theme.cc` installs the
Fusion palette and application QSS before any splash, workspace or dialog is
created. Widget-specific geometry/state rules use `theme::qss()` placeholders;
custom painters use the same tokens. The approved logo supplies `#62C7D4`;
neutral and status colors reuse the local `inputs/mattias-tech-design` skill
tokens. Its surface/raised roles map to Canvas/Surface in this existing workbench.

Do not introduce local literal interface colors. Run
`python3 scripts/host/check-theme-colors.py` when editing widgets. Material colors,
color-wheel samples, normal/ID channels, black–white value ramps, and diagnostic
pixel fixtures represent image data and must retain their meaning. They are not
recolored by the UI theme.

`bts-theme-tests` checks actual Qt dialog/list/scroll rendering and inherited
palettes; it is part of `bts-cpu-check`. Linux GUI checks must run in an isolated
Xvfb session, with isolated XDG settings and no Wayland connection.

New projects select the bundled CC0 `studio.exr`, with background opacity 100%
and blur 0%, facing the studio at rotation 0°. Saved projects and explicit project templates retain their settings.
Both Linux packaging steps require the pinned HDRI and its CC0 notice via
`scripts/check-environment.py`; see `NOTICE.md` for provenance and checksum.
