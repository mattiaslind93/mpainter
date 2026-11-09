# Shaderball — 2026-11-04

Arbetet startade på `main`, HEAD `f0b3cb7`, med rent arbetsträd. Inga
halvfärdiga ändringar fanns att återställa. Referensen
`/home/mlind/bts/blender-texture-studio` (`feat/environment-preflight-mac`, HEAD `5b1e4d00c62f46ca7206d6fb84f0015bf9c16ce5`)
användes endast för läsning. Inga push-kommandon kördes.

## Inventering före ändringen

| Användning | Befintligt objekt |
| --- | --- |
| New Project, inbyggd mesh | Endast **Sphere**, `assets/meshes/sphere.obj` |
| Genererade material- och smartmaterialtumnaglar | Matematisk sfär |
| Genererade native-materialtumnaglar | Sfär, även för GPU-utvärdering av position/normal |
| Masktumnaglar, CPU/GPU | Redan **Shader_Ball_JL_01**, via bakade PNG-underlag |

Sfärens OBJ har 2 145 vertexposter och 1 920 quadytor samt 128 trianglar. Andra geometrier som
förekommer i testfixturer är inte valbara inbyggda projektobjekt. Importerade
resurser kan ha egna sparade förhandsbilder; dessa ersätts inte.

## Ändringar

- New Project erbjuder **Sphere** och **Shaderball**. Shaderball väljer
  `Shader_Ball_JL_01.obj`; fullständigt namn, upphovsperson och licens visas
  i knappens tooltip. En saknad paketfil ger en avaktiverad knapp med förklaring.
- OBJ kopierad byteidentiskt från referensen: 38 651 vertexposter,
  UV-koordinater och normaler, 37 292 trianglar. Referensens USDZ används som
  licensbevis, men dess materialtexturer distribueras inte.
- Genererade material- och smartmaterialtumnaglar använder nu JL01.
  Native-material utvärderas på motsvarande JL01-UV-underlag med bakad
  position, normal och AO. Befintliga maskunderlag behålls byteidentiskt.
- CMake-installation och Linux/AppImage-paket inkluderar mesh och licens.
  `NOTICE.md` och maskunderlagens dokumentation innehåller attribution.

## Licens

**CC BY 4.0 tillåter kommersiell vidaredistribution**, med attribution,
licenshänvisning, uppgift om ändringar och utan ytterligare restriktioner på
själva CC-materialet. Modellen kan därför ingå i produkten enligt den
verifierade licensen.

Primärkällor kontrollerade 2026-11-04:

- [Shader Ball JL 01 på Sketchfab](https://sketchfab.com/3d-models/shader-ball-jl-01-9ecc1d8ddb5b4059a80d0bd925e3dd3b).
- [Modellens publika API-post](https://api.sketchfab.com/v3/models/9ecc1d8ddb5b4059a80d0bd925e3dd3b):
  licenslänken är CC BY 4.0 och licensposten anger att kommersiell användning är tillåten.
- [Creative Commons CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

Referensens `Shader_Ball_JL_01.usdz`, internfil `scene.usdc`, innehåller
modellens titel, samma Sketchfab-ID, upphovspersonen
`JLRIN (https://sketchfab.com/JLRIN)` och `CC-BY-4.0` med licensadress.
Det knyter den lokala källan till den publicerade licensen. Profilen heter nu
**Craneo / CRANEO_**; både det ursprungliga och det nuvarande namnet krediteras.
Sketchfabs HTML-sida nekade direkt hämtning med HTTP 403, men sökindex och den
publika API-posten var tillgängliga. Den fullständiga licenstexten hämtades
från SPDX:s licensdatabas.

`docs/shaderball/license-evidence.json` sparar API-utdrag, källhashar och
identifierade USDZ-metadatafält. `assets/meshes/Shader_Ball_JL_01-NOTICE.md`
beskriver referensens OBJ-konvertering och bakade kartor. Paketet har även
`licenses/shaderball-jl01.md` och `licenses/CC-BY-4.0.txt`.

## Verifiering och leverans

- Bygge i distrobox `bts-build`, `cmake --build … --parallel 4`: PASS.
  Projektets kontroll av GPU-kodvägar passerade också.
- `bts-asset-tests` och `bts-smart-material-preset-tests`: PASS.
- New Project-test mot källbygge och färdig AppImage: PASS. Båda knapparna
  fungerar. JL01 importeras med 37 292 trianglar och UV; sparat projekt
  återöppnas med samma geometri. Sfären importeras separat med 3 968 trianglar.
- Tumnaglarnas silhuett: PASS för material, smartmaterial och mask. Testet
  kontrollerar JL01-foten där sfären saknar täckning. Bilderna har granskats.
- Native-materialtest mot källbygget och färdig AppImage: PASS för utvärderad nodgraf,
  bevarad egen förhandsbild, generering för äldre resurser, hanterat
  importfel och ny process utan originalfil. Sparade och genererade
  förhandsbilder återöppnas pixelidentiskt.
- AppImage-testet kördes från `/tmp` för att kontrollera paketerad
  meshupplösning oberoende av repo som aktuell katalog.
- Meshar, attribution och licenstext extraherades ur den färdiga AppImagen
  och jämfördes byte för byte med källorna: PASS.
- AppImage-skärmbilden är en faktisk Xvfb-fönsterfångst, visuellt granskad.
  Den visar JL01 i viewporten och sparas som `~/mpainter-work/shaderball.png`.

Alla GUI/GPU-tester och Blender-fixturgenerering kördes under `xvfb-run`
i `bts-build`, utan synliga desktopfönster. Bygg- och testjobb kördes
sekventiellt. AppImage-komprimeringen använder två processorer.
Projekttexturerna är 128×128; native-förhandsvisningen utvärderar 64×64
meshkartor och läser tillbaka en 48×48-paket. Assethyllans befintliga
inmatningsbilder är 512×512. Inga unload-, stora mesh- eller 4K-stresstester
kördes.

Testningen hittade och rättade ett nytt formatfel: native-förhandsvisningen
försökte först använda 48×48 som meshkartstorlek. Meshkartorna använder nu
64×64, följt av den vanliga nedskalningen. En äldre felaktig fixtursökväg i
native-tumnageltestet rättades också. Teststartens isolerade config-sökväg
och aktivering av befintliga spara/öppna-teståtgärder korrigerades innan
slutkörningen. Native-testets warm-logg innehåller en icke-fatal
`BTS_GPU_READBACK_PRIOR_GL_ERROR code=1280`; pixel- och återöppningsassertionerna
passerar. Det avsiktligt skadade materialet i felhanteringstestet ger också
förväntad importvarning.

Reproduktion:

```sh
distrobox enter bts-build -- env APPIMAGE_EXTRACT_AND_RUN=1 \
  BTS_HOST_EXECUTABLE=/mnt/mpainter-phase1/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage \
  BTS_SHADERBALL_OUTPUT=/mnt/mpainter-phase1/build/shaderball-proof-new \
  bash /mnt/mpainter-phase1/mpainter/scripts/host/test-shaderball-headless.sh
```

Native-testet körs på motsvarande sätt med
`test-shaderball-native-headless.sh` och en ny separat outputkatalog.
Fixturgeneratorn använder en befintlig Blender 5.2-binär;
`BTS_TEST_BLENDER` kan ange en annan lokal sökväg.

Käll- och AppImage-bevis sparas i `docs/shaderball/`. Bygg-, test- och
paketloggar finns i `~/mpainter-work/build/shaderball-*.log` samt respektive
`shaderball-*/`-katalog.

Leveranser:

- `~/mpainter-work/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`
- `~/mpainter-work/shaderball.png`
- `~/mpainter-work/REPORT-shaderball.md` och kopia i repot

AppImage SHA-256:
`a9c97f33058e7fed15cbb8abc3d68882ac811563c331a18c22bfedccf952c129`.
Även AppImage-sidofilen `.sha256` och `dist/SHA256SUMS` är uppdaterade.


AppImage-storlek: 438 475 256 byte. `git diff --check` och testskriptens
syntaxkontroll passerar. En lokal commit på `main`; ingen push.
