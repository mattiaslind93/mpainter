# REPORT-checkbox — 2026-11-04

Temafixen, flikflytten och HDR-gesten är implementerade. Tillägg 1 har fungerande avlastning för vanliga målnings-/bitmapdata, men fullständig avlastning av alla resurstyper är **inte uppfylld**; se begränsningen nedan.

- Checkboxar, radioknappar och toggles har synliga av/på-, hover- och disabled-lägen. Bakgrund/kant följer `wcol_option` (`#666666`/`#373737`); bocken använder XML-temats turkosa accent `#22947D` enligt önskemålet. XML:s ursprungliga `item` är vitt. Qt-menyer, listor, Fill-panelens lokala stilar och egenritade lagerrader är inkluderade.
- Högerpanelen har **Layers | Texture Sets**. Set och UV-tiles har checkboxar; dolda mål spärras för målning/bakning. Valen sparas i projektet. Exportkällan behåller samtliga tiles och deras ursprungliga upplösning.
- Avlastning diskcachar avkodade bitmappar förlustfritt, släpper residenta målade sidor/bakade kartor och rensar återbyggbara GPU-cachebilder. Dolda UDIM tar ingen fullstor array-plats. Cachefel behåller data. Sparning, öppning och export följs av ny avlastning.
- **Shift + högerdrag** roterar HDR 0,5°/pixel, synkar båda rotationskontrollerna och sparas i projektet. Kamera/modell står still; bakgrunden förblir dold. Vanligt högerdrag ändrar inte HDR.

Verifierat sekventiellt i `bts-build` med privat `xvfb-run`, även mot färdig AppImage: `bts-theme-tests`, `bts-environment-rotation-gesture-tests`, `bts-texture-set-residency-tests` och GUI-kedjan i `scripts/host/test-checkbox-headless.sh`. GUI-kedjan provar klick, unload/reload, oförändrade pixeldata, EEVEE/Cycles-interaktion, reglage, sparning/öppning och exportens tile-urval. Texturer: högst 128×128 i avlastningstesterna. Bygg: högst `--parallel 6`; inga samtidiga tunga tester/paketeringar, inga synliga fönster.

Mätning med ett set och en tile avlastade: målade sidors RSS **786 432 → 0 byte**, materialens registrerade GPU-bildallokeringar **3 145 680 → 1 223 368 byte**. Detta är inte hela processens/rendererens minne. Bevis: `docs/checkbox/validation.json`. Skärmdumpar: `../shots/checkbox.png` och `../shots/texturesets-tab.png`.

**Begränsning:** native-materialbibliotek och GPU-resurser som hålls av undo/adjustment kan fortfarande vara residenta. Avlastning av godtyckliga icke-redigerbara importerade shadernätverk är inte implementerad. Fullständig RAM-/VRAM-frigöring för samtliga set-/lagertyper kan därför inte utlovas. Dessa resurser har behållits för att inte riskera dataförlust.

AppImage: `../dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage` (testad). SHA-256: `ca5b020053a48d13e0df11a5f6c1f072adb79b037125007a082683c1c124e8a8`. Ändringarna samlas i en lokal commit på `main`; ingen push.
