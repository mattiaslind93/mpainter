# Paritet med mfPainter

Referens: `/home/mlind/bts/blender-texture-studio`, grenen `feat/environment-preflight-mac`, HEAD `5b1e4d00`, inklusive arbetskopians ändringar och nya filer. Referensbinärens CMake-cache pekar på detta källträd. mPainter utgick från ren `main`, `573a1a5`.

Den namnnormaliserade textdiffen före ändringar finns i [docs/parity/baseline.diff](docs/parity/baseline.diff). Inventeringen före ändringar finns i [docs/parity/before.tsv](docs/parity/before.tsv). Den innehåller samtliga 5 738 filnamn och SHA-256 för båda träden. 1 260 var byteidentiska, 34 identiska efter namnnormalisering, 124 textfiler hade ytterligare skillnader, en binärfil skilde, 42 filer tillkom och 4 277 saknades.

Normaliseringen mappar mfPainter/mfpainter/MFPAINTER och Milford-varianterna till mPainter/Studio, samt Smart Material, Smart Mask och Texture Set List till de godkända termerna. C++-filnamn som `BuiltinSmartMasks` och serialiserade `smartmask` behålls för kompatibilitet. `BrowserFileType::Substance` motsvarar `MaterialArchive` på samma enum-position. De 44 gamla Blender-patchfilerna motsvaras av den samlade `third_party/blender/patches/0001-native-host.patch`. Alla 67 ändrade spårade Blender-källfiler i den faktiska byggkällan var byteidentiska med mfPainter, se [docs/parity/blender-source.json](docs/parity/blender-source.json). Även de 13 ospårade C++-headerfilerna matchar byte för byte, se [docs/parity/blender-untracked.json](docs/parity/blender-untracked.json).

Byggets Qt 6.8.3 Core, Gui och Widgets matchar också referensens SDK-binärer byte för byte före paketeringens strip/RPATH-ändringar, se [docs/parity/qt-runtime.json](docs/parity/qt-runtime.json). En efterkontroll av samtliga 5 696 referensfiler hittade inga ändrade kontrollsummor.

## Återställda funktioner

- Penselpresets hade ersatts med en tom katalog och sex bitmapborstar hade tagits bort ur konstruktorn. `BuiltinBrushPresets.cc/.hh`, egna alpha-bilder, katalogregistrering och katalog-/codec-tester är återställda från referensens arbetskopia. Inga material eller Adobe-maskarkiv följer med detta.
- `.sbsar` hade tappat filtyp/ikon i filbläddraren. Mappningen till den befintliga materialarkivikonen är återställd.
- Den separata XML-metadata-läsaren `native/sbsar_metadata` och dess tester hade raderats. De är återställda från referensen.
- Åtta inbyggda maskrecept hade ersatts med en tom funktion; endast två enkla masker återstod. Katalogen har nu tio redigerbara generatorer med egna namn. Enligt det nya kravet använder dessa egen procedurkod i stället för medföljande bild-/Adobe-maskarkiv.
- Cycles gömde splitterposition 1, som efter den nya vänsterplacerade Assets-panelen var själva viewporten. Renderläget gömmer nu Assets via widgetens namn, så renderingen syns och kan fortsätta; återgång återställer panelernas tidigare synlighet.
- Återöppnade, packade composite-bilder återfår sin native-bildtyp inför Cycles. Avstängda hjälpbilder registreras inte längre som färdiga materialbilder; deras felaktiga registrering blockerade renderstart. Fullupplösta normaler publiceras via normalkanalen.
- Shaderball-belysningen refererade till borttagna miljöbilder. Detta gav svart materialförhandsvisning. Importförhandsvisningen använder nu appens egen studioljusmodell.

- Den fullständiga CPU-testvägen hade ersatts av tre brandingtester. `bts-cpu-check` kör åter referensens manifestbaserade testsamling. Den korta kontrollen heter nu `bts-release-check`. Raderade test- och diagnostikskript är återställda med mPainter-namnet, inklusive Qt-probens källkod och den använda Qt-filväljarkorrigeringen. Gamla verifieringsskripts hårdkodade skärm `:0` har ersatts av ärvd, verifierad Xvfb-skärm enligt säkerhetskravet. Testlaunchern använder arbetskopians runtime/Qt/Studio HDR och skrivbara testkataloger, inte referensens byggkatalog; tester för avsiktligt borttagna minnesblockeringar ersätts av de nya fullupplösningsproven.

Samtliga 266 återinförda filer listas i [docs/parity/restored-files.txt](docs/parity/restored-files.txt). Listan omfattar också testfixturer och de sex egna penselbilderna.

## Bevarade, tillåtna skillnader

Produktnamn, Material Presets/Mask Generators/Texture Sets, splash/ikon/About, branding-commit `094abbd`, borttagna medföljande material och CraftyPack samt Poly Haven-länken behålls. AppImage-installationsvägar, Qt:s standardkataloger för användardata och cache, körbart namn, paketeringsmetadata och genererad neutral sfär behålls. Temats tidigare förändringar ersätts av Mattias Turquoise Dark enligt det uttryckliga nya kravet. Studio HDR behålls.

De borttagna referensfilerna omfattar även historiska rapporter, skärmbilder, inspelade verifieringsprojekt, specifikationer, prototyper och äldre Python-appverktyg som inte ingår i den angivna native-binären. De redovisas i den fullständiga inventeringen men har inte kopierats in som produktinnehåll. Whitespace i ternära uttryck och flyttade test-/cachevägar ändrar inte beräkningsresultat.

## Nya krav som medvetet ändrar referensbeteendet

`ResourceBudget.hh` och Principled-importens Normal-blockering var identiska med mfPainter före arbetet. De tas ändå bort/ändras enligt de efterföljande uttryckliga kraven. Minnesstyrning, sekventiell bakning, GPU-fallback, Normal/Bump/Coat-import, Cycles-enhetsval och Turquoise Dark verifieras i `REPORT-bake.md`. Enbart jämförelsen med mfPainter bevisar inte att dessa nya funktioner fungerar.

Referensträdet används endast för läsning. Ingen push ingår. Headless-diagnostik tillåts parallellt med artistens app endast när den befintliga Xvfb-ägarkontrollen verifierar separat server, UID, förälder, X11-lås och isolerad konfigurationskatalog. Skärm `:0` och `:1` nekas; artistens process lämnas orörd.
