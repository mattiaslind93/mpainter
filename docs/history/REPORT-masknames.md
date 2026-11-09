# REPORT-masknames

Referens: `/home/mlind/bts/blender-texture-studio`, `feat/environment-preflight-mac`, HEAD `5b1e4d00` och arbetskopians `BuiltinSmartMasks.cc`/`AssetCatalog.cc`. Referensen används endast för läsning.

De tio inbyggda maskerna är återställda. Hyllans alfabetiska ordning är: **Crease Dirt, Curvature edges, Edge Wear, Ground Dirt, Leaks & Stains, Moss, Position height, Rust, Scratches, Surface Dust**. Edge Rust och Moss From Top hör till referensens importerade Adobe-testmasker, inte dess tio inbyggda masker.

Egen CPU/GPU-procedurkod behålls. Referensens redigerbara maskrecept, åtta CC0-grungebilder och shaderball-underlag återställs för samma maskutseende och tumnaglar. Inga Adobe `.spmsk` buntas. Materialbiblioteket förblir tomt.

Befintliga mPainter-filnamn och UUID:n behålls. Äldre standardnamn migreras vid inläsning av maskeffekter och identifierad resursmetadata. Sparade parametrar, effekt-ID:n och egna namn bevaras; redan sparade masker byter inte recept.

Byggt i `bts-build`. Åtta C++-testbinärer passerar, liksom tema- och GPU-kodvägskontrollerna. Alla tio maskers värden och CPU-tumnaglar är pixelidentiska med en separat kompilering av referenskoden. GPU-tumnaglarna matchar också alla tio referensrecept, både i byggbinären och i den färdiga AppImagen. Resultat: `docs/masknames/validation.json`.

AppImage-hyllans tio namn och ordning verifierades via tillgänglighets-API. Skärmdumpen är en verklig fönsterfångst under `xvfb-run` i `bts-build`, granskad visuellt. Appen avslutades rent med kod 0. Inga synliga desktopfönster öppnades. Referensindata har oförändrade SHA-256.

- Skärmdump: `~/mpainter-work/shots/masknames.png`
- AppImage: `~/mpainter-work/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`
- Loggar: `~/mpainter-work/logs/*masknames*`; referensjämförelse: `~/mpainter-work/build/masknames-comparison/`
- AppImage SHA-256: `2f29b7ae0464f54565033634ea8d56daac50e503bbb2606e8af8768c0cce2450`

En commit på `main`; ingen push.
