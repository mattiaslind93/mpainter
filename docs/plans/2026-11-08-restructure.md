# mPainter omstrukturering

Plan godkänd av Mattias 8 november 2026. Den gäller mPainter 0.9.2 och framåt.

Planen delar mPainter i tre lager: Blender, Qt och vår egen kod. Den låser main och Gumroad bakom grindar. Den låter Coder och workers bygga och testa alla tre plattformar från samma commit. Målet är att kunderna alltid får en testad app, att en människa kan ta över koden, och att en ny Blender-version kan bytas in med så lite arbete som möjligt.

Hur Coder ska arbeta står i [docs/agents/coder.md](../agents/coder.md). Den här filen säger vad som ska göras och i vilken ordning.

## Läget idag

| Fynd | Värde |
|---|---|
| Filer i `native/host/`, alla i en platt mapp | 1 017 |
| Andel av appkoden som är test och demo | cirka 28 % (255 filer) |
| CI-byggen som lyckas | 0 av 3 |
| Separata källträd | 3 (main, Windows-korsbygget, Mac-trädet) |
| Blender-filer som patch 0001 ändrar | 67, cirka 1 800 rader, plus 13 nya headers |
| Filer i `native/host/` som inkluderar Blender-headers direkt | 158 (101 inkluderar också Qt) |
| Skydd på main | inget, och inga environments |
| Formatfil (`.clang-format`) | saknas. `FillPanel.cc` har 89 rader längre än 200 tecken. |

Testet av Windows-bygget bf17b142 är klart. Bygget gick igenom, men `--cpu-workspace-shutdown-test` är rött på Linux och på Windows under Wine. Windows-paketet saknar Qt:s TLS-plugin, så HTTPS fungerar inte där.

## Tre lager

Lagren ligger i ett repo, inte i tre. Med flera repon måste versionerna hållas i takt för hand, och det var ett sådant glapp som gav 0.9.0-alpha i 0.9.2-binären.

| Lager | Var | Ändras | Gräns |
|---|---|---|---|
| Blender 5.2.0 | Hämtas från `fbe6228` av `scripts/prepare-blender.py` och låses | Bara via numrerade patchar i `third_party/blender/patches/` | Bara `native/host/blender/` får inkludera Blender-headers |
| Qt 6.8.3 | Installeras med aqt och låses | Aldrig. En uppgradering är en ny version plus hela testsviten. | `QtWidgets` bara i `ui/`. QtCore-typer får användas överallt. |
| mPainter | `native/`, `scripts/`, `packaging/`, `assets/` | Vanliga PR:er | En mapp per domän med egen `CMakeLists.txt`, som Blenders moduler |

Det som behövs för att en Blender-uppgradering ska bli ett avgränsat jobb:

- `mp check` stoppar Blender-includes utanför `native/host/blender/` (fas 5).
- Patch 0001 delas upp efter syfte. Varje patch börjar med en rad om varför den behövs och om den kan skickas upp till Blender (fas 6).
- `mp check` räknar ändrade Blender-filer, så att siffran bara kan gå nedåt (fas 6).
- `docs/blender-surface.md` genereras ur alla includes och visar vilka Blender-moduler och headers som används (fas 6).
- `mp blender bump <commit>` hämtar den nya versionen, applicerar patcharna, säger vilka som inte gick, bygger och kör funktionstestet på alla tre plattformar (fas 6).

Helt automatiskt blir det inte, eftersom Blender ändrar sitt interna C++-API mellan releaser.

## Roller och grindar

Alla agenter använder Mattias GitHub-konto (beslut 8 nov). GitHub kan därför inte skilja Mattias från agenterna, så två grindar hålls av regler och hookar i stället för av GitHub. Tabellen säger vilka.

| Roll | Får | Får inte | Vad som håller det |
|---|---|---|---|
| Mattias | Har sista ordet. Godkänner varje Gumroad-release, externa bidrag och ändringar i regler och grindar. | | Enda godkännare i environment `release` |
| Coder (GrokBot) | Skriver uppdrag, startar workers på Bazzite och Mac Studion, mergar till main när alla kontroller är gröna, mergar en ändring i regler och grindar när Mattias har godkänt just den ändringen, taggar en release | Släppa till Gumroad. Merga externa bidrag. Merga en ändring i regler och grindar som Mattias inte har godkänt. | `author-gate` för externa PR:er. Resten hålls av Coders instruktioner. |
| Worker (Claude eller Codex) | En plattform, en worktree, en branch. Pushar sin branch och öppnar PR. | Merga PR:er, pusha main, tagga, ladda upp, ändra i den andra plattformens mappar | Egen användare på maskinen, deny-hookar för Claude, briefen för Codex |
| Extern bidragsgivare | Öppna PR från en fork | Köra kod på Bazzite eller Mac Studion. Bli mergad utan Mattias godkännande. | `author-gate`. Forkar når aldrig self-hosted runners. |

