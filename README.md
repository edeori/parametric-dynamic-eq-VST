# parametric-dynamic-eq-VST

## Match EQ

A spektrum feletti Match EQ panellel egy referenciasáv átlagos hangszínéhez
illeszthetsz egy másik sávot:

1. A referenciasávon nyomd meg a **Learn Reference** gombot, és játssz le egy
   jellemző részletet. **Stop & Keep** lezárja a tanulást; 12 másodpercnyi
   értékelhető hang után automatikusan lezárul. Legalább 1 másodperc szükséges,
   a csendet figyelmen kívül hagyja. Hosszabb, reprezentatív részletet érdemes használni.
2. A pluginpéldányt mozgasd/másold a célsávra. A referencia a plugin állapotával
   együtt megmarad. Másik példányhoz vagy projekthez használható a **Save Ref...** /
   **Load Ref...** (`.pdeqref`) is.
3. A célsávon indítsd el a **Learn Target** tanulást, majd állítsd le, vagy várd
   meg az automatikus befejezést. Mindkét tanulás a plugin EQ előtti bemenetét méri.
4. Az **Apply Match** a jelenlegi EQ-sávokat 16 szerkeszthető, statikus sztereó
   haranggörbére cseréli. Az **Undo Match** az utolsó alkalmazás előtti sávokat
   állítja vissza (beleértve a Solo/Mute/Dynamic beállításokat). Ez az egyszeri
   visszaállítás az adott pluginpéldány életében érhető el, projektből nem töltődik vissza.

**Amount**: az illesztés erőssége, 0–100%. **Smoothing**: spektrális simítás,
0,15–2 oktáv; nagyobb érték szélesebb korrekciókat ad. **Max +/-**: az összesített
korrekció emelési/vágási korlátja, 1–18 dB. A beállítások változtatása után ismét
nyomd meg az **Apply Match** gombot. A kézi sávszerkesztéseket ez ismét lecseréli.

A tanulás és a beállítások DAW-projektbe mentődnek. A tanulás az editor bezárása
után is folytatódik; félkész tanulás helyett a legutóbbi kész profil mentődik.
Az összhangerő eltérését az illesztés kiszűri; az Output értéke változatlan marad.
Ha a Bypass aktív, kapcsold ki a korrekció meghallgatásához.

Ez átlagos tónusillesztés: a dinamikát, torzítást vagy térérzetet nem másolja át.
A nagyon halk vagy a másik felvételből hiányzó frekvenciatartományokra nem készít
erős helyreállító emelést. A 16 sáv közelítést ad, nem pontonkénti spektrummásolatot.

Fejlesztői ellenőrzés: `./build.sh` fordítja a VST3/AU/Standalone formátumokat,
és futtatja a DSP- és Match EQ-teszteket. A Match EQ tesztjei lefedik a
hangerőfüggetlenséget, a tényleges szűrőválaszt, több mintavételi frekvenciát,
a sztereó ellenfázist, a mentést/betöltést és a tanulás életciklusát.

## Licencelés

A ParaDyn EQ a Moth Production megosztott licencelési moduljét használja
(próbaidőszak + távoli aktiválás/deaktiválás + offline türelmi idő +
aláírt licenc-ellenőrzés) - ugyanaz a modul, amit a DeathWings, PhaseLock,
StringLife, BlendSlot és SplitFire is használ. A `PARADYNEQ_ENABLE_LICENSING`
CMake opció vezérli (alapértelmezett `ON`).

- **Próbaidőszak**: 15 nap az első indítástól, lokálisan követve.
- **Lejáratkor** (a próbaidő letelt és nincs érvényes licenc): a szerkesztő
  felülete továbbra is normálisan megnyílik - csak a hang némul el
  (`processBlock()` törli a puffert és korán visszatér, még a felhasználó
  saját Bypass gombja előtt is, hogy ne legyen "bypass-alakú kiskapu"). Nincs
  külön "zárolt" UI-mód.
- **Új fájlok**: `Source/Licensing/` (12 fájl, app-agnosztikus - változatlan
  a megosztott modulhoz képest), `Assets/licensing_config.json`.
- **Konfig-példa** (a valós értékek az `Assets/licensing_config.json`-ban
  vannak, megosztva az egész terméksorral - ne commitolj plug-inenként
  eltérő kulcsot/URL-t):
  ```json
  {
    "companyName": "Moth Production",
    "applicationName": "ParaDyn EQ",
    "productId": "paradyn-eq",
    "productVersion": "0.1.0",
    "serverBaseUrl": "https://licensing.<placeholder-domain>",
    "publicKey": "<exponent>,<hex modulus>",
    "trialLengthDays": 15,
    "offlineGracePeriodDays": 14
  }
  ```
- **A `productId`-nak pontosan egyeznie kell azzal, amire a licencszerver
  aláírt licencet ad ki** - eltérés esetén `Tampered` hibaként jelenik meg,
  nem egy külön "rossz produktum" hibaként, még ha az aláírás önmagában
  helyes is.
- **Build-time override-ok** (lokális szerver teszteléshez, a committolt
  config szerkesztése nélkül):
  ```bash
  cmake -B build -DPARADYNEQ_LICENSE_SERVER_BASE_URL=http://127.0.0.1:8787
  ```
  Támogatott cache-változók: `PARADYNEQ_LICENSE_SERVER_BASE_URL`,
  `PARADYNEQ_LICENSE_PRODUCT_ID`, `PARADYNEQ_LICENSE_PUBLIC_KEY`.
- **Threading**: minden inicializáció egyszer fut a processor konstruktorában
  (fő/message szálon); az aktiválás/deaktiválás/frissítés hívások a license
  manager saját háttér `juce::ThreadPool`-ján futnak; a `processBlock()`
  csak egy lock-free atomic olvasást végez (`isAudioEnabled()`) - az audio
  szálon soha nincs hálózati vagy diszk I/O.
- **Megosztott modul forrása**: `Licensing-VST` (külön repo -
  `juce-module/Source/Licensing/` a kliens modul, `server/` a FastAPI
  aktiváló szerver + Docker deploy, megosztva az egész terméksorral). A
  ParaDyn EQ saját `Source/Licensing/` mappája ennek egy vendorolt
  (nem submodule) másolata (a DeathWings-VST-ből átemelve).

## GitHub release

See [RELEASE_PIPELINE.md](docs/RELEASE_PIPELINE.md) for licensed Mac/Windows builds, binary verification and separate manual publication. Licensing product ID: `paradyn-eq`.
