# Poly Haven starter materials — 2026-11-04

Arbetet startade på `main`, HEAD `a376269`, med rent arbetsträd. Inga halvfärdiga ändringar fanns att återställa. Referensen `/home/mlind/bts/blender-texture-studio` användes endast för läsning av tidigare CraftyPack-import. Ingen push har gjorts.

## Bibliotek och implementation

47 inbyggda, redigerbara Blender Material Presets visas under **Assets > Materials** och **Material presets**, med bibliotek **Built-in**. 30 använder distinkta Poly Haven-texturer; 17 är nyskapade procedurmaterial. Endast de neutrala materialnamnen från uppdraget används i biblioteket.

Originalgraferna importeras genom den befintliga native-materialvägen. Fill-kontrollens **UV / Tri-planar** växlar projektion utan att ersätta källgrafer eller bildoriginal. Diffuse och AO multipliceras; OpenGL-normalerna kopplas via Normal Map. Displacement finns kvar i källgrafen och följer programmets befintliga Height-policy. Standardläget återger normal/Bump på aktuell mesh och återskapar inte geometrisk displacement; detta anges också i materialets importvarning. Procedurmaterial använder Principled BSDF med bland annat Noise, Wave, Voronoi och Bump.

Alla 47 miniatyrer är självständiga Blender Cycles-renderingar på **Shader_Ball_JL_01**, 256×256, 24 samples, CPU med fyra trådar. De lagras i materialarkiven. Originalkartorna är diskbaserade beroenden och hela biblioteket avkodas inte vid start. Se [kontaktkartan](docs/polyhaven/contact-sheet.jpg).

## Källor och slutlig matchning

Samtliga ursprungliga och slutliga textursluggar granskades mot [texturkatalogen](https://api.polyhaven.com/assets?t=textures), `/info/<slug>` och `/files/<slug>`. [API-granskningen](docs/polyhaven/api-review.json) sparar adresser, existens/type och svarens SHA-256. [Manifestet](assets/materials/polyhaven/manifest.json) innehåller varje slutlig nedladdningsadress, MD5, SHA-256 och filstorlek. Namn som Calf Hide och Galvanized Steel är ytapproximationer, inte certifieringar av art eller materialprov.

Duplicerade läder/metallval har separerats. Exempel på korrigeringar är faktisk cherry/walnut-veneer, herringbone i stället för rutigt tyg, murad sten i stället för bergvägg, lichen i stället för generisk mossa och originalprocedurer för bar järn-/stålyta i stället för målad rostplåt. Fine Rust och Rough Iron justerades även efter visuell kontroll.

| Material | Slutlig källa | Granskningsbeslut |
| --- | --- | --- |
| Aluminium Ribbed | Procedural (original) | Original procedural shader. |
| Calf Hide | [leather_white](https://polyhaven.com/a/leather_white) | Smooth animal hide; species is an approximation, not certified calf. |
| Carbon Fibre | Procedural (original) | Original procedural shader. |
| Coated Metal | [blue_metal_plate](https://polyhaven.com/a/blue_metal_plate) | Painted/coated sheet instead of bare patterned plate. |
| Bare Concrete | [concrete_floor_worn_001](https://polyhaven.com/a/concrete_floor_worn_001) | Retained: API texture type and surface tags match the intended material. |
| Clean Concrete | [concrete](https://polyhaven.com/a/concrete) | Bare concrete panels; avoids moss in original floor texture. |
| Dusty Concrete | [concrete_wall_008](https://polyhaven.com/a/concrete_wall_008) | Retained: API texture type and surface tags match the intended material. |
| Plain Concrete | [rough_concrete](https://polyhaven.com/a/rough_concrete) | Plain cement instead of stained painted wall. |
| Denim | [denim_fabric_03](https://polyhaven.com/a/denim_fabric_03) | Even twill weave without the large folds of the original. |
| Knit Wool | [knitted_fleece](https://polyhaven.com/a/knitted_fleece) | Retained: API texture type and surface tags match the intended material. |
| Rough Fabric | [hessian_230](https://polyhaven.com/a/hessian_230) | Retained: API texture type and surface tags match the intended material. |
| Suit Tweed | [poly_wool_herringbone](https://polyhaven.com/a/poly_wool_herringbone) | Wool blend herringbone instead of picnic/towel checks. |
| Gravel Ground | [gravel_floor](https://polyhaven.com/a/gravel_floor) | Retained: API texture type and surface tags match the intended material. |
| Brushed Iron | Procedural (original) | Original procedural shader. |
| Galvanized Steel | [corrugated_iron_03](https://polyhaven.com/a/corrugated_iron_03) | Galvanized steel sheeting; corrugation retained as a surface approximation. |
| Hammered Metal | Procedural (original) | Original procedural shader. |
| Powder Coat | Procedural (original) | Original procedural shader. |
| Raw Iron | Procedural (original) | Original patterned painted rusty plate is unsuitable; original bare-metal shader. |
| Rough Iron | Procedural (original) | API candidate render revealed yellow paint; use original rough bare-iron shader. |
| Polished Iron | Procedural (original) | Original procedural shader. |
| Coarse Leather | [brown_leather](https://polyhaven.com/a/brown_leather) | Retained: API texture type and surface tags match the intended material. |
| Soft Leather | [leather_red_02](https://polyhaven.com/a/leather_red_02) | Retained: API texture type and surface tags match the intended material. |
| Worn Leather | [fabric_leather_01](https://polyhaven.com/a/fabric_leather_01) | Aged wrinkled leather instead of smooth new white leather. |
| Mortar Brick Wall | [red_brick](https://polyhaven.com/a/red_brick) | Retained: API texture type and surface tags match the intended material. |
| Pebbles | [ganges_river_pebbles](https://polyhaven.com/a/ganges_river_pebbles) | Retained: API texture type and surface tags match the intended material. |
| Grainy Plastic | Procedural (original) | Original procedural shader. |
| Striped Plastic | Procedural (original) | Original procedural shader. |
| Diamond Plate Plastic | Procedural (original) | Original procedural shader. |
| Coarse Rust | [rust_coarse_01](https://polyhaven.com/a/rust_coarse_01) | Retained: API texture type and surface tags match the intended material. |
| Fine Rust | [rusty_metal_04](https://polyhaven.com/a/rusty_metal_04) | Fine red-brown surface corrosion; visually checked diffuse, replacing greenish tarnished sheet. |
| Silicone | Procedural (original) | Original procedural shader. |
| Painted Steel | [painted_metal_shutter](https://polyhaven.com/a/painted_metal_shutter) | Painted sheet; corrugated shutter is an approximation. |
| Rough Steel | Procedural (original) | Duplicate rusty plate replaced by original rough uncoated steel shader. |
| Worn Rusty Steel | [metal_plate_02](https://polyhaven.com/a/metal_plate_02) | Retained: API texture type and surface tags match the intended material. |
| Cherry Wood | [cherry_veneer](https://polyhaven.com/a/cherry_veneer) | Actual cherry wood grain instead of weathered generic planks. |
| Rough Wood | [rough_wood](https://polyhaven.com/a/rough_wood) | Rough natural timber instead of furniture tabletop. |
| Walnut Wood | [walnut_veneer](https://polyhaven.com/a/walnut_veneer) | Actual walnut veneer instead of generic dark wood. |
| Wool | [wool_boucle](https://polyhaven.com/a/wool_boucle) | Actual looped wool instead of teddy plush. |
| Reptile Scales | Procedural (original) | Original procedural shader. |
| Leaf Litter | [brown_mud_leaves_01](https://polyhaven.com/a/brown_mud_leaves_01) | Retained: API texture type and surface tags match the intended material. |
| Lichen Rock | [lichen_rock](https://polyhaven.com/a/lichen_rock) | Actual lichen-covered rock instead of generic moss. |
| Rubber | Procedural (original) | Original procedural shader. |
| Gold | Procedural (original) | Original procedural shader. |
| Copper | Procedural (original) | Original procedural shader. |
| Marble | [marble_01](https://polyhaven.com/a/marble_01) | Retained: API texture type and surface tags match the intended material. |
| Stone Wall | [rustic_stone_wall](https://polyhaven.com/a/rustic_stone_wall) | Stone masonry wall instead of natural rock face. |
| Ceramic Tiles | [floor_tiles_06](https://polyhaven.com/a/floor_tiles_06) | Retained: API texture type and surface tags match the intended material. |

## Licens

Poly Havens texturer distribueras enligt [CC0 1.0](https://polyhaven.com/license), med kredit till [Poly Haven](https://polyhaven.com) i `NOTICE.md`. Nyskapade procedurshaders är också CC0. Full CC0-text medföljer biblioteket. JL01-modellens separata CC BY 4.0-attribution kvarstår och omfattar förhandsbildernas modell. Inga kommersiella biblioteksfiler eller webbplatsens exempelrenderingar har kopierats.

## Storlekar

Det valda karturvalet i 2K skulle vara **988 796 073 byte**, över budgeten cirka 400 MB. Därför används originalfiler i **1K**: **157 kartor, 251 393 682 byte**. Diffuse/roughness/metal/AO använder tillgängliga original-JPEG; normal GL och displacement använder original-PNG. Ingen lokal omkomprimering har gjorts.

| Innehåll | Byte | MiB |
| --- | ---: | ---: |
| Texturoriginal | 251 393 682 | 239,75 |
| Native-materialarkiv inklusive miniatyrer | 7 775 705 | 7,42 |
| Hela biblioteket med metadata/licens/README | 260 357 624 | 248,30 |
| Tidigare AppImage | 438 475 256 | 418,16 |
| Ny AppImage | 696 822 264 | 664,54 |
| Ökning | **258 347 008** | **246,38** |

AppImage-ökningen är cirka 58,9 %. Den omfattar det tillagda biblioteket och den uppdaterade programbinären. SHA-256 redovisas i `docs/polyhaven/package-validation.json` och AppImagens `.sha256`-fil.

## Verifiering

- CMake-bygge i `bts-build`, `--parallel 4`: PASS; kontrollen av GPU-kodvägar passerar.
- `bts-asset-tests`: PASS med assertioner på. Kontrollerar 47 inbyggda native-material, kategorier, unika resurs-ID:n, dolda diskbaserade bildberoenden och sparade miniatyrer.
- `--cpu-asset-dragdrop-test`: PASS; hyllans faktiska MIME, insättning, dubbelklick och progress/felhantering.
- Alla 157 originalfiler: exakt API-storlek, MD5 och SHA-256. Alla 47 materialarkiv: checksumma, komplett beroendekedja, bevarade källkopplingar och fungerande normalgrafer. Alla 47 har 256px PNG-förhandsbild.
- Källbyggets riktiga UI-test: PASS. Gold, Denim, Carbon Fibre och Cherry Wood appliceras i turordning på JL01, provas med UV och triplanar, sparas och öppnas igen. GPU-prov är ändliga; de tre strukturerade materialen ändrar utvärderade prov när projektionen byts. Oberoende jämförelse bekräftar samma sparade källbytes, parametrar och upplösta bilddata. 15 projektresurser bevaras.
- Färdig AppImages extraherade bibliotek: **254 filer byteidentiska** med repot. NOTICE och JL01-mesh/attribution är också byteidentiska. [Paketbevis](docs/polyhaven/package-validation.json).
- Färdig AppImage: samma fulla UI-test PASS, från `/tmp` via `APPIMAGE_EXTRACT_AND_RUN=1`. Tid 135,65 sekunder, högsta RSS 1 935 708 KiB (1,85 GiB), noll swap-operationer. Skärmbilden `polyhaven.png` är en faktisk Xvfb-fångst från denna körning, visuellt granskad. [AppImage-testbevis](docs/polyhaven/appimage-ui-validation.json), [källbyggets testbevis](docs/polyhaven/source-ui-validation.json) och [CPU-resultat](docs/polyhaven/cpu-validation.txt).

Testerna fann ett fel i återöppning med redan cachade native-material: nya alias för resurs-ID:n ändrade inte källregistrets generation. Normalrenderaren kunde därför planera innan resursen blev tillgänglig och sedan vänta permanent. `NativeMaterialGpuLibrary` ökar nu generationen när en bindning skapas, ändras eller tas bort, men inte vid identiska bindningar. Samma UI-test reproducerade väntan före rättningen och passerade efteråt. Testets befintliga sparbevis har även gjorts användbart för JL01:s enda UV-tile; det äldre tretile-testets standardkrav kvarstår.

En tidig utvecklingskörning fick SIGSEGV när Gold applicerades. Den gav ingen användbar stacktrace och gick inte att reproducera under gdb eller i senare normala körningar. Orsaken är inte fastställd; detta redovisas som en kvarvarande diagnostisk osäkerhet, inte som ett bevisat åtgärdat fel. Övriga tidiga testfel gällde testaktivering och en gammal tretile-fixtur. De slutliga kontrollfilerna skiljs från utvecklingsloggarna under `../build/polyhaven/`.

Alla GUI/GPU-körningar och Blender-renderingar gjordes med `xvfb-run` i distrobox **bts-build**, utan synliga desktopfönster. Tunga jobb kördes sekventiellt. CMake använde högst fyra parallella jobb och AppImage-komprimeringen två. Projekttesterna använder 128×128, miniatyrerna 256×256 och varje Blender-material renderas i en separat process. Inga unload- eller 4K-stresstester kördes. Källbyggets UI-test tog 129,27 sekunder, högsta RSS 1 939 884 KiB (1,85 GiB), noll swap-operationer.

## Reproduktion och leverans

Se [bibliotekets README](assets/materials/polyhaven/README.md) för hämtning och regenerering. Den slutliga UI-körningen kan upprepas så här:

```sh
distrobox enter bts-build -- env APPIMAGE_EXTRACT_AND_RUN=1 \
  BTS_HOST_EXECUTABLE=/mnt/mpainter-phase1/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage \
  BTS_POLYHAVEN_OUTPUT=/mnt/mpainter-phase1/build/polyhaven/appimage-repeat \
  bash /mnt/mpainter-phase1/mpainter/scripts/polyhaven/run-headless.sh test
python3 scripts/polyhaven/verify.py --library assets/materials/polyhaven \
  --ui ../build/polyhaven/appimage-repeat --output ../build/polyhaven/repeat-validation.json
```

Leveranser: `~/mpainter-work/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`, `~/mpainter-work/polyhaven.png`, `~/mpainter-work/REPORT-polyhaven.md`. Källändringarna och granskningsunderlagen ingår i en lokal commit på `main`. Ingen push.