### Merge till main

Bara Coder mergar. En worker mergar aldrig en PR.

- PR:ens senaste commit har gröna kontroller: `checks`, plus `build` och `features` för Linux, Windows och macOS. Fram till fas 3 bygger main inte på Mac, så då krävs Linux och Windows.
- Branchen är uppdaterad mot main, så att det som testades är exakt det som mergas.
- `author-gate` är grön. Den godkänner en PR från Mattias konto. En PR från någon annan kräver Mattias review på senaste commiten. Grinden körs som `pull_request_target` och läser ingen kod från PR:en.
- En PR som rör regler och grindar (`.github/`, `.claude/`, `AGENTS.md`, `CLAUDE.md`, `scripts/release/`, `docs/agents/`) kräver att Mattias har godkänt just den ändringen. Därefter mergar Coder den när alla andra villkor här är uppfyllda. Godkännandet ersätter inget av dem.
- Ingen force-push och ingen radering av main.
- Ett externt bidrag som Mattias har godkänt läggs av Coder på en branch i repot, så att plattformstesterna kan köras.

Ett undantag finns. Den 8 november 2026 kl. 15:51 CET godkände Mattias att PR #2 och PR #3 (planen och agentdokumenten) mergas utan plattformskontrollerna `build` och `features`. För just de två PR:erna ersätts plattformskontrollerna av dokumentationsvalidering och en verifierare från den andra modellfamiljen på senaste head-SHA. Alla andra villkor gäller, också hans godkännande av ändringen i regler och grindar och GitHubs skydd av main. Undantaget gäller ingen annan PR, inte heller andra PR:er med bara dokumentation.

### Konton och maskiner

- Ett konto för allt: Mattias.
- Agenterna får en finkornig token för kontot, utan rätt till Actions, environments och repo-inställningar. Om den räcker för att stoppa godkännande av releaser verifieras när den sätts upp.
- Gumroad-token flyttas till environment `release` som secret och tas bort från disken.
- Workers och runners körs som en egen användare på Bazzite och Mac Studion, så att de inte når Mattias filer.

## Kontakt mellan agenterna

En gemensam `agent_chat.md` tappar rader när flera agenter på två maskiner skriver i den. I stället får varje uppdrag en egen mapp utanför repot, på maskinen där workern kör. Varje fil har en enda skrivare.

```
~/mpainter-work/runs/<datum>-<ämne>/
  brief.md        Coder skriver före start, efter docs/agents/brief.md
  to-worker.md    Coder lägger till numrerade meddelanden
  from-worker.md  Workern skriver status, frågor och "läst till och med #n"
  report.md       Workern skriver sist, efter docs/agents/report.md
```

- Claude-workers ser nya meddelanden genom en hook i repots `.claude/settings.json` efter varje verktygsanrop. Det verifieras när hooken byggs.
- Codex-workers läser `to-worker.md` före varje steg och innan de rapporterar klart. Det står i briefen.
- Beslut som ska bestå skrivs i PR-beskrivningen, inte i meddelandena.
- Mattias och Coder pratar i GrokBot-appen. Godkännanden görs i GitHub.
- Tunga jobb tar ett lås per maskin med `flock` (bygge, VM, GPU) och körs med minnestak via `systemd-run`. Det ersätter dagens `pgrep`-loopar.

## Bygg, test och release

