# Texture Sets unload fix — 2026-11-04

Implementerat på `main`, lokalt utan push. Arbetsträdet var rent vid starten; befintlig unload-kod granskades och byggdes vidare på. Referensrepositoriet har inte ändrats.

## Ändringar

- Native-materialbibliotekets shaderprogram, bildbuffertar, privata nodträd och packade bilddata kan nu lämna RAM/VRAM. Nodträd sparas med Blenders egen `.blend`-serialisering; stabila ID-skal bevaras för externa referenser. Shaderprogram och GPU-bilder skapas om vid återläsning.
- Native bildpixlar sparas rått, i begränsade block, utan bildkodning eller färgkonvertering. Float- och bytebuffertar samt färgrymdsmetadata bevaras. SHA-256 verifierar cacheinnehållet. Skrivning slutförs innan motsvarande originaldata frigörs; läsfel publicerar inte halvfärdiga pixelbuffertar.
- GPU-cache för undo, adjustments, frysta materialkällor, brush/polygon/path-resurser och tillfälliga kompositer kan tömmas. Recept och historiktillstånd bevaras för återuppbyggnad. Även undo/redo-dokumentens importerade texturer skickas till befintlig förlustfri diskcache.
- Icke redigerbara importerade material kan avlastas inklusive privata nodgrupper och bildpixlar. Tillfälliga transparensnoder hanterar synlighet. Originalets länkar och länkordning återställs exakt. Originalnätverken återställs före projektsparning.
- Resurser som fortfarande behövs av laddade material/tiles skyddas. Vid partiell UDIM-avlastning behålls ett delat shadernätverk; motsvarande tile-kompositer och mål frigörs. Aktiva GPU-lån skyddas och pågående ändringar avslutas före unload.
- Listdelegaten och radwidgeten tar nu hänsyn till den temade knapphöjden. De två `Main shader`-knapparna får varsin fullständig rad.
- Texture set-checkboxar använder synlig kant `#A6A6A6` i okryssat läge och Turquoise Dark-markering `#22947D`.

## RAM och VRAM, färdig AppImage

NVIDIA GeForce RTX 3090, 24 GiB. Alla värden nedan är byte, med MiB inom parentes. Den isolerade native-regressionen använder 32×32 bildbuffertar och 128×128 rendering, två tiles och tre historiktillstånd. Den väntar på GPU-arbete före mätning och anropar `malloc_trim(0)` för RSS-diagnostik. Återladdat mäts efter första återrenderingen, före ytterligare undo/redo-genomgång.

| Mått | Före | Avlastat | Återladdat |
|---|---:|---:|---:|
| Processens RAM, VmRSS | 479,629,312 (457.41 MiB) | 481,460,224 (459.16 MiB) | 485,785,600 (463.28 MiB) |
| Registrerad GPU-texturlagring | 2,270,556 (2.17 MiB) | 720,948 (0.69 MiB) | 2,807,452 (2.68 MiB) |
| Drivrutinens använda enhetsminne, GL_NVX | 1,085,014,016 (1034.75 MiB) | 1,085,014,016 (1034.75 MiB) | 1,085,014,016 (1034.75 MiB) |

GPU-texturlagringen minskar med 1 549 608 byte, cirka 68 %. **Total RSS minskar inte i detta lilla test, och drivrutinens rapporterade enhetsminne är oförändrat.** Dessa värden får inte tolkas som att motsvarande mängd minne har återlämnats till operativsystemet eller GPU-drivrutinen. Serialisering/återläsning, allokatorer och globala renderingsresurser påverkar RSS; drivrutinen kan behålla allokerade minnesområden efter att texturer har frigjorts. GL_NVX-värdet gäller hela enheten och inkluderar andra klienter. Testet verifierar separat att avlastade native-pixlar saknas och privata nodträd är tömda.

GUI-scenariot mäter en avlastad texture set och en avlastad tile i en annan set, medan dess första tile är kvar:

| Mått | Före | Avlastat | Återladdat |
|---|---:|---:|---:|
| Processens RAM, VmRSS | 957,079,552 (912.74 MiB) | 981,209,088 (935.75 MiB) | 994,136,064 (948.08 MiB) |
| Resident CPU-paintdata | 786,432 (0.75 MiB) | 0 (0.00 MiB) | 0 (0.00 MiB) |
| GPU-kompositer | 3,145,680 (3.00 MiB) | 1,223,368 (1.17 MiB) | 3,320,440 (3.17 MiB) |
| Samtliga registrerade GPU-texturer | 1,562,308,036 (1489.93 MiB) | 1,560,356,072 (1488.07 MiB) | 1,564,370,120 (1491.90 MiB) |

