# Bakning, import och GPU

## Ändringar

- Minnes-preflight och konfigurerade RAM/VRAM-gränser är borttagna från mesh maps, displacement, UDIM, material, projekt, import och export. Faktiskt tillgängligt minne läses vid körning. Formatgränser, överflödeskontroller och validering av trasiga filer finns kvar.
- Projektets upplösning behålls. Kartor och texture sets behandlas sekventiellt, färdiga kartor och stora arbetsytor använder diskstödda fullupplösningsbuffertar. GPU-raybakning delas efter tillgängligt VRAM och faller tillbaka till CPU. GPU-arbetet sker på rendertråden och tillfälliga buffertar släpps mellan kartorna.
- Före bakning släpps återberäkningsbara GPU-cacher och minnesinformationen uppdateras. Aktiva/uthyrda texturer och målade originaldata behålls. Cachekapaciteten räknar med egna återanvändbara allokeringar. En RGBA32F-textur upptar 256 MiB vid 4K och 1 GiB vid 8K; flera kanaler och lagerprefix kan därför förklara stor VRAM-användning. Den ursprungliga 14,5-GB-situationen i Mattias öppna projekt är inte reproducerad, och ingen läcka i just det projektet är därmed bevisad.
- Cycles byggs med CUDA och OptiX samt medföljande kernels för RTX 3090/sm_86 och PTX. AppRun hittar värdens drivrutin även i distrobox. Inga NVIDIA-drivrutinsbibliotek buntas. Automatvalet är OptiX, sedan CUDA, sedan CPU. Cycles-läget gömmer nu rätt panel efter flytten av Assets till vänster; viewporten behålls synlig. Återöppnade, packade composite-bilder kan åter publiceras till Cycles; avstängda hjälpbilder utesluts.
- Blender-materialimport stöder bildnormaler, Normal Map i tangent/object/world space, Bump och kedjor samt Coat Normal. Enkla tangentnormalbilder blir normalkanal; komplexa träd utvärderas genom det bevarade Blender-nodträdet. Okända kanalnoder ger varning och resten importeras. Coat-noder följer det översta synliga materiallagret; oberoende coat-lober per lager blandas inte.
- Svarta materialthumbnails från borttagna miljöbilder ersätts med appens studioljus. Tio egna procedurala Mask Generators och egna sfärthumbnails finns i katalogen. Inga Adobe-maskfiler medföljer; användarimporten finns kvar.
- UI använder färger ur `assets/theme/Turquoise_Dark.xml`, kopierad från Mattias fil. Lagerpanelens väntetext är begriplig engelska och försvinner när asynkron projektöppning är klar. Se även `REPORT-parity.md`.

## Verifiering

Alla körningar sker i `bts-build`, med `unset WAYLAND_DISPLAY`, privat `xvfb-run -a` och isolerade skrivkataloger. Referensen `/home/mlind/bts` och artistens pågående app lämnas orörda.

- 4K: samtliga 14 kartor (sju roller × UDIM 1001/1002), 3,5 GiB, identiska byte för byte mellan simulerat 16 MiB VRAM och normal körning. 8K: samma kontroll av alla sju kartor, 7 GiB. Undo/redo och CPU-fallback passerar. 8K-referensen behöver två GPU-batcher på grund av drivrutinens SSBO-gräns; den begränsade körningen använder 193.
- Oberoende rayprov: alla 16 777 216 respektive 67 108 864 resultat bitidentiska. Produktionsprovet jämför också GPU mot CPU: största flyttalsfel 1,788e-7; vid noll VRAM är CPU-fallback bitidentisk. Avbrytning passerar.
- Cacheprov: tre återuppbyggnader/frigöringar återgår till samma lediga VRAM, cirka 34 MiB frigörs varje gång. Målade original och bildhash behålls.
- `height_1001.exr`, 8192², 32-bit float: alla 67 108 864 värden bevarade exakt. Den riktiga displacement-kanalen skapas och bilden binds vid simulerat 9 GiB ledigt RAM och en byte VRAM; tolv kompositionsprov är exakta. Den gamla 50-procentsregeln hade blockerat detta fall.
- Tio generatorer passerar CPU-test av sfär, invertering, Fill/Folder och codec samt GPU-rendering. Alla tio syns samtidigt i Assets > Mask Generators i ett separat UI-prov; dubbelklick lägger till generatorn i lagerstacken.
- Turquoise Dark har jämförts visuellt med referensbilden. Skärmbildens canvas, panel, knapp, inmatning, accent, menyrad, avdelare och scrollbar innehåller exakt XML-temats RGB-värden.
- Slutlig AppImage: CUDA och OptiX identifierar RTX 3090, väljer GPU och renderar 64²/8 samples med identiska RGBA-pixlar. Inga `libcuda`, `libnvoptix`, NVIDIA-drivrutiner eller `.spmsk` finns i paketet; elva Cycles-kernels och OptiX-licenser medföljer. Samma AppImage öppnar det sparade Coat-projektet, visar alla tio generatorer, når `Cycles / OPTIX — Rendering Done` och återgår till Painting med Assets återställd.

Bilder och JSON finns i `docs/parity/validation`. Materialprovet omfattar tolv fall, inklusive de fyra begärda Blender-jämförelserna, Object/World Normal, annat BSDF, okända noder, sparning/återöppning av Coat och Brand Test. Snabba materialbyten passerar separat. Ljussättning/kamera skiljer mellan referens och app; ingen pixelidentisk renderparitet påstås. Den återställda CPU-sviten passerar samtliga 231 kontroller (200 native-testprogram och 31 ytterligare kontroller); efter sista Cycles-fixen passerar tre berörda kontroller igen. Slutlig AppImage-kontrollsumma anges i `docs/parity/validation/appimage.json`.