1. `mp version bump 0.9.3`. Versionen finns bara i `pyproject.toml`. Allt annat läser därifrån. `CHANGELOG.md` får en ny rubrik.
2. `mp check`, snabba grindar vid varje commit och i CI:
   - ingen hårdkodad version utanför `pyproject.toml`
   - inga `QUuid = {}`, inga sökvägar med `\` till CMake, inga `shared_ptr::unique()`
   - produktionskod inkluderar aldrig harness- eller testheaders
   - clang-format, och Blender-headers bara i `native/host/blender/`
   - antalet ändrade Blender-filer ökar inte utan en ny patch, och `docs/blender-surface.md` är aktuell
   - varje sökväg och kommando i en `AGENTS.md` finns
   - varning för opushade commits
3. `mp build <plattform>` och `mp package <plattform>`. Linux och Windows-korsbygget på Bazzite, macOS på Mac Studion. Byggmiljöerna går att köra om från vilket läge som helst.
4. `mp test features <plattform>`. Samma lista i `scripts/verify/features.toml` på alla tre plattformar, med en skärmdump per steg. Windows körs i VM:en med emulerad grafik, eftersom det inte finns någon riktig Windows-dator. Mac kör också `spctl`, `stapler validate` och start från en nedladdad DMG.
5. Merge till main enligt reglerna ovan.
6. `mp release check`. Läser `manifests/linux.json`, `windows.json` och `macos.json`. Varje plattform skriver bara sin egen fil. Version, commit och sha256 måste stämma, och paketet måste vara samma fil som testades. Ett rött test stoppar releasen, utom om det står i `scripts/verify/quarantine.toml` med Mattias godkännande.
7. `mp release draft`. Taggen pekar på commiten som alla tre paket byggdes från. Utkastet har ändringslistan och skärmdumpar per plattform: appen startad med versionen synlig, och varje ändrad funktion. Coder skickar länken till Mattias i GrokBot-appen.
8. Mattias godkänner i environment `release`.
9. `mp release publish` körs i CI. Den laddar upp exakt de testade filerna till Gumroad och publicerar releasen. En push till main skickar aldrig något till Gumroad.

## Mappstruktur och agentfiler

Bara åtta mappar får en egen `AGENTS.md`, där det finns regler som gäller just där. Övriga mappar beskrivs som ett avsnitt i närmaste `AGENTS.md` och i filhuvudet i varje fil. `CLAUDE.md` innehåller bara raden `@AGENTS.md`, så Claude och Codex läser samma text. Ingen symlänk, eftersom symlänkar kan bli vanliga textfiler vid checkout på Windows.

| Mapp med egen AGENTS.md | Innehåll |
|---|---|
| `/` | Vad mPainter är, hårda regler, karta, vanliga kommandon, roller |
| `native/host/` | Arkitektur på en sida, domänkarta med ett avsnitt per undermapp, hur man lägger till en fil, lager |
| `native/harness/` | Vad harness är, register över `BTS_*`-variablerna (fas 4) |
| `native/tests/` | Hur man lägger till och kör tester, röda tester och karantän |
| `scripts/` | Tabell över `mp`-kommandon, regler för skript |
| `scripts/platforms/windows-cross/` | Stegen i ordning, resursgränser, licensregler för Microsofts filer |
| `scripts/platforms/macos/` | Signering och notarisering (fas 3) |
| `third_party/` | Patchregler och uppgradering av Blender |

Målstrukturen för `native/host/` är domänmappar: `app/`, `platform/`, `blender/`, `document/`, `layers/`, `painting/`, `fill/`, `gpu/`, `baking/`, `materials/` (med `native/`, `layering/`, `openpbr/`, `common/`), `assets/`, `viewport/`, `io/`, `persistence/`, `spsm-import/`, `python/`, `ui/` (med `shell/`, `panels/`, `dialogs/`, `widgets/`, `theme/`) och `diagnostics/`.

Andra nya filer och mappar:

- `.clang-format` med Blenders format (radlängd 99), och `.git-blame-ignore-revs`
- `.claude/settings.json` med deny-hookar för Claude-workers och hooken för meddelanden
- `docs/agents/` med `coder.md` (Coders arbetssätt och rollerna), `brief.md` och `report.md`
- `docs/contributing.md`, `docs/architecture/`, `docs/building/`, `docs/platforms/`, `docs/features/`, `docs/history/`, `docs/decisions/`
- `scripts/mp.py`, `scripts/verify/features.toml`, `scripts/verify/quarantine.toml`
- `.github/pull_request_template.md` och arbetsflödena `checks.yml`, `author-gate.yml`, `build.yml`, `features.yml`, `release.yml`

## Hårda regler

Står först i rotens `AGENTS.md`.

1. Versionen finns bara i `pyproject.toml`.
2. Alla plattformar byggs från samma commit på main. Inga långlivade plattformsgrenar.
3. En funktion är klar när den bygger och klarar funktionstestet på alla tre plattformar.
4. Ändringar i Blender görs bara som numrerade patchar, som gäller alla plattformar.
5. Ingen fix lämnas lokalt. Pusha till en branch och öppna en PR samma dag.
6. Produktionskod inkluderar aldrig harness-kod.
7. Bara `native/host/blender/` inkluderar Blender-headers.
8. Inget når main utan gröna kontroller på alla plattformar. Enda undantaget är PR #2 och PR #3, se Merge till main.
9. Inget når Gumroad utan Mattias godkännande.

## Kod och git

Detaljerna skrivs i `docs/contributing.md` i fas 1.

- Conventional Commits, `type(scope): subject`. Små commits som var och en går att landa ensam.
- En agent, en worktree, en kortlivad branch från main.
- PR-beskrivningen har rubrikerna Why, What changed, Scope, Tradeoffs, Blast Radius och Verification. Verification nämner riktiga körningar och deras utfall.
- Kommentarer bara för ett varför som koden inte kan visa. Varje fil börjar med SPDX-licensraden och ett filhuvud, som i Blender.
- Lås beteendet före en flytt, ta bort innan du lägger till, flytta alla anropare och radera det gamla i samma omgång, och låt ett skript göra flytten.
- Formatering: Blenders `.clang-format` i en commit som bara formaterar. `SortIncludes` av i första vändan, eftersom 4 087 includes i ett platt träd kan ändra kompileringen.

## Windows-lärdomar

| Problem | Orsak | Skydd | Status |
|---|---|---|---|
| Kompilering stoppar på `uuid = {}` | Qt på Windows gör `{}` tvetydigt | `QUuid()` och `check-windows-uuid-resets.py` | Grind finns |
| Kraschar vid start | `tbbmalloc_proxy` byter ut CRT:ns malloc | Blender-patch 0003 | Fixat |
| CI-installationen faller efter 3 timmar | `build.py` skickar `D:\a\...` till CMake | `.as_posix()` och en grind | Ej åtgärdat |
| Paketet fick fel version | `package.py` hade versionen hårdkodad | Läser `pyproject.toml` | Fixat i PR #1 |
| Mesa-DLL:er kan följa med | VM:en använder Mesa för test | Ett gemensamt paketeringsskript | Halvt |
| Inget HTTPS i paketet | Qt:s TLS-plugin saknas | Ta med `qschannelbackend.dll` | Ej åtgärdat |
| Shutdown-testet rött | `workspace_shutdown_test.cc:54` | Rött test stoppar release | Ej åtgärdat |
| Byggen som bara fungerade på en maskin | `prepare-source.sh` stoppar på ett förberett träd | Byggsteg som går att köra om | Fas 2 |
| Wine är inte Windows | Ingen riktig Windows-dator | Funktionstest i VM:en, känd begränsning i release-texten | Begränsning |

## Mac

Mac-trädet ligger på Mac Studion i `~/mpainter-build/mpainter`, branch `mac-port`, revision `29956e9`. MacOS-patcharna heter `0002–0007-macos-*.patch` och har totalt 79 tillagda rader. De numreras om till 0004–0009 i main, eftersom 0002 och 0003 redan är Windows-patchar där.

`docs/platforms/macos.md` skrivs från Mac-trädet och ska ta med:

- certifikatet Developer ID Application, team-ID och var det ligger
- signering inifrån och ut utan `--deep`, hardened runtime och en incheckad `entitlements.plist`
- `xcrun notarytool submit --wait` och `xcrun stapler staple` på både .app och DMG
- verifiering med `codesign --verify --strict` och `spctl`, och ett test från en nedladdad DMG

Raden i `build.py` som stoppar macOS och instruktionen om `xattr -dr com.apple.quarantine` tas bort.

## Faser

Varje fas är en serie små PR:er. Varje PR klarar kontrollerna innan nästa börjar.

### Fas 0. Grindar och frys

- [ ] Besluta om PR #1 (Windows-fixarna) och merga den
- [ ] Tagga `pre-restructure`
- [ ] Pusha `mac-port` (29956e9) från Mac Studion till GitHub
- [ ] Skydda main: PR krävs, ingen force-push och `author-gate`. Plattformskontrollerna blir krav när de finns.
- [ ] Skapa environment `release` med Mattias som enda godkännare och flytta Gumroad-token dit
- [ ] Finkornig token för agenterna, och en egen användare för workers och runners på varje maskin
- [ ] Rensa `worktree-checkbox-*` och `win-bf17b142` när innehållet är tillvarataget

### Fas 1. Agentfiler, dokumentation och formatering

- [ ] `AGENTS.md` i de mappar som finns, `CLAUDE.md` med `@AGENTS.md`, och `docs/agents/`
- [ ] En kontroll av att sökvägar och kommandon i varje `AGENTS.md` finns
- [ ] `docs/contributing.md` och `.github/pull_request_template.md`
- [ ] Ny `docs/`-struktur. Dela upp `building.md` per plattform och skriv `windows.md` och `macos.md`.
- [ ] `REPORT-*.md` till `docs/history/`, och bestående kunskap till `docs/features/`
- [ ] De lösa filerna i `~/mpainter-work` arkiveras och ersätts av `runs/`
- [ ] clang-format på hela trädet i en commit, och formatkontrollen i CI

### Fas 2. mp, versionsparitet, funktionstest och CI

- [ ] `scripts/mp.py` med doctor, check, build, package, test, version, release och brief
- [ ] `features.toml`, `mp test features` på Linux och i Windows-VM:en, med skärmdumpar, och `quarantine.toml`
- [ ] Byggmiljöer som går att köra om, och lås per maskin med `flock`
- [ ] Manifest per plattform, versionsmallar och `CHANGELOG.md`
- [ ] Slå ihop Windows-paketerarna och ta med TLS-pluginen
- [ ] CI: sökvägsfixen, Bazzite som runner, ccache, `.claude/settings.json` med deny-hookar

### Fas 3. Mac-porten in i main

- [ ] Metal-presentationen bakom plattformsval, och Darwin-stoppet bort
- [ ] MacOS-patcharna in som 0004–0009
- [ ] Signering och notarisering som skript i `scripts/platforms/macos/`, och Mac Studion som runner
- [ ] Första releasen där alla tre byggs och testas från samma commit, godkänd av Mattias med skärmdumpar

### Fas 4. Skilj test- och demokod från appen

- [ ] Flytta 255 harness-filer till `native/harness/`, och bryt ut demogrenarna ur `main()` och `GpuLayerPreview::self_test`
- [ ] CMake-option `MPAINTER_WITH_HARNESS`
- [ ] Register över `BTS_*`-variablerna i `native/harness/AGENTS.md`

### Fas 5. Domänmappar och Blender-gränsen

- [ ] Ett skript gör `git mv`, en domän per commit, och bygger efter varje
- [ ] Undermapparna läggs först till i include-sökvägen
- [ ] Fixa de 8 verify-skript som söker icke-rekursivt och CMake-regler som matchar exakta sökvägar
- [ ] En `CMakeLists.txt` per domänmapp
- [ ] Blender-includes bakom `native/host/blender/`, och `mp check` stoppar nya utanför

### Fas 6. Blender-patcharna och uppgraderingsvägen

- [ ] Dela patch 0001 efter syfte
- [ ] Generera `docs/blender-surface.md` och räkna ändrade Blender-filer
- [ ] `mp blender bump`, provkört mot en nyare Blender-commit på en egen branch

### Fas 7. Kodstädning

- [ ] SPDX-rad och filhuvud i varje fil
- [ ] Dela upp `main.cc` (1,1 MB) i app-start, editor-koppling och menyer
- [ ] Kommentarer bara där logiken inte är självklar

## Beslut och öppna frågor

Beslutat 8 november av Mattias:

- Coder mergar till main när bygget och hela funktionstestet är gröna på alla tre plattformar.
- Varje Gumroad-release kräver Mattias godkännande, med skärmdumpar som visar att appen fungerar och vad som har ändrats.
- Externa bidrag och ändringar i regler och grindar kräver Mattias godkännande.
- Windows testas bara i VM:en.
- Ett repo med tre lager.
- Ett GitHub-konto för allt.
- Coder mergar en ändring i regler och grindar efter att Mattias har godkänt just den ändringen, när alla andra villkor för merge är uppfyllda. Workers mergar aldrig.
- PR #2 och PR #3 får mergas efter dokumentationsvalidering och en verifierare från den andra modellfamiljen, utan plattformsbyggen (kl. 15:51 CET). Det gäller bara de två PR:erna.

Öppna, med rekommendation:

- Var kör Coder, och som vilken användare? Behövs före fas 0.
- Windows byggs primärt med korsbygget från Bazzite. MSVC i CI är en kontroll.
- Harness lämnar releasebygget, utom det som funktionstestet kräver.
- Marknadsföringsvideon och evidensfilerna lämnar git. Historiken behålls.
- Kodnamnet `bts` behålls tills vidare.
- `docs/` skrivs på engelska. Den här planen är på svenska eftersom den är Mattias beslutsunderlag.
