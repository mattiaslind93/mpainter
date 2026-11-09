# HDR-rotation — 2026-11-04

Orsak: mfPainter sätter `studiolight_rotate_z = 1.5707963705062866` (+90°) i `native/host/NewProjectProfile.inc:7`. mPainter hade ändrat värdet till 0°. `NewProjectProfile::apply` aktiverar kamerarelativ belysning i båda. HDR-filerna är byte-identiska trots olika namn: SHA-256 `48505ffc80c0ca026775c77621b5a85a4c44b0fb7819ddadfa9e7bc6f5732031`.

Återställt +90°. Äldre, oversionerade projekt med identifierad standard-HDR och 0° uppgraderas vid öppning. Egna vinklar och World/Local-val bevaras. En sparad versionsmarkering hindrar upprepad migrering och bevarar även ett senare uttryckligen valt 0°.

EEVEE och Cycles behåller referensens Studio Light-transform, utan extra offset eller spegling. Kameravektorn omvandlas till Z-up och roteras +90°; tumnaglarnas motsvarande vektor är `(z,x,y)`, med `u = .5 - atan2(x,z)/(2π)` och top-down `v = acos(y)/π`. Den sparade World-noden har ingen extra Mapping, precis som i referensen. Tumnaglarnas tidigare syntetiska ljus har ersatts med HDR-baserade RGBE-kartor för reflektioner och diffus belysning. Deras BRDF är fortsatt en förenklad förhandsvisning.

HDR-bakgrunden är osynlig. EEVEE behåller temats grå gradient. Cycles kamerabakgrund använder temagrått, inverterat genom aktuell displaytransform; verifierat RGB `(114,114,114)`.

Verifiering: endast `distrobox bts-build` + privata `xvfb-run`-displayer, inga synliga fönster. Samma shaderball JL01, kamera, metallmaterial, HDR, ACEScg och 64 Cycles-samples. Referensens befintliga bts-host-objekt länkades med en separat testdriver utanför referensen; referenskällor, objekt och binär ändrades inte. Båda testscenerna har samma World-datablock och neutral kamerafärg, eftersom Cycles ändrar importance sampling när World saknas.

Spara–öppna-testet hittade även ett befintligt fel: en avaktiverad intern normalbilds 1×1 GPU-platshållare ersatte bildens logiska dimensioner vid omsparning. Sparvägen bevarar nu dimensionerna. Fyra spara–öppna-cykler i följd passerar: 90°, äldre 0°→90°, eget 0° och äldre eget 37°/World.

Slutjämförelse på objektets inre pixlar: Cycles **pixelidentisk**; EEVEE standardvy medelavvikelse **0,000046/255**, max **1/255**. World och vriden kamera: medel **0,018/255** respektive **0,005/255**. Tumnaglarnas 24 riktningsprover matchar den oberoende koordinatformeln inom `1,8e-15`.

Leverans relativt `~/mpainter-work/`: `shots/hdrrot-compare.png` (mfPainter vänster, mPainter höger; EEVEE/Cycles), `dist/mPainter-0.9.0-alpha-linux-x86_64.AppImage`. Mätningar finns i `mpainter/docs/hdrrot/`; körloggar i `logs/hdrrot-*`. En lokal commit på `main`, ingen push.

AppImage SHA-256: `5a205e1333eb48489ae74f9063d6273e07fa3fd290eeefc923905994927a3954`. Samtliga 5 788 paketfiler/länkar matchar AppDir utan avvikelser. Slut-AppImagen migrerar äldre 0° till 90°, sparar och återöppnar filen med 90° och ren avslutning.
