# klc-light : directions des vagues, touche Power — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remplacer les origines `aqua-N` par des vagues décrites par une origine nommée, un sens et une taille (dont une origine sur une touche choisie au clic), et rendre la touche Power pilotable.

**Architecture:** Une structure `Wave` remplace le champ `origin` partout (`BaseLayer`, `EffectLayer`, `Group`) ; les anciennes valeurs sont converties à la lecture et les profils de la bibliothèque passent en version 3. `Layout` porte pour chaque touche sa position en unités du contrôleur (coordonnées de GG) ; `protocol.cpp` en tire les octets 0x8C … 0x9A et `Preview` le décalage de chaque touche. La touche Power est le 12ᵉ élément du paquet de 11 touches, dont l'octet 2 devient le compte d'éléments.

**Tech Stack:** C++17, Qt 6 (Core, Quick, QuickControls2 Material, Test), qmake6, hidapi ; Python 3 pour `tools/gen_layout.py`.

**Spec:** `docs/superpowers/specs/2026-11-04-klc-light-vagues-design.md` (mesures dans `docs/origins.md`)

## Global Constraints

- Aucun envoi au vrai clavier pendant le développement ; l'interface se lance seulement avec `KLC_LIGHT_FAKE_DEVICE=1`. Ne jamais `pkill -f build/klc-light` : `pkill -x klc-light`.
- Tests : `bin/test`. Les 200 tests existants restent verts, sauf ceux qu'une tâche réécrit explicitement (nommés dans la tâche).
- `transitions()` (pentes par unité, commit `9871455`) ne change pas.
- Au plus 16 étapes par cycle, au plus 6 emplacements animés : inchangé.
- Unités du contrôleur : x' = round((x_GG − 140) · 4294 / 667), y' = round((y_GG − 89) · 4294 / 667), y vers le haut (barre d'espace y' = 0, touches F y' = 1075).
- Points nommés : `left` (0, 538), `right` (4294, 538), `bottom` (2147, 0), `top` (2147, 1075), `center` (2147, 538).
- Vitesse (0x94) = round(31 · (1001 / 31)^((taille − 1) / 99)) : taille 1 → 31, 68 → 326, 100 → 1001. Taille par défaut 68.
- `Wave` par défaut : `{origin: "left", flow: "toward", size: 68}`.
- Chaînes d'interface en anglais (`qsTr`/`tr`), traduites dans `src/i18n/fr.json`.
- Commits : en anglais, à l'impératif, terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. Un fichier de thème v1 (`keyboard.klc.json`) avec `"origin": "aqua-3"` doit toujours s'appliquer par `klc-light apply --theme` (hook Omarchy) → test `profileV1AquaOriginStillApplies` (Tâche 3).
2. Annuler (Ctrl+Z) après avoir choisi une touche d'origine rend l'origine précédente, pas la sélection → test `uiPickOriginUndo` (Tâche 6).
3. Un groupe passé en vague reçoit la vague par défaut, et repasser en fixe puis en vague ne laisse pas d'origine invalide → test `backendNewWaveGroupDefaultWave` (Tâche 6).
4. La touche Power peinte d'une couleur, en mode réactif : son élément porte le masque réactif comme les autres touches → test `protocolPowerReactive` (Tâche 2).
5. Une origine sur une touche cachée (`HID_…`) ou absente est refusée, pas envoyée avec (0, 0) → test `profileWaveOriginHiddenKeyRejected` (Tâche 3).

---

### Task 1 : positions GG et touche Power dans la disposition

**Files:**
- Modify : `tools/gen_layout.py`, `src/layoutdata.h`, `src/layoutdata.cpp` (régénéré), `src/layout.h`, `src/layout.cpp`
- Test : `tests/klc-light_tests.cpp`

**Interfaces:**
- Produces : `Key` gagne `int cx = -1, cy = -1;` (position en unités du contrôleur, −1 sans position GG) ; `KeyData` gagne `int cx, cy;`. `Layout::byId("KEY_POWER")` : hid 0x66, `Region::Enter`, visible, dans `group("all")`.

- [ ] **Step 1 : tests en échec**

```cpp
void layoutPowerKey() {
    const Key *power = Layout::gs76().byId("KEY_POWER");
    QVERIFY(power);
    QCOMPARE(power->hid, quint8(0x66));
    QVERIFY(power->visible);
    QVERIFY(Layout::gs76().group("all").contains("KEY_POWER"));
    QCOMPARE(power->cx, 4294);
    QCOMPARE(power->cy, 1075);
}
void layoutControllerPositions() {
    const Layout &l = Layout::gs76();
    QCOMPARE(l.byId("KEY_ESC")->cx, 0);     // GG (140, 256)
    QCOMPARE(l.byId("KEY_ESC")->cy, 1075);
    QCOMPARE(l.byId("KEY_SPACE")->cx, 1738); // GG (410, 89)
    QCOMPARE(l.byId("KEY_SPACE")->cy, 0);
    for (const Key &key : l.keys())
        QVERIFY2(key.cx >= 0 && key.cy >= 0, qPrintable(key.id));
}
```

- [ ] **Step 2 :** `bin/test` → les deux échouent (`KEY_POWER` absent, `cx` inconnu à la compilation).

- [ ] **Step 3 : générateur**
  - `gen_layout.py` lit `~/Work/klc-re/decrypted/all/msi-ge76-klc.lisp` (constante `GG_LAYOUT`) : les triplets `(hid x y)` de `get-prism-sync-nonvariant-hid-info`, puis ceux du cas `((0) …)` (anglais) de `get-prism-sync-variant-hid-info` pour les codes absents (40, 49, 100, 229, 230).
  - Retirer 102 de `EXCLUDED` ; ajouter `PHOTO[102] = (963, 997, 183, 212)` (rangée F, colonne du pavé) et `EXTRA_EVDEV[102] = 116` (`KEY_POWER`) ; sa région : `Enter` (elle n'est dans aucune liste de `regions.json`).
  - Émettre `cx, cy` par la formule des contraintes globales, `-1, -1` sans position GG. Une touche visible sans position GG fait échouer le générateur.
  - Régénérer : `python3 -I tools/gen_layout.py`.

- [ ] **Step 4 :** `Layout::Layout()` recopie `cx`, `cy`. `bin/test` → tout vert (les tests de disposition existants qui comptent les touches visibles passent de 102 à 103 : mettre à jour ces comptes dans la même étape, rien d'autre).

- [ ] **Step 5 :** commit `Place keys in controller units and add the power key`.

### Task 2 : octet de compte et touche Power dans les paquets

**Files:**
- Modify : `src/protocol.cpp`, `src/layout.h`, `src/layout.cpp`
- Test : `tests/klc-light_tests.cpp` (`compareWithFixture`)

**Interfaces:**
- Consumes : `KEY_POWER` (Task 1).
- Produces : `regionPacket()` écrit `packet[2] = fragments.size()` ; `regionCode()` est supprimé ainsi que ses usages.

- [ ] **Step 1 : tests en échec**
  - `protocolPowerStatic` : profil fixe `#00E5FF`, `keys["KEY_POWER"] = #FF0000` → le paquet dont l'octet 2 vaut 0x0C contient, en élément 12 (offset 4 + 11·12), `ff 00 00 00 00 00 00 00 00 01 00 66` ; les trois autres paquets `0x0e` sont identiques à ceux d'avant (octets 2 : 0x2A, 0x18, 0x24).
  - `protocolPowerAnimated` : fond `wave` → l'élément Power est lié à l'emplacement 0, masque 0 (effet).
  - `protocolPowerReactive` : réactif activé, autorisé → l'élément Power porte le masque 8 et la couleur réactive, comme `KEY_A`.

- [ ] **Step 2 :** `bin/test` → les trois échouent.

- [ ] **Step 3 :** dans les deux chemins de `buildPackets` (fixe et animé), ajouter le fragment de la touche Power à la fin de la liste de `Region::Enter`, construit comme celui de toute autre touche (couleur, emplacement, masque). `regionPacket()` écrit le compte de fragments.

- [ ] **Step 4 :** `compareWithFixture` neutralise, pour tout paquet `0x0e` dont l'attendu a l'octet 2 = 0x0B et l'obtenu 0x0C, l'octet 2 et le 12ᵉ élément (en les recopiant de l'obtenu), quel que soit le mode. `bin/test` → tout vert, y compris `protocolStaticAll`, `protocolStaticZqsd`, `protocolAres`, `protocolTron`.

- [ ] **Step 5 :** commit `Send the power key as the twelfth element of the 11-key packet`.

### Task 3 : modèle `Wave`, format v3 et conversion

**Files:**
- Modify : `src/profile.h`, `src/profile.cpp`, `src/document.h`, `src/document.cpp`, `src/effects.h`, `src/effects.cpp`, `src/protocol.cpp`, `src/backend.cpp`, `src/preview.cpp`
- Test : `tests/klc-light_tests.cpp`

**Interfaces:**
- Produces (dans `profile.h`) :
  - `struct Wave { QString origin = "left"; QString flow = "toward"; int size = 68; };` avec un `operator==` écrit à la main (le projet est en C++17)
  - `Wave waveFromAqua(const QString &origin);` — `aqua-1`, `aqua-4` → `{right, toward, 68}` ; `aqua-3` → `{left, toward, 68}` ; `aqua-0`, `aqua-2`, `aqua-5` → `{center, toward, 68}`.
  - `Wave normalized(const Wave &wave);` — bord + `away` → bord opposé + `toward` (`left`↔`right`, `top`↔`bottom`).
  - `QJsonObject waveToJson(const Wave &)` ; `std::optional<Wave> waveFromJson(const QJsonObject &holder, const QString &where, QStringList *errors)` — lit `holder["wave"]` (champs manquants : défauts), sinon `holder["origin"]` via `waveFromAqua`, sinon `Wave{}` ; résultat normalisé.
  - `QStringList validateWave(const Wave &, const QString &where, const Layout &)`.
- `BaseLayer::origin`, `EffectLayer::origin`, `Group::origin` deviennent `Wave wave;`. `EffectDef::origin` devient `std::optional<Wave> wave;` (vide : pulsation synchronisée).
- `toJson(const Document &)` écrit `"version": 3` et `"wave"` ; `documentFromJson` lit 1, 2 et 3.

- [ ] **Step 1 : tests en échec**
  - `profileWaveFromAqua_data/…` : les six valeurs `aqua-N` → la table ci-dessus.
  - `profileWaveNormalized` : `{left, away}` → `{right, toward}` ; `{top, away}` → `{bottom, toward}` ; `{center, away}` et `{KEY_G, away}` inchangés.
  - `documentV3RoundTrip` : un document avec un fond `{KEY_G, away, 40}` et un groupe `{top, toward, 1}` → `toJson` a `version` 3, un objet `wave` par vague et aucune clé `origin` ; relu, il est égal à l'original.
  - `documentV2AquaConverted` : un v2 avec un groupe `"origin": "aqua-3"` → `wave == {left, toward, 68}`.
  - `profileV1AquaOriginStillApplies` (Review Focus 1) : thème de test avec un `keyboard.klc.json` v1 dont le fond est `wave` + `"origin": "aqua-3"` → `klc-light apply --theme` (environnement de test existant `cli…`) sort 0 et envoie 7 paquets.
  - `profileWaveErrors_data/…` : origine `"KEY_NOPE"` → erreur contenant `wave origin: unknown key "KEY_NOPE"` ; `"diagonal"` → `wave origin: unknown "diagonal"` ; flow `"sideways"` → `wave flow` ; taille 0 et 101 → `wave size`.
  - `profileWaveOriginHiddenKeyRejected` (Review Focus 5) : origine sur une touche invisible de la disposition (un `HID_…`) → `wave origin: unknown key`.

- [ ] **Step 2 :** `bin/test` → échecs à la compilation puis sur ces tests.

- [ ] **Step 3 :** implémenter les fonctions ci-dessus ; remplacer `origin` par `wave` dans les lectures, écritures et validations (`validate(Profile)` et `validate(Document)` appellent `validateWave` pour chaque vague ; une origine `KEY_…` doit être une touche visible de `Layout`). `baseEffect` : `rainbow` et `wave` → `EffectDef::wave` = la vague du fond (`rainbow` : `{right, toward, 68}`) ; `breathing`, `flashing` → pas de vague. `layerEffect` : `pulse` → pas de vague. `builtinProfile` : emplacement 0 en pulsation, emplacements 1 … 5 `waveFromAqua("aqua-N")`. `Backend::setBehavior` / `setBaseType` : `Wave{}` au lieu de `"aqua-1"`. `Backend::setOrigin` disparaît (l'interface est refaite en Tâche 6 : la liste `aqua-N` de `EffectPanel.qml` est retirée ici, sans remplaçant). `Preview` : `wave` = `def.wave.has_value()`.

- [ ] **Step 4 :** `protocol.cpp`, provisoirement : modèle `aqua-0` sans vague, `aqua-3` avec vague (octets de vague du modèle conservés ; la Tâche 4 les écrit). `compareWithFixture(…, SkipTransitions)` neutralise aussi 0x8C … 0x9A des paquets d'effet. `bin/test` → tout vert.

- [ ] **Step 5 :** commit `Describe waves by origin, flow and size; read aqua origins as waves`.

### Task 4 : octets de vague

**Files:**
- Modify : `src/protocol.h`, `src/protocol.cpp`
- Test : `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes : `Wave`, `normalized` (Task 3) ; `Key::cx/cy` (Task 1).
- Produces (dans `protocol.h`) :
  - `struct WaveFields { int x = 0, y = 0; bool alongX = false, alongY = false; int speed = 0; bool away = false; };`
  - `WaveFields waveFields(const Wave &wave, const Layout &layout);` — points et types des contraintes globales ; une touche → sa position, circulaire.
  - `int waveSpeed(int size);`

- [ ] **Step 1 : tests en échec**
  - `protocolWaveSpeed` : `waveSpeed(1) == 31`, `waveSpeed(68) == 326`, `waveSpeed(100) == 1001`.
  - `protocolWaveFields_data/…` : `left` → (0, 538, 1, 0) ; `right` → (4294, 538, 1, 0) ; `top` → (2147, 1075, 0, 1) ; `bottom` → (2147, 0, 0, 1) ; `center` → (2147, 538, 1, 1) ; `KEY_SPACE` → (1738, 0, 1, 1) ; `away` → `away == true` pour `center` et pour une touche.
  - `protocolWaveBytes` : fond `wave` `{KEY_ESC, away, 100}` → paquet d'effet de l'emplacement 0 : 0x8C–0x8D `00 00`, 0x8E–0x8F `33 04` (1075), 0x90 `01 00`, 0x92 `01 00`, 0x94 `e9 03` (1001), 0x9A `01` ; 0x8A reste `ff`, transitions et total inchangés.
  - `protocolPulseUnchanged` : un groupe `pulse` → paquet d'effet identique à celui d'avant la tâche (modèle `aqua-0`, 0x8C … 0x9A d'origine).

- [ ] **Step 2 :** `bin/test` → échecs.

- [ ] **Step 3 :** implémenter ; dans `effectPacket`, quand `def.wave` existe, écrire les champs en u16 petit-boutiste aux offsets ci-dessus et 0x9A.

- [ ] **Step 4 :** `bin/test` → tout vert.

- [ ] **Step 5 :** commit `Encode wave origin, direction and speed as GG does`.

### Task 5 : aperçu des vagues

**Files:**
- Modify : `src/preview.h`, `src/preview.cpp`
- Test : `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes : `waveFields`, `waveSpeed` (Task 4).
- Produces : `Preview::colorAt` inchangé de signature ; `WaveMsPerUnit` et `Animated::leftmost` supprimés ; nouvelle fonction privée `double delayMs(const Key &, const WaveFields &) const`.

- [ ] **Step 1 : tests en échec** — avec un fond `wave` noir → blanc (étapes `[["#FFFFFF", 60], ["#000000", 60]]`), comparer, à un instant fixe, la luminosité de deux touches pour vérifier qui est en avance d'un décalage de d / vitesse × 10 ms :
  - `previewWaveTowardLeft` : `{left, toward}` → `KEY_KPENTER` en avance sur `KEY_A` du décalage attendu (à 1 ms près).
  - `previewWaveTowardTop` : `{top, toward}` → `KEY_SPACE` en avance sur `KEY_F5`.
  - `previewWaveAwayFromKey` : `{KEY_G, away}` → `KEY_G` en avance sur `KEY_ESC`.

- [ ] **Step 2 :** `bin/test` → échecs.

- [ ] **Step 3 :** distance |dx| (horizontale), |dy| (verticale), √(dx² + dy²) (circulaire) en unités du contrôleur ; décalage = d / vitesse × `UnitMs` ; `toward` : couleur à `ms + décalage`, `away` : à `ms − décalage`.

- [ ] **Step 4 :** `bin/test` → tout vert.

- [ ] **Step 5 :** commit `Preview waves from the controller's distance model`.

### Task 6 : backend et interface

**Files:**
- Modify : `src/backend.h`, `src/backend.cpp`, `src/EffectPanel.qml`, `src/KeyboardView.qml`, `src/Main.qml`, `src/i18n/fr.json`, `src/i18n/en.json`
- Test : `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes : `Wave`, `normalized` (Task 3).
- Produces (`Backend`) :
  - `Q_INVOKABLE void setWaveDirection(const QString &direction);` — `left`, `right`, `up`, `down`, `center-in`, `center-out`, `key-in`, `key-out` → (`left`, toward), (`right`, toward), (`top`, toward), (`bottom`, toward), (`center`, toward), (`center`, away), (touche, toward), (touche, away). La touche : celle de la vague si c'en est déjà une, sinon `KEY_G`.
  - `Q_INVOKABLE void setWaveSize(int size);` (bornée 1 … 100, une étape d'annulation par geste).
  - `Q_INVOKABLE void pickWaveOrigin();` `Q_INVOKABLE void cancelPick();` `Q_INVOKABLE bool pickKey(const QString &id);` (vrai si le mode choix était actif : fixe l'origine, une étape d'annulation, termine le mode).
  - `Q_PROPERTY(bool picking READ picking NOTIFY pickingChanged)`.
  - `current` gagne `direction` (l'un des huit identifiants), `waveKey` (identifiant de la touche, sinon `""`), `waveSize`.
- QML : `FlatComboBox` `objectName: "waveDirectionBox"` (8 entrées, libellés du §2 de la spec) ; `Slider` `objectName: "waveSizeSlider"` ; `Button` `objectName: "pickOriginButton"` (visible pour `key-in`/`key-out`, `checked` = `backend.picking`, un clic en mode choix annule) ; `Label` d'aide visible en mode choix : `"Click a key to set the wave origin · Esc cancels"` ; anneau `objectName: "originRing"` sur la touche `backend.current.waveKey`.

- [ ] **Step 1 : tests en échec**
  - `backendWaveDirections_data/…` : chaque identifiant → la `Wave` attendue ; `key-out` depuis `left` → origine `KEY_G` ; `key-in` depuis `{KEY_Q, away}` → `{KEY_Q, toward}`.
  - `backendNewWaveGroupDefaultWave` (Review Focus 3) : groupe passé en `wave`, puis `static`, puis `wave` → `Wave{}` et `validate` sans erreur.
  - `uiWaveDirectionChoices` : `waveDirectionBox` a 8 entrées.
  - `uiPickOrigin` : choisir `key-in`, cliquer `pickOriginButton`, cliquer `KEY_J` sur le clavier dessiné → origine `KEY_J`, mode terminé, sélection inchangée, `originRing` sur `KEY_J`.
  - `uiPickOriginEsc` : en mode choix, Échap → mode terminé, origine et panneau ouvert inchangés.
  - `uiPickOriginUndo` (Review Focus 2) : après le choix de `KEY_J`, Ctrl+Z → origine précédente.
  - `uiWaveSizeOneUndoStep` : un glissé du curseur de 68 à 20 → une seule étape d'annulation.

- [ ] **Step 2 :** `bin/test` → échecs.

- [ ] **Step 3 :** implémenter. `KeyboardView` : pendant `backend.picking`, le glissé ne sélectionne rien et un clic sur une touche appelle `backend.pickKey(id)`. `Main.qml` : Échap appelle `backend.cancelPick()` seulement quand `backend.picking`, sans fermer le panneau. Traductions fr des nouveaux libellés : « Vers la gauche », « Vers la droite », « Vers le haut », « Vers le bas », « Vers le centre », « Depuis le centre », « Vers une touche », « Depuis une touche », « Taille de vague », « Choisir la touche », « Cliquez une touche pour placer l'origine · Échap annule ».

- [ ] **Step 4 :** `bin/test` → tout vert ; lancer `KLC_LIGHT_FAKE_DEVICE=1 build/klc-light` et vérifier à l'œil la liste, le curseur, l'anneau et l'aperçu des huit directions.

- [ ] **Step 5 :** commit `Edit wave direction, size and key origin in the editor`.

### Task 7 : documentation, version, vérification sur le clavier

**Files:**
- Modify : `README.md`, `pkgbuild/PKGBUILD` (`pkgver=0.3.0`), `docs/origins.md`

- [ ] **Step 1 :** README : format v3 (objet `wave`), les huit directions, la touche Power, la conversion des `aqua-N`. `bin/test` → vert. Commit `Release 0.3.0`.
- [ ] **Step 2 (avec l'utilisateur, un envoi à la fois) :** Power peinte en vert ; puis un fond en vague par direction (huit), l'utilisateur dit d'où part la vague. Écart constaté → corriger `waveFields` (sens vertical, calage), test mis à jour, revérification. Consigner le résultat dans `docs/origins.md`, commit `Record the wave directions checked on the GS76`.
