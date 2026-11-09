# REPORT-splashcrop

Datum: 2026-11-04. Version: **0.9.0-alpha**.

- Splashen beskärs vid midjan, med ansikte/överkropp horisontellt centrerade. Bildytan slutar vid y=443; version och status ligger helt nedanför. Proportionerna bevaras vid 1× och 2×.
- Källan är repoasseten i 1880 × 1058, verifierad pixelidentisk med `../inputs/splash.webp` (0 avvikande pixlar). Ingen förminskad förhandsbild används.
- Versionsnummer uppdaterat i splash/About, CMake-utläsning, projektmetadata, CI och Linux/Windows/macOS-paketering. CMake behåller nu suffixet `-alpha`.
- ”4 materials…” var en fast knapptext för blandningsfunktionen. Knappen heter nu ”Blend materials…”. En separat räknare följer Materials-kategorins verkliga antal och döljs vid 0; blandningsfunktionen finns kvar.

Byggt och paketerat i `bts-build`: `bts-host`, `bts-cpu-check`, Linux-staging och AppImage. Det saknade paketeringsverktyget `file` installerades i containern. Slutlig binär strippades och fick samma relativa RPATH som paketeringsskriptet.

Verifierat: splashens bildgräns vid 1×/2×, full version i About, tomt bibliotek, räknarens 0/1/2-lägen samt befintliga Shelf→Layers-tester. Slutlig AppImage kördes på GNOME/XWayland med isolerade inställningar, rapporterade 0 material och korrekt version, och avslutades med kod 0. Skärmdumparna är faktiska Qt-fönsterfångster från den AppImagen och har granskats visuellt. Övriga plattformar har inte byggts.

- AppImage: `~/mpainter-work/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`
- Splash: `~/mpainter-work/shots/splashcrop-splash.png`
- Lagerpanel och tomt bibliotek: `~/mpainter-work/shots/splashcrop-layers.png`
- Bygg-/testloggar: `~/mpainter-work/logs/*splashcrop*`
- AppImage SHA-256: `eaf84e1859e42019028ae4cb8f82ffd8763467542a9488a8023cf3ba3fd66c4f`

Ny separat commit på `main`; ingen amend eller push. `/home/mlind/bts` har inte berörts.
