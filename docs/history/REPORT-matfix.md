# Poly Haven material corrections — 2026-11-04

Arbetet startade på `main`, HEAD `f9ce10e`, med rent arbetsträd. Det fanns inga halvfärdiga spårade ändringar från det avbrutna försöket att återställa. Allt arbete och alla leveranser är lokala; ingen push.

## Wool: fel motiv, inte saknad bild

`wool_boucle_diff_1k.jpg` innehåller faktiskt det gulsvarta rutmönstret. Dess storlek, MD5 och SHA-256 stämde med Poly Havens API, beroendet fanns i native-arkivet och samma motiv syns direkt i originalbilden. API-taggarna innehåller `checker`. Det var alltså ett olämpligt texturval, inte en trasig sökväg eller reservbild.

Wool använder nu [polar_fleece](https://polyhaven.com/a/polar_fleece): enfärgad gräddvit, luddig fleece som visuell approximation av ull. Den gör inte anspråk på verifierad fibersammansättning. Fem oförändrade originalkartor i 1K hämtades från [Poly Havens fil-API](https://api.polyhaven.com/files/polar_fleece), med verifierade MD5/SHA-256. Den tidigare uppsättningens sex oanvända kartor är borttagna. Material, normaler och JL01-miniatyr är regenererade. [Verifierad Wool-vy i AppImagen](docs/matfix/wool-viewport.png). [Källgranskning](docs/matfix/wool-source-review.json).

## Distinkta material och granskning av hela biblioteket

Alla 47 inbyggda material har granskats i en kontaktkarta byggd från arkivens faktiska inbäddade miniatyrer. Samtliga 30 texturbaserade material använder olika Poly Haven-källor; de 17 procedurmaterialen är egna CC0-grafer. [Beslut för varje material](docs/matfix/material-review.json), [kontaktkarta](docs/matfix/contact-sheet.jpg).

Nio material har ändrats och fått nya Cycles-miniatyrer på **Shader_Ball_JL_01**, 256×256, 24 samples, CPU med fyra trådar, en process i taget:

| Material | Ändring |
| --- | --- |
| Wool | Enfärgad noppig fleece i stället för gulsvart rutväv. |
| Rough Iron | Mörkt, varmt gjutjärn med grov kornighet, tydlig relief och hög roughness (0,68–0,92). |
| Raw Iron | Blåsvart glödskal med breda blottade metallpartier och varierad roughness (0,30–0,78). |
| Rough Steel | Ljusare, kall stålyta med riktade slipspår och satinroughness (0,38–0,66). |
| Brushed Iron | Finare riktad borstning och lägre roughness (0,19–0,36), åtskild från slät Polished Iron. |
| Grainy Plastic | Kornig salviagrön polymer, åtskild från blå Powder Coat och beige material. |
| Walnut Wood | Mörkare oljad valnötston med bevarad ådring, åtskild från ljus Cherry Wood. |
| Calf Hide | Varmare ljust brun läderton, åtskild från gråbeige Dusty Concrete. |
| Worn Leather | Blekta, kallt gråbruna slitna ytor, åtskilda från rödbrun Soft Leather och mörk Coarse Leather. |

Rough Steels relief och färgvariation dämpades även efter granskning i den faktiska 1K-vyn, så att slipspåren inte blir överdrivet korrugerade. Färgjusteringarna ligger i redigerbara noder; bildoriginalen har inte skrivits om. Övriga material behåller sina ursprungliga ytkännetecken, exempelvis betongens fogar/porer, tygernas olika väv, korrugerad plåt och hamrade metallytor. En enkel [RGB-avståndskontroll](docs/matfix/thumbnail-distance-review.json) kompletterar den visuella granskningen men behandlas inte som ett perceptuellt facit. Raw Iron/Rough Steel ökade exempelvis från 12,48 till 49,49 i RMS på samma centrala utsnitt.

## Varför Cherry Wood visade 128

Den tidigare leveransens `PolyHavenDemo.hh` valde uttryckligen `documentResolution=128`, och `run-headless.sh test` skickade också `--gui-fix-resolution 128`. Det sparade demonstrationsprojektet och dess screenshot hade därför en **128×128-komposit**. Statusradens tal beskriver denna Texture Set-komposit, inte källbildens upplösning. Den begränsningen suddade ut fin ådring.

Programmets vanliga New Project-standard var redan 1024 (`ProjectSettings.hh`, `NewProjectDialog.cc`). Cherry Woods fem bundlade bilder är 1024×1024. `NativeMaterialImageUpload.cc` allokerar och överför källans fulla dimensioner i begränsade paket; där finns ingen 128-begränsning att ta bort.

Standardkommandot `run-headless.sh test` använder nu en ensam Cherry Wood på JL01 och dialogens **orörda 1024-standard**. Testet kräver både 1024 i dialogen och faktisk 1024×1024-komposit, provar UV/triplanar, sparar och öppnar projektet samt kontrollerar upplösningen igen. Den gamla fyrmaterialsregressionen finns separat som `test-mapping` vid 128, så att dess historik och flera material inte i onödan multiplicerar minnesåtgången.

Den levererade [viewportbilden](docs/matfix/cherry-viewport.png) kommer från den färdiga AppImagens 1K-körning efter spara/öppna. Både bild och GPU-sampler verifieras som 1024×1024. Ådringen syns diagonalt i UV och längsgående i triplanar. Källbilder, diskbaserade beroenden, stegvis uppladdning, cache- och unload-policy är bevarade. Äldre projekts redan inbäddade material byts inte automatiskt ut; applicera den uppdaterade inbyggda preseten om du vill ersätta dem. Befintliga projekt som uttryckligen sparats i 128 ändras inte automatiskt: välj **1024 i Texture Set-inställningarna** för dessa. Det undviker oväntad minnesökning i äldre projekt.

## Ytterligare rättning: läsning utanför indexbufferten

Fyrmaterialsregressionen återgav först den intermittenta krasch vid Gold som
redan nämndes i `REPORT-polyhaven.md`. Under gdb inträffade samma typ av krasch
vid återöppning. Stacktracen pekade på `NativeMaterialFragmentGpuGeometry` →
`GPU_indexbuf_build_from_memory` → parallell minneskopiering.

Orsaken var ett konkret API-kontraktsfel: Blender tar **antal primitiver** och
multiplicerar med tre för trianglar. Anroparen skickade **antal index**. Resultatet
blev en tre gånger för stor kopiering, läsning utanför källbufferten och en
överstor IBO som också underskattades av den befintliga minnesberäkningen.

Rättningen skickar `indices.size()/3`, kräver kompletta trianglar och kontrollerar
att den faktiska IBO-längden matchar källans indexantal. Den befintliga GPU-
regressionen verifierar nu faktisk buffertlängd, utöver bildresultat, återanvändning,
Undo och tillståndsåterställning. Den riktade körningens texturmål är bara 128 px. Den riktade körningen gav **36 index i GPU-bufferten för 36 källindex**, 13 bildavläsningar och största numeriska fel 1,14×10⁻⁶ (tolerans 3×10⁻⁵).

Den äldre breda fragmenttestsuiten stoppade före detta test vid sin textmatchning
för en avvisad shaderkontext (`Unsupported fragment context did not fail with its
specific admission reason`). Den redovisas inte som passerad. `test-resident`
kör därför den befintliga resident-geometriregressionen direkt; övriga assertioner
i den breda sviten har inte försvagats eller tagits bort.
[Första kraschspåret](docs/matfix/mapping-crash.txt) och
[gdb-diagnosen](docs/matfix/mapping-debug-backtrace.txt) finns sparade.

De två observerade krascherna använde cirka 1,33 respektive 1,83 GiB RSS, utan swap.
Det går inte att fastställa att detta orsakade den tidigare Bazzite-frysningen;
det bevisade felet här var en otillåten minnesläsning, inte observerad minnesbrist.

## Verifiering och leverans

- CMake-byggen i `bts-build`, högst `--parallel 4`: PASS. Kontroll av GPU-kodvägar: PASS.
- `bts-asset-tests` och faktisk shelf drag/drop/dubbelklick: PASS.
- Alla 47 materialarkiv, beroenden, kanaler och 256px JL01-miniatyrer: PASS.
- Alla 156 originalkartor: API-storlek, MD5 och SHA-256 verifierade. Texturdata: **246 261 776 byte**. Ingen bild omkomprimerad.
- Riktad 128px GPU-geometritest: PASS, 36 faktiska index för 36 källindex, korrekt bildresultat, cache, Undo och GPU-tillstånd.
- Unload/reload med 32/128px-fixturer: PASS; native-bitar, källgraf, delade resurser och samtliga Undo/Redo-pixelresultat bevaras.
- Fyrmaterialssekvens efter indexrättningen, 128px: PASS för Gold, Denim, Carbon Fibre och Cherry Wood, UV/triplanar och packad spara/öppna.
- AppImage-bygge med två komprimeringstrådar: PASS. **253 biblioteksfiler** verifierade byteidentiska direkt i den körande AppImagens extraherade katalog. Även JL01 och notices stämmer. [Paketbevis](docs/matfix/package-validation.json).

AppImage: **691,694,072 byte**, SHA-256 `516e0a1e16ceff6f08ed039c72ed5dd64a7a5751aca00df099b8e3fe1bfa602b`. Det är 5 128 192 byte mindre än föregående leverans.

De fem materialens 1K-testfall passerade: Cherry Wood, Wool, Rough Iron, Raw Iron och Rough Steel. Varje fall verifierar dialogens 1024-standard, faktisk GPU-bild och sampler på 1024×1024, ändliga GPU-värden, UV/triplanar samt spara/öppna med identiska källbytes och parametrar. De tre metallernas [faktiska GPU-prov](docs/matfix/metal-gpu-samples.json) är olika, och de slutliga viewportbilderna har granskats visuellt.

Efter den sista datajusteringen av Rough Steel byggdes AppImagen om och både Rough Steel och Cherry Wood kördes om. Wool, Rough Iron och Raw Iron använder byteidentiska materialarkiv och samma byteidentiska programbinär som vid sina passerade körningar; detta kontrollerades mot det slutliga paketet. [Exakt testproveniens](docs/matfix/ui-test-provenance.json).

| Slutlig kontroll | Tid | Högsta RSS (GiB) | Swap-operationer |
| --- | ---: | ---: | ---: |
| Resident GPU-geometri, 128px | 0:49.19 | 1.13 | 0 |
| Unload, 32/128px | 0:04.32 | 0.46 | 0 |
| Fyra material, 128px | 2:15.54 | 1.85 | 0 |
| AppImage-komprimering | 0:13.12 | 1.91 | 0 |
| AppImage: Cherry Wood, 1K | 1:37.87 | 1.91 | 0 |
| AppImage: Wool, 1K | 1:41.49 | 1.90 | 0 |
| AppImage: Rough Iron, 1K | 1:45.70 | 1.89 | 0 |
| AppImage: Raw Iron, 1K | 1:46.08 | 1.91 | 0 |
| AppImage: Rough Steel, 1K | 1:47.86 | 1.93 | 0 |

[Resursmätningar](docs/matfix/resource-usage.json). Alla GUI/GPU-körningar och Blender-renderingar skedde headless med `xvfb-run` i **bts-build**, utan synliga fönster. Inga tunga jobb överlappade. Byggen använde högst fyra trådar, Blender fyra och AppImage-komprimering två. Inga 4K-/VRAM-stresstester kördes. Det första importförsöket stoppades korrekt av sessionsskyddet eftersom den privata konfigurationssökvägen saknade testnamnet; den rättades och importen passerade i en verifierad privat Xvfb-session.

Leveranser:

- `~/mpainter-work/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`, med uppdaterad `.sha256` och `SHA256SUMS`.
- `~/mpainter-work/matfix.png`: faktisk Xvfb-fångst från slutliga AppImagen, Cherry Wood i 1K efter återöppning.
- `~/mpainter-work/matfix-contact-sheet.jpg`: samtliga 47 faktiska inbäddade materialminiatyrer.
- `~/mpainter-work/REPORT-matfix.md`: denna rapport.

[Leveransfilernas storlekar och SHA-256](docs/matfix/delivery-files.json). Källändringar och granskningsunderlag samlas i en enda lokal commit på `main`. Ingen push.


## Reproduktion

Se [bibliotekets README](assets/materials/polyhaven/README.md) för nedladdning och selektiv regenerering. Alla GUI-, GPU- och Blender-körningar ska ske i `bts-build` under `xvfb-run`; hjälpskriptet ordnar Xvfb och isolerad testsession.

```sh
distrobox enter bts-build -- env \
  APPIMAGE_EXTRACT_AND_RUN=1 \
  BTS_HOST_EXECUTABLE=/mnt/mpainter-phase1/dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage \
  BTS_POLYHAVEN_OUTPUT=/mnt/mpainter-phase1/build/matfix/repeat-cherry \
  bash /mnt/mpainter-phase1/mpainter/scripts/polyhaven/run-headless.sh test
python3 scripts/polyhaven/verify.py --library assets/materials/polyhaven \
  --ui ../build/matfix/repeat-cherry --output ../build/matfix/repeat-validation.json
python3 scripts/polyhaven/contact-sheet.py --library assets/materials/polyhaven \
  --output ../matfix-contact-sheet.jpg
```

För Wool: lägg till `BTS_POLYHAVEN_DETAIL='Wool'` och använd en annan outputkatalog. Kör tester och AppImage-byggen sekventiellt.
