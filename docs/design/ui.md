# mPainter interface guide

This is the reference for how mPainter looks and how its interface is put
together. It fixes the values — type sizes, colors, spacing, control sizes,
depth — and says where each one applies and why.

It exists because the interface drifted. An audit of the current build found
six different corner radii before they were unified, twelve different spacing
values, eleven different control heights, and a Properties panel holding
thirty-one widgets in one flat column under two headings. None of that was a
bad decision; it was many small decisions made separately. This document is the
place those decisions now get made once.

**The colors do not change.** The palette comes from Blender's Turquoise Dark
theme and it stays. What changes is that every color now has a stated role and
a stated place, so that nobody has to guess which of nineteen dark grays is the
right one.

**Authority.** `native/host/Theme.hh` holds the tokens; this document explains
them. Where the two disagree, the code is what ships, so fix the code. Never
write a literal color, size or spacing value into a widget — `scripts/host/check-theme-colors.py`
already fails the build for literal colors, and the same discipline applies to
the rest.

---

## 1. Principles

**Hierarchy comes from weight, color and case — not from size.** A dense
professional tool cannot afford a type scale that steps 14 → 18 → 24; the panel
would hold half as much. Section titles are the same size as body text, set in
uppercase at weight 600 in a brighter color. This is how Blender does it, and
users arrive here from Blender.

