# mPainter 0.9.0 alpha

Ett program för att måla texturer i 3D, byggt på Blender, med lagerstack och import av nodbaserade material. Bygg material med stödda noder i Blender och ta in dem i mPainter som Material Presets. Vissa noder och shaderfunktioner har begränsningar. Importvarningar visar vad som behöver ses över.

**Det här är en alpha.** Den funkar och jag använder den själv, men räkna med buggar och saker som ändras mellan versionerna. Spara ofta. Den rapporterade kraschen vid ombakning av mesh maps är fixad i det testade lokala bygget. En ny AppImage med fixen har ännu inte paketerats. Linux är testat idag. Windows har ingen fungerande version ännu. macOS behöver en viewportport.

**Köper du alphan nu får du alla uppdateringar fram till och med 1.0 gratis.**

## Det här finns i 0.9.0 alpha
- Turquoise Dark gränssnitt med lagerstack, Texture Sets flik och Assets panel
- HDR studioljus som standard, rotera ljuset med Shift + högerdrag
- Texture Sets: bocka av sets eller UV tiles för att gömma dem och avlasta resurser till en förlustfri diskcache. Bocka i för att ladda tillbaka. Delade resurser kan ligga kvar; total RAM och VRAM behöver inte minska
- Bakning av curvature, ambient occlusion och thickness på utvärderad subdivision och height/displacement. Curvature använder Blender Cycles Pointiness-algoritmen på den geometrin, oberoende av normal maps. Detaljnivån följer meshens subdivision; en grov mesh får inte extra ytdetaljer automatiskt. AO och thickness har brusreducering på som standard
- Bakningens 4K och 8K kodvägar klarade tidigare riktade tester. Minnesåtgång beror på mesh, kartor och lager; det är ingen garanti för stora hela projekt
- GPU assisterad bakning av AO och thickness med CPU fallback. Cycles rendering väljer OptiX, sedan CUDA, sedan CPU om en kompatibel GPU saknas
- 10 Mask Generators: Crease Dirt, Curvature edges, Edge Wear, Ground Dirt, Leaks & Stains, Moss, Position height, Rust, Scratches och Surface Dust
- 47 inbyggda material: 30 baserade på CC0 texturer från Poly Haven och 17 egna procedurmaterial, med UV och Triplanar. Medföljande texturkartor är 1K
- Blender material med stödda noder som Material Presets, och spara en lagermapp som preset via högerklick
- Shader_Ball_JL_01 inbyggd som preview och projektmesh
- Bitmap penslar, import av normal maps och 8K float height EXR

## Pris
- Alpha: FÖRSLAG 29 USD (gratis uppdateringar till 1.0)
- 1.0: FÖRSLAG 59 USD

## Systemkrav
- **Linux (testat):** x86_64, kompatibel glibc baserad distro, X11 eller XWayland och GPU med OpenGL 4.3. Den lokala AppImagen är byggd på Fedora 43 och testad på Bazzite. Äldre distros är inte verifierade. glibc och grafikdrivrutin ingår inte
- **GPU:** NVIDIA RTX 3090 är testad. Cycles kan använda OptiX eller CUDA med kompatibel NVIDIA drivrutin. GPU stödet varierar mellan bakändar
- **Minne och disk:** inga verifierade minimikrav för RAM eller VRAM ännu. 8K kartor kan ta mycket minne. Räkna med cirka 660 MiB för AppImagen, cirka 2,1 GiB uppackad, plus utrymme för projekt, cache och temporära filer
- **Windows:** endast experimentellt byggarbete. Inget fungerande programpaket eller verifierade systemkrav ännu
- **macOS:** inget fungerande bygge. Nuvarande viewport kräver OpenGL 4.3 som macOS saknar. En Metal integration för viewporten behövs och är inte implementerad

## Vanliga frågor
**Är den stabil?** Nej, det är en alpha. Den går att jobba i men spara ofta och behåll säkerhetskopior.
**Får jag 1.0?** Ja, gratis för alla som köper alphan.
**Kan jag använda mina Blender material?** Material med stödda noder kan importeras som Material Presets. Alla noder och shaderfunktioner återges inte. Vanlig materialimport återskapar inte geometrisk displacement från källgrafen.
**Öppnar den filer från andra texturprogram?** Den importerar normal och height maps. Den öppnar eller renderar inte .sbsar arkiv. Det separata metadataverktyget läser bara redan extraherad XML med beskrivningsdata.
**Vilket grafikkort behövs?** Viewporten kräver OpenGL 4.3. Testkortet NVIDIA RTX 3090 stöder Cycles OptiX och CUDA. Bakning av mesh maps och Cycles har CPU fallback; viewporten behöver fortfarande en kompatibel GPU.
**Återbetalning?** (Mattias bestämmer.)

## Licens och credits
- mPainter bygger på Blender och släpps under **GNU GPL v3 eller senare**. Du får använda, ändra och dela programmet enligt GPL. Det du betalar för är det färdigbyggda programmet, uppdateringar och att stötta arbetet. Matchande källkodspaket för denna alpha behöver fortfarande förberedas före release
- Material: texturer från [Poly Haven](https://polyhaven.com), CC0, plus egna CC0 procedurmaterial
- Default Studio HDR: skapad i Blender av Mattias Lind. Blenders valbara studio HDRI:er: CC0, Greg Zaal / Poly Haven
- [Shader Ball JL 01](https://sketchfab.com/3d-models/shader-ball-jl-01-9ecc1d8ddb5b4059a80d0bd925e3dd3b) av JLRIN / Craneo (CRANEO_), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Konverterad till upprätt OBJ och använd för härledda previewkartor
- Grunge underlag från ambientCG, CC0
- Blender är ett varumärke som tillhör Blender Foundation. mPainter är inte knutet till eller godkänt av Blender Foundation
