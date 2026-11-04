# Polish — Linux, 2026-11-04

Lokal ändring på `main`, ingen push. Alla appkörningar och GUI-tester gjordes i `bts-build` med `unset WAYLAND_DISPLAY` och `xvfb-run -a`, på privata displayer ≥ :99. Ingen app öppnades på Mattias skärm; endast egna testprocesser avslutades.

- **Studio:** Mattias `default-studio.hdr` är inbuntad och standard för nya projekt, även i direktbygge och Linux-startskript. Filen är byte-identisk med originalet. HDR:n belyser scenen och syns i reflektioner; kamerabakgrunden är alltid temats grå gradient, även vid återöppning. Bakgrundsopacitet/oskärpa har tagits bort från Display Settings.
- **Tema:** gråtonerna mättes i `turquoise-ref.png` och mappades till `Turquoise_Dark.xml`: region/header `#424242`, panel `#3D3D3D`, knappar `#585858`, listor `#282828`/`#2D2D2D`, numeriska fält `#595959`, scrollbar `#676767`. Viewporten använder XML:s radiella gradient `#727272`–`#404147`. Menyer, dialoger, verktyg och kurvor följer sina respektive XML-roller. 26 färgroller validerade exakt; huvudbilden jämfördes sida vid sida med referensen.
- **Greppytor:** QSlider och egna gradient-, nivå-, färgramp-, kurv- och färghjulshandtag får dubblerad bredd och höjd på träffytan. Utseendet är oförändrat; dragning behåller pekarens offset så värdet inte hoppar vid grepp.

Verifierat: `bts-theme-tests` (båda orienteringar, RTL, invertering, ändlägen, inaktiverade kontroller, gränser, signaler och oförändrat utseende), färgaudit, paketets negativa HDR-kontroller och nytt projekt med ren avslutning. Slut-AppImage:s **5 786 filer/länkar** matchar byggpaketet utan avvikelser. Headless EEVEE/llvmpipe vid 125 % skalning: HDR-rotation 0°→90° ändrade sfärens reflektioner, men fyra bakgrundsområden var pixel-identiska. Färgväljarens utökade greppyta testades även med riktiga X11-mushändelser: ingen färgförändring vid grepp, korrekt ändring vid dragning.

Leverans, relativt `~/mpainter-work/`:

- `dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`
- `shots/polish-main.png` (2560×1400), `shots/polish-compare.png`, `shots/polish-display.png`, `shots/polish-dialogs.png`, `shots/polish-menu.png`
- Mätningar och loggar: `logs/polish-colors.json`, `logs/polish-lighting.json`, `logs/polish-color-handle.json`, `logs/polish-defaults.json`, `logs/polish-appimage-contents.json`, `logs/polish-build-delivery.log`.

AppImage SHA-256: `fa79a2b1eb8a5f16b09d5e7c41f4adf996881f2339247c7cc934991b4875eb9e`.
