# REPORT-brand

2026-11-04 · Linux · mPainter **0.9.0-alpha**

- Fortsatte den befintliga diffen på `main`. App-/fönster-/desktop-/AppImage-ikoner och About-loggor är de godkända filerna från `../brand-final`; 25 jämförelser av käll- och paketfiler passerade. Ompaketering hanterar även skrivskyddade ikonoriginal.
- Splashasseten är byteidentisk med `../splash-crop-v2.png` (852 × 531). Hela bilden skalas proportionerligt ovanför statusraden. Kantmarkörer och footer kontrollerades vid 1×/2×; penslarna granskades visuellt.
- Svenska texter i tomvy, verktygstips, bakningsvalidering och sparstatus ersattes med engelska, inklusive `No project – File → New (Ctrl+N) / File → Open`, `Saving…` och `Saved`.
- Importerade Blender-shaders och kanalpresets visas nu även i **Material presets**. Inga medföljande material återinfördes.
- Blender 5.0.1 skapade ett nytt material med UV → Noise → ColorRamp → Principled BSDF → Material Output. I AppImagen importerades det via **File → Import native Blender shader…**, valdes i **Material presets** och applicerades på appens sfär. Den turkosa/kopparfärgade ytan och fill-lagret verifierades visuellt. Testprojekt: `../build/brand-resume/applied-sphere.blend`. Återskapningsskript: `native/tests/fixtures/create_brand_material.py` (Blender `--python … -- /path/material.blend`).
- `bts-cpu-check`, inklusive branding och tomt bibliotek/presetkategorier, samt kontrollen av GPU-vägar passerade. Slutlig AppImage klarade ren start (0 material), fönsterikon, återöppning av det sparade preset-lagret och ny sparning med `Saved` på engelska. Båda körningarna avslutades rent med exitkod 0; se `../logs/brand-final-results.json`.

All app-/Blender-körning, testning och bildfångst skedde i **bts-build**, via `xvfb-run -a -s '-screen 0 1920x1080x24'`, med `WAYLAND_DISPLAY` borttagen och kontroll mot `DISPLAY=:0/:1` före start. UI-testet använde Xvfb `:99`, llvmpipe, egen D-Bus-session och isolerade inställningar/data. Endast egna testprocesser avslutades. Den körande 1.0.0-instansen och `/home/mlind/bts` lämnades orörda.

Leverans: `../dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`.
Bilder: `../shots/brand-splash.png`, `brand-about.png`, `brand-empty.png`, `brand-preset-import.png`.
Loggar och kontroller: `../logs/brand-*.log`, `brand-*.json`, `build-brand-resume*.log` och `package-brand-resume*.log`.

AppImage SHA-256: `7b352def005b066ed288707775e280752384132b34aff035074a59737ab8de11`.

En ny lokal commit på `main`; ingen push.
