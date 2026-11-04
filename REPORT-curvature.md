Curvature, AO och thickness — 2026-11-04.

Grundfelet var att curvature målade band runt polygonernas dihedralvinklar, följt av percentilskalning och clamp. Bake använde också originalmeshen i stället för utvärderad subdivision, den aktiva displacement-kanalen saknades i anropsvägarna, och subdividerade hörn återanvände cage-vertex-ID:n. Rasterpunkter och strålgeometri kunde därför hamna på olika ytor. AO använde avståndsmedelvärden; thickness fick dessutom separat min/max-normalisering per tile. Floatlagring fanns redan — någon generell 8-bitarskvantisering hittades inte.

Nu används utvärderad subdivision, aktiv signerad height/displacement och omräknade smooth normals. Samma yta rasteriseras och strålspåras. Curvature skattas från normalernas förändring i world space, med areaviktad radieutjämning och svetsning över UV-gränser inom varje objekt. Radie och styrka är inställbara; valfri tangent-normal-map-detalj räknas på samma yta. Detaljupplösningen följer bakmeshen, så mycket fina normaldetaljer kräver tillräcklig subdivision. Pointiness och detta krökningsestimat har olika skalning och är inte numeriskt identiska.

AO/thickness har 256 samples som standard, cosine-viktad stratifiering, separata maxavstånd i world space, geometrisk ray-offset med flyttalsskydd och skaloberoende triangelträffar. Thickness använder inåtriktade strålar. OIDN är på som standard, kör CPU med högst sex trådar och överlappande arbetstiles. Hela beräkningen och kartlagringen är float; ingen efterföljande min/max-normalisering eller clamp används. OIDN kan ge mindre över-/undersvängning kring skarpa övergångar.

Headless jämförelse mot referensbyggets Blender Cycles på exakt exporterade, displacerade trianglar: subdividerad sfär med height och `Shader_Ball_JL_01`, 256×256, 256 samples. Medelabsolutfel mot Blender, före → efter:

| Mesh | Curvature | AO | Thickness |
|---|---:|---:|---:|
| Displacerad sfär | 0,05359 → 0,00836 | 0,40873 → 0,00083 | 0,42509 → 0,00201 |
| Shader Ball | 0,10915 → 0,06451 | 0,07397 → 0,02225 | 0,45184 → 0,04460 |

Curvature-bilden använder samma fasta 3× visningsförstärkning runt 0,5 i alla tre kolumner. Mätningarna använder råa floatvärden. Bilder: `../shots/curvature-compare.png`, `../shots/ao-compare.png`, `../shots/thickness-compare.png`. Före-data bevarades från ursprungsbygget `41d2be9`; rådata/EXR och körloggar finns i `../build/curvature/` respektive `../logs/curvature-*`.

Analytiska krökningstester, ray-offset/skalning, kanalinfångning, sparade inställningar, normaldetalj, CPU/GPU och unload/reload passerar. Unload-testet använder högst 128×128. Största CPU/GPU-avvikelsen för AO/thickness är 0,00594. Detaljer och checksummor finns i [docs/curvature/validation.json](docs/curvature/validation.json). Kör `scripts/host/test-surface-bakes-headless.sh` i `bts-build`; `BTS_SURFACE_GPU=1` väljer privat Xvfb/offscreen-GPU och `BTS_SURFACE_REGRESSION=1` inkluderar de små produktionsproven. Blender-/bildskripten heter `compare-surface-bakes.py`, `render-surface-bake-comparison.py` och `assemble-surface-bake-comparison.py` i samma katalog.

Thickness följer [Adobes dokumenterade inward-AO-beteende](https://experienceleague.adobe.com/en/docs/substance-3d/bakers/bakers-settings/thickness-map-from-mesh). Bildreferensen är [Cycles AO med Inside](https://docs.blender.org/manual/en/5.0/render/shader_nodes/input/ao.html), inte en körning av Substance. Referensrepon är endast lästa. Alla prov kördes utan synliga fönster, sekventiellt; bygg använde högst `--parallel 6`. Leverans: `../dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`. En lokal commit på `main`, ingen push.

Den färdiga AppImagen är provkörd med samma produktionsprov och offscreen RTX 3090. Curvature är bitexakt mot CPU-bygget; AO/thickness ligger inom ovanstående GPU-tolerans. Ren avslutning verifierad. SHA-256: `a9a603060cdadd1fd16ba71a844b14e2ffb2fea2ec617ad8b05b91e4d37e6a95`.
