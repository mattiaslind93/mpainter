# Mörkt tema och Studio Environment

2026-11-04 · Linux · lokal ändring på `main`, ingen push.

- Gemensam Fusion-palett och QSS i `native/host/Theme.hh/.cc`, applicerade före splash. Dockor, rubriker, flikar, verktygsrader, menyer, dialoger, listor, egenskaper, scrollbars, Assets och statusrad använder samma tokens. Mattias lokala tech-design-skill och `brand-final` styr färgerna; loggans accent är `#62C7D4`.
- Hårdkodade UI-färger har ersatts, även i egna ritfunktioner och penselns GPU-förhandsvisning. Bild-/materialdata behåller sina färger. En automatisk källkodskontroll motverkar nya färgliteraler.
- Nya projekt använder **Studio Environment** (`studio.exr`), opacitet 100 %, oskärpa 0 %, rotation 0°. Sparade projekt och valda mallar behåller sina inställningar.
- Befintlig HDRI är CC0 enligt medföljande Blender-notis: Greg Zaal / Poly Haven. HDRI och licens är paketerade; båda paketeringsstegen kontrollerar filens SHA-256 och licensnotisen. Ingen ersättningsfil behövdes. Se `NOTICE.md` och https://polyhaven.com/license.

## Verifiering

Byggt och testat headless i `bts-build` med `WAYLAND_DISPLAY` borttaget och `xvfb-run -a -s '-screen 0 2560x1440x24'`; aldrig DISPLAY :0/:1. Slutligt UI-test använde mjukvarurendering och 125 % skalning. Endast egna testprocesser avslutades.

Godkänt: `bts-cpu-check` (branding/assets/tema), färgaudit, 32 kontrastpar, GPU-källkodskontroll samt negativa pakettester för saknad/skadad HDRI och saknad licens. AppImage och byggpaket matchar: **5 771 filer/länkar, noll avvikelser**.

Ett nytt sfärprojekt skapades genom slutpaketets UI; Studio och alla tre standardvärden kontrollerades. Helfönsterbilderna `shots/theme-main.png` och `shots/theme-dialogs.png` granskades visuellt och itererades, inklusive penselbakgrunden. Inga gamla grå panelytor kvar i granskade vyer. Extra bilder finns för splash, menyer och Preferences. Loggar: `logs/theme-final-ui.json`, `logs/theme-appimage-contents.json`, `logs/build-theme-final.log`, `logs/theme-final-tests.log`.

Diagnostikflaggan för automatiskt demoprojekt hoppades över vid sista omkörningen (kod 77) eftersom en separat användarstartad grafiksession var aktiv. Den lämnades orörd; slutkontrollen gjordes via vanligt UI i isolerad Xvfb.

## Leverans

`dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`

SHA-256: `3f2ff003cbe4ba02811ab7d2d76e343ea3132dcdc9356269d87cca58b3dc0878`

Artefakt-, bild- och loggsökvägar ovan är relativa `~/mpainter-work/`.