**Depth comes from lightness, not shadow.** On a dark interface a drop shadow
is nearly invisible, which is why Material's dark theme guidance raises a
surface by lightening it rather than by shadowing it ([Material Design — Dark
theme](https://m2.material.io/design/color/dark-theme)). Shadows in mPainter are
reserved for things that genuinely float above the window: menus, popovers,
dialogs, the undocked Properties window.

**Group before you decorate.** Proximity and a shared container do more for
comprehension than borders and tints ([Gestalt laws of
grouping](https://blog.logrocket.com/ux-design/gestalt-laws-of-grouping/)).
Reach for spacing first, then a heading, then a separator, and only then a
filled box.

**Hide what does not apply; keep what does in the same place.** An inspector
should change its contents as the selection changes ([Inspector
guidelines](https://marioaguzman.github.io/design/inspectorguidelines/)), but a
control that is present must not move between selections. Hide whole sections,
never reorder them.

**The panel is next to the canvas, never on top of it.** Property editing
belongs beside the thing it changes, so the change is visible while it is being
made ([Evil Martians — designing interfaces for developer
tools](https://evilmartians.com/chronicles/devs-in-mind-how-to-design-interfaces-for-developer-tools)).
The floating Properties window exists for a second monitor, not as the default.

**Ordinary before advanced.** Most-used controls sit at the top of a panel,
rarely-used ones below or in a sub-panel ([Blender Human Interface
Guidelines](https://developer.blender.org/docs/features/interface/human_interface_guidelines/layouts/)).

---

## 2. Typography

### 2.1 The face

Ship **Inter** and load it explicitly. Today the interface uses whatever each
platform supplies — San Francisco on macOS, Segoe UI on Windows, something else
again on Linux — at a fixed 11 px. The three platforms therefore have different
text metrics, so a label that fits on one clips on another, and no spacing value
can be correct everywhere at once.

Inter is already the face Blender 4.0 and later use, so the two halves of the
application agree. Blender bundles it at
`Contents/Resources/5.2/datafiles/fonts/Inter.woff2`, but Qt cannot load WOFF2:
add `Inter.ttf` (Regular and SemiBold) to the application's own assets and
register it with `QFontDatabase::addApplicationFont` at startup, before the
first window is built.

Fall back to the platform UI font if registration fails. Never fall back
silently — log it.

Monospace, where numbers must line up in a column or a hash is shown:
**DejaVu Sans Mono**, also already in Blender's datafiles.

### 2.2 The scale

Pixel sizes, because the application sets pixel sizes.

| Role | Size | Weight | Case | Color | Used for |
|---|---|---|---|---|---|
| Body | 11 | Regular | Sentence | `Text` | Labels, field contents, list rows, menu items. The default. |
| Body strong | 11 | SemiBold | Sentence | `Heading` | The one value in a row that matters most; the active item in a list. |
| Section title | 11 | SemiBold | UPPER, +0.5 px tracking | `Muted` | Every section heading in a panel: MESH MAPS, CHANNELS, SYMMETRY. |
| Caption | 10 | Regular | Sentence | `Muted` | Axis labels, units, tile counts, helper text under a control. |
| Dialog title | 13 | SemiBold | Sentence | `Heading` | Dialog and window titles only. |
| Splash | 34 / 14 | DemiBold / Regular | Sentence | `Heading` / `Text` | The splash screen alone. Not a scale anyone else may use. |

Nothing else. The audit found stray 12 px and 17 px sizes with no role; they
become Body or Caption.

### 2.3 Rules

- Numeric values that sit in a column use `font-variant-numeric: tabular-nums`
  (Qt: `QFont::setFeature("tnum")`), so digits do not jitter while dragging.
- Never set a size on a widget directly. Take it from the type tokens.
- Uppercase is only for section titles. It is not an emphasis tool.
- A label ends without a colon. The alignment already says it is a label.

---

## 3. Color

Every token in `Theme.hh`, what it means, and where it is allowed. The hex
values are Turquoise Dark's and do not change.

### 3.1 Surfaces, as a ladder

The single biggest source of inconsistency today is that there are nineteen
dark grays with no stated order, so a new panel picks one by eye. They form a
ladder. Pick by depth, not by appearance.

| Level | Token | Hex | What sits here |
|---|---|---|---|
| −1 Recessed | `Input` | `#1F1F1F` | Anything you type into or that holds content: text fields, spin boxes, combo box bodies, the console. |
| −1 Recessed | `Menu` | `#1F1F1F` | Menu and popover bodies. Same value as `Input` on purpose. |
| 0 Chrome | `MenuBar` | `#232323` | The menu bar. |
| 0 Chrome | `Tab` | `#2B2B2B` | Inactive tabs. |
| 0 Chrome | `MenuButton` | `#2C2C2C` | Menu bar buttons at rest. |
| 0 Chrome | `ListAlternate` | `#2D2D2D` | Alternating list rows. |
| 0 Chrome | `Status` | `#303030` | The status bar. |
| 0 Chrome | `ToolbarItem` | `#313131` | Toolbar segment backgrounds. |
| 1 Canvas | `Canvas` | `#282828` | The application ground: the area behind panels, the shelf background, empty space. |
| 2 Panel | `Panel` | `#3D3D3D` | A panel body sitting on the canvas. |
| 3 Surface | `Surface` | `#424242` | A card, a group box, a raised region inside a panel. |
| 3 Surface | `Header` | `#424242` | Panel and section headers. Same value as `Surface` on purpose. |
| 4 Control | `Raised` | `#585858` | Buttons, tool buttons, anything pressable at rest. |
| 4 Control | `Tool` | `#585858` | Tool rail buttons. Same value as `Raised` on purpose. |
| 4 Control | `Number` | `#595959` | Number field track. |
| 4 Control | `Curve` | `#545454` | Curve and ramp editor backgrounds. |
| 5 Interactive | `Hover` | `#505050` | Hover state on a level-3 or level-4 surface. |
| 5 Interactive | `OptionInner` | `#666666` | Checkbox and radio interiors. |
| — | `Viewport` | `#727272` | The 3D viewport clear color. Not a panel color. |
| — | `ViewportEdge` | `#404147` | The viewport's own border. |

**Four pairs are deliberate aliases** — `Surface`/`Header`, `Raised`/`Tool`,
`Border`/`OptionOutline`, `Menu`/`Input`. They are separate names so that a
future change to one role does not silently drag the other with it. Do not
"clean them up" into one token, and do not change one of a pair without
deciding about the other.

### 3.2 Lines

| Token | Hex | Where |
|---|---|---|
| `Separator` | `#161616` | The hard division between major regions: around the viewport, between dock areas. The darkest line in the interface, and the only one that reads as a structural seam. |
| `Border` | `#373737` | The outline of a control or a group box. |
| `OptionOutline` | `#373737` | Checkbox and radio outlines. Alias of `Border`. |

A separator inside a panel, between two sections, is not `Separator`. Sections
are separated by space (§5), and only by a 1 px `Border` line when space alone
is not enough.

### 3.3 Text

| Token | Hex | Where |
|---|---|---|
| `Heading` | `#FFFFFF` | Dialog titles, the active item in a list, the one emphasized value in a row. Used sparingly; white is the loudest thing in this palette. |
| `Text` | `#E6E6E6` | All ordinary text. The default. |
| `ButtonText` | `#D9D9D9` | Text on a `Raised` control, which is lighter than a panel and so needs slightly less contrast. |
| `Muted` | `#A6A6A6` | Section titles, captions, units, helper text. Information that is true but secondary. |
| `TabText` | `#989898` | Inactive tab labels. |
| `Disabled` | `#838383` | A control that cannot be used right now. Never used to make something merely quiet — that is `Muted`. |

The distinction between `Muted` and `Disabled` matters and is currently blurred
in places. Muted means "less important"; disabled means "you cannot touch this".
If a user might try to click it, it is not `Disabled`.

### 3.4 Accent and selection

| Token | Hex | Where |
|---|---|---|
| `Accent` | `#22947D` | The one thing on screen that is active or being acted on: a checked toggle, a focused field's border, a progress fill, the drop target highlight. |
| `Selection` | `#22947D` | Selected text and selected items. Alias of `Accent`. |
| `ListActive` | `#186858` | The active row in a list — the one with focus. |
| `ListSelection` | `#33524B` | Selected but not active rows. |
| `OnAccent` | `#FFFFFF` | Text and icons drawn on top of `Accent`. |

Accent is a budget, not a decoration. If more than one region of the screen is
carrying accent at the same time, one of them is wrong.

### 3.5 Meaning

| Token | Hex | Where |
|---|---|---|
| `Success` | `#8BDC00` | An operation finished. Never as a decorative green. |
| `Warning` | `#E3C16E` | Something needs attention but nothing is broken: an unbaked map, an unsaved change. |
| `Error` | `#FF3352` | Something failed. |
| `ErrorSurface` | `#771111` | The background behind an error message, so `Error` text is not the only cue. |

Semantic color is separate from the accent and never counts as it.

### 3.6 Data

| Token | Hex | Where |
|---|---|---|
| `AxisX` | `#FF3352` | X axis. Shares its value with `Error`; in a 3D context it reads as an axis, not a failure. |
| `AxisY` | `#8BDC00` | Y axis. |
| `AxisZ` | `#2890FF` | Z axis. |
| `PlotPurple` | `#9B80FF` | A fourth plotted series — curves, histograms. |
| `Black` / `White` | `#000000` / `#FFFFFF` | Endpoints of a value ramp. Never a panel background. |

### 3.7 Scrollbars

| Token | Hex | Where |
|---|---|---|
| `ScrollHandle` | `#676767` | The handle at rest. |
| `ScrollHover` | `#B3B3B3` | The handle under the pointer. |

---

## 4. Depth, shading and shadow

### 4.1 Elevation

Depth is expressed by moving up the surface ladder in §3.1, not by shadow. A
dialog over a panel is `Surface` over `Panel` over `Canvas`; that difference in
lightness is the whole effect.

### 4.2 When a shadow is allowed

Only on something that genuinely floats free of the window plane:

| Layer | Shadow |
|---|---|
| Menu, popover, combo box list | `0 4px 12px rgba(0, 0, 0, 0.45)` |
| Dialog, undocked Properties window | `0 8px 24px rgba(0, 0, 0, 0.5)` |
| Drag ghost following the pointer | `0 2px 8px rgba(0, 0, 0, 0.4)` |

Nothing else. A button does not have a shadow. A panel does not have a shadow.
A list row does not have a shadow. Shadows are never used to separate two
things that are on the same plane — that is what §5 spacing is for.

Where the platform already draws a native shadow (macOS menus, native window
frames), let it, and do not add a second one.

### 4.3 Gradients

A gradient is allowed only to suggest a light source from above, and only at a
lightness delta small enough that nobody notices it as a gradient:

- Vertical only, top lighter.
- Maximum 6 % lightness difference between the stops.
- Two stops.

It applies to exactly two things: a `Raised` control at rest, and the toolbar
band. Everywhere else is flat. A pressed control inverts the direction rather
than changing color, which is how Blender's own widgets read as physical.

Do not use a gradient to attract attention, to signal state, or to fill an
empty area. Accent does the first two; nothing does the third.

### 4.4 Focus

A focused control draws a 1 px `Accent` border, replacing its `Border`. No
glow, no outer ring, no change in size — a focus ring that changes a control's
footprint shifts the layout around it.

---

## 5. Spacing

### 5.1 The scale

Base unit 4 px. Everything is a multiple, with one 2 px step for genuinely
tight work.

| Token | Value | Use |
|---|---|---|
| `SpaceTight` | 2 | Between items in a grid of thumbnails; inside a dense list row. |
| `Space` | 4 | Between a label and its control. Between two controls in the same group. Standard gap. |
| `SpaceWide` | 6 | Between a section title and the first control under it. |
| `SpaceLoose` | 8 | Panel outer padding. Between a control group and the next. |
| `SpaceSection` | 12 | **Add this.** Between two titled sections in a panel. |
| `SpaceRegion` | 16 | **Add this.** Between major regions inside a dialog. |

The audit found 1, 2, 3, 4, 5, 6, 7, 8 and 10 px in use as gaps. There are six
legitimate values. Anything else is a mistake.

### 5.2 Padding by container

| Container | Padding |
|---|---|
| Panel body | 8 all round |
| Dialog body | 12 all round |
| Group box | 8 all round, 6 above the first control under the title |
| List row | 0 vertical, 4 horizontal |
| Toolbar | 4 all round, 4 between items |
| Menu item | 4 vertical, 8 horizontal |
| Button | 4 vertical, 8 horizontal |

### 5.3 Alignment

- Everything in a panel aligns to a single left edge. A control that is indented
  must be indented because it is subordinate, not because it looked better.
- Inside one section, every label shares one column width and every value starts
  at the same x. Across sections the width may differ; within one it may not.
- Numeric fields are right-aligned; text fields are left-aligned.
- A row of controls shares one baseline and one height.

### 5.4 Label placement

Labels sit to the **left** of their control, in a two-column row, which is what
the application does today and what Blender users expect. Keep it.

The research on label placement is genuinely mixed — Penzo's 2006 eye-tracking
study favored labels above the field, a later replication by Das and McEwan
found right-aligned labels fastest, and a third study found no significant
effect at all ([summary and
critique](https://www.effortmark.co.uk/label-placement-forms-whats-best/)).
What is not in dispute is that labels above a field cost a line of vertical
space each, and an inspector panel cannot spend that. The decision here rests on
space and on consistency with Blender, not on a claimed speed advantage.

Labels above the field are correct in one place: a dialog with a small number of
unfamiliar fields, where vertical space is not scarce.

---

## 6. Controls

### 6.1 Heights

| Size | Height | Use |
|---|---|---|
| Dense | 18 | A control inside a list row, where the row height governs. |
| Default | 22 | Every field, button, combo box and spin box in a panel. |
| Tool | 28 | Toolbar and tool rail buttons, which are square: 28 × 28. |

Three values. The audit found 14, 18, 19, 20, 22, 24, 28, 30, 31, 32 and 36.

### 6.2 Widths

- A button is as wide as its label plus 8 px padding each side, with a minimum
  of 64 px, so that OK and Cancel are not different widths.
- A field fills the column it is in. Fixed-width fields are only for values with
  a known maximum length, such as a resolution or a tile number.
- Buttons in a row are all the width of the widest one.

### 6.3 Icons

| Context | Icon | Button |
|---|---|---|
| Toolbar, tool rail | 18 | 28 × 28 |
| Inline in a row | 16 | 22 × 22 |
| Row action, such as remove | 12 | 20 × 20 |
| Mesh map thumbnail | 36 | — |
| Shelf thumbnail | 84 | — |

Icons are drawn, not typed. A Unicode character used as an icon renders at the
text size, in whatever shape that platform's font happens to supply, and cannot
be made to match the drawn set — the toolbar's old `⋈` and `◩` were unreadable
for exactly this reason. Draw new marks in `WorkspaceShell.cc`'s `Glyph` set.

Every image built for a thumbnail is generated at the display's device pixel
ratio and tagged with `setDevicePixelRatio`, or it is soft on every Retina
screen.

### 6.4 Hit targets

Anything clickable that is not inside a list row is at least 24 × 24, padding
included. A 20 × 20 row button is acceptable because the whole row is a target.

### 6.5 Radius

One value: **3 px**, as `theme::Radius` / `theme::RadiusPx`, on buttons, tool
buttons, fields, combo boxes, group boxes, progress bars and painted cells.

Circles — slider handles, the Fill invert button — are half their own fixed
size and are not this token. Nothing else is rounded at any other value.

---

## 7. Structure and grouping

### 7.1 The three ways to group

In order. Do not reach for a later one while an earlier one is still available.

1. **Space.** 12 px between sections, 4 px within one. This alone handles most
   cases, and it is the cheapest to read.
2. **A section title.** 11 px uppercase SemiBold in `Muted`, 6 px above its
   first control. Use when space alone leaves it ambiguous what belongs to what.
3. **A box.** `Surface` background, 1 px `Border`, 3 px radius, 8 px padding.
   Use only when the group has to be visibly separable from what surrounds it —
   a nested mask stack, a warning.

A section that has a title does not also need a box. A section that has a box
rarely needs a separator line as well.

### 7.2 Sections

A section is: title, then its controls, then 12 px before the next section.

- A section whose contents are all hidden hides itself, title included. An
  empty heading is worse than no heading.
- Sections keep their order between selections. Hide, never reorder.
- A section with more than about seven rows wants splitting or a sub-panel.
- Advanced controls go in a collapsible sub-panel, collapsed by default, with
  its state remembered.

### 7.3 Properties, specifically

This panel is the clearest example of what this document is for. It currently
adds **thirty-one widgets directly to one vertical layout**, with two headings
between them. Each widget shows and hides itself depending on the selection, so
the panel is correct — but it reads as an undifferentiated list, because
structurally that is exactly what it is.

The fix is not to move controls around. It is to put the existing controls into
named sections, in a fixed order:

| # | Section | Holds |
|---|---|---|
| 1 | *(context header)* | What is selected — the existing `material_label_`. No title; it is the panel's subject. |
| 2 | CONTENT | What the layer is made of: fill, paint, generator, filter, levels, anchor, imported effect, compare mask, color selection. One of these at a time. |
| 3 | CHANNELS | Base Color, Metallic, Roughness, Normal, Bump, Displacement, AO. The channel rows, in this fixed order. |
| 4 | PROJECTION | UV / Triplanar, scale, rotation, offset, wrap, warp, 3D projection settings. |
| 5 | BLENDING | Blend mode and opacity. |
| 6 | SYMMETRY | The symmetry controls. |
| 7 | ADVANCED | Collapsed by default. Everything that is rarely touched. |

Implementation: a small `PropertySection` widget — title, content area, optional
collapse — and the thirty-one children become seven. `synchronize()` then shows
and hides sections rather than individual widgets, and a section hides itself
when everything in it is hidden.

This is a contained change: the controls, their callbacks and their behaviour
all stay as they are. Only their parent changes.

### 7.4 Tabs

Layers, Texture Sets and Properties are peers in one tab bar. Properties opens
first, because it is what you look at while working.

Tabs are for views of the same subject. A tab is not a place to put something
that did not fit.

---

## 8. Motion

Almost none.

| What | Duration | Curve |
|---|---|---|
| Hover and press feedback | 0 — immediate | — |
| Panel or section collapse | 120 ms | ease-out |
| Progress and loading | continuous | linear |

Nothing else animates. A paint application is judged on whether the brush keeps
up with the hand; interface animation competes for the same frame budget.
Honour `prefers-reduced-motion` where the platform exposes it.

---

## 9. Writing

- Sentence case for everything: labels, buttons, menu items, titles. Not Title
  Case.
- A button says what it does: *Bake*, *Apply*, *Export*. Not *OK* where a verb
  exists.
- An error says what failed and what to do: "Add a layer to this Texture Set
  before dropping a mask generator", not "Invalid target".
- Use the user's words, not the implementation's: *mesh map*, not *map resource*.
- No ellipsis unless the control really opens a dialog. With one, always.
- Units go in the field's suffix, not in the label.

---

## 10. What is currently wrong

Found while auditing the present build. Each is small on its own.

| Area | State | Fix |
|---|---|---|
| Font | Platform default, so text metrics differ per platform | Bundle and load Inter (§2.1) |
| Type sizes | 10, 11, 12, 14, 17, 34 px, three without a role | Six roles, §2.2 |
| Control heights | Eleven different values | Three, §6.1 |
| Spacing | Nine different gap values | Six, §5.1 |
| Properties panel | 31 widgets, one column, 2 headings | Seven sections, §7.3 |
| `Muted` vs `Disabled` | Used interchangeably in places | §3.3 |
| Surface grays | Nineteen, with no stated order | The ladder, §3.1 |
| Shadows | None anywhere, including on menus and dialogs | §4.2 |
| Gradients | None | Two places only, §4.3 |

Already fixed: corner radii (six values → one), the mesh map thumbnails and
remove buttons, the toolbar's text-character icons, and the Properties tab.

## 11. Before you commit interface code

- Every color is a `Theme.hh` token. No literals.
- Every size and gap is from §5.1, §6.1 or the type scale.
- The new control's height matches the controls beside it.
- Labels in the section share one column width.
- The section hides itself when its contents are all hidden.
- Nothing moved position between two selections.
- It was looked at on a Retina display, and any generated image is tagged with
  the device pixel ratio.
- It was looked at at the minimum window size, 1000 × 680.

---

Sources consulted: [Material Design — Dark theme](https://m2.material.io/design/color/dark-theme),
[Blender Human Interface Guidelines — Layouts](https://developer.blender.org/docs/features/interface/human_interface_guidelines/layouts/),
[Evil Martians — Designing interfaces for developer tools](https://evilmartians.com/chronicles/devs-in-mind-how-to-design-interfaces-for-developer-tools),
[Inspector guidelines](https://marioaguzman.github.io/design/inspectorguidelines/),
[LogRocket — Gestalt laws of grouping](https://blog.logrocket.com/ux-design/gestalt-laws-of-grouping/),
[Caroline Jarrett — Label placement in forms](https://www.effortmark.co.uk/label-placement-forms-whats-best/),
[cxpartners — Web form design guidelines: an eyetracking study](https://www.cxpartners.co.uk/our-thinking/web_forms_design_guidelines_an_eyetracking_study/),
[UX Planet — How to design a properties panel](https://uxplanet.org/how-to-design-properties-panel-4d562cc47da3).