CPU-paintdata kan förbli på disk även efter återladdning; GPU-recept återspelas utan att hela CPU-bufferten behöver bli resident. GUI-siffrorna innehåller globala viewport-, miljö- och asset/thumbnail-resurser som inte ägs av avmarkerade sets. Dessa och delade material frigörs inte godtyckligt. Det finns därför ingen garanti om en viss minskning av processens totala RSS eller drivrutinens reservationer. Stora texturer/stressprov kördes avsiktligt inte efter den tidigare systemfrysningen.

## Verifiering

Samtliga nedanstående tester passerade både mot det färska källbygget och vid verifieringen av leveransen. GUI/native-GPU kördes mot själva AppImage-filen; de två fristående CPU/tematesterna kördes från samma bygge.

- `bts-theme-tests`: PASS.
- `bts-texture-set-residency-tests`: PASS; CPU-paint 524 288 → 0 byte, exakt reload/undo, frisläppta avkodade bilder, bibehållna original vid diskfel och oförändrade exportdimensioner. Maximal testtextur 128×128.
- Native unload-regression: PASS; floatbitar inklusive signed zero, HDR, subnormal och NaN-payload, bytepixlar, genererad bild med ändrat pixelvärde, importerad emission/nodgrupp och tile-synlighet. Nod-/socketvärden och länkordning jämförs före/efter. Hela GPU-pixelbufferten jämförs bitexakt för reload samt alla tre undo/redo-tillstånd med frysta native-materialkällor och Clone-adjustments. Skrivfel bevarar original och aktiva materialalias skyddas.
- Workspace checkbox-regression: PASS; texture set/tile unload–reload, CPU-hashar och GPU-prover, HDR-arbetsflöde i EEVEE/Cycles, projektsparning/återöppning och exportkontroll.
- Layoutassertion: PASS. Raderna är `(0,0,374,32)` och `(0,32,374,32)`; shaderknapparna `(271,1,93,30)` respektive `(271,33,93,30)`. Inga överlapp och båda knapparna ligger helt inom sin rad.
- Skärmbilden från AppImage-körningen granskades visuellt: synliga checkboxar, turkos bock och åtskilda shaderknappar.
- `git diff --check`: PASS.

Alla GUI-tester kördes med privata `xvfb-run`-servrar i distrobox `bts-build`, utan synliga fönster. Bygget kördes med högst `--parallel 4`; AppImage-komprimeringen använde två processorer. Inga tunga test-/byggjobb kördes samtidigt. Den dedikerade unload-regressionen hoppar över den vanliga stora headless-texturtesten. GUI-scenariots mål är 128×128; dess vanliga asset-thumbnailcache använder också 512×512-förhandsvisningar.

Reproducerbar körning av den färdiga leveransen:

```sh
distrobox enter bts-build -- env APPIMAGE_EXTRACT_AND_RUN=1 \
  BTS_HOST_EXECUTABLE=/mnt/mpainter-phase1/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage \
  bash /mnt/mpainter-phase1/mpainter/scripts/host/test-unloadfix-headless.sh
```

Mätunderlag i repot: `docs/unloadfix/native.json` och `workspace.json` gäller AppImage; `native-build.json` och `workspace-build.json` gäller källbygget. Loggar i `~/mpainter-work/build/`: `unloadfix-build.log`, `unloadfix-final-test.log`, `unloadfix-package.log`, `unloadfix-appimage-test.log`. En första paketering stoppades av rättigheter i en gammal verktygscache; slutpaketeringen använde en separat checksummeverifierad cache och lyckades.

## Leverans

- AppImage: `~/mpainter-work/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`
- Skärmbild: `~/mpainter-work/unloadfix.png`
- Rapport: `~/mpainter-work/REPORT-unloadfix.md` och kopia i repot.
- AppImage-storlek: 437,307,896 byte.
- SHA-256: `c751d16583917f260c642307efc6ce98c9af07730de2431f9843592a6740f83e`. Även `.AppImage.sha256` och relevant rad i `dist/SHA256SUMS` är uppdaterade.
