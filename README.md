# parametric-dynamic-eq-VST

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