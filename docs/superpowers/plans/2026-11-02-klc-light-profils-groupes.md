# klc-light : profils nommés, groupes, sélecteur RGB — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remplacer « un profil par thème » par une bibliothèque de profils nommés (liables à un thème), remplacer les calques Keys/effets par des groupes nommés avec comportement, et refaire les champs R G B.

**Architecture:** Un nouveau type `Document` (profil v2 : fond, groupes, réactif) est ce que l'éditeur manipule et enregistre ; `lighting(const Document &)` le convertit en `Profile` v1, que `Protocol` et `Preview` consomment sans changement. `Library` gère `~/.config/klc-light/` (fichiers de profils, `state.json`, migration depuis les thèmes). `Backend` et la CLI passent par `Library`.

**Tech Stack:** C++17, Qt 6 (Core, Quick, QuickControls2 Material, Test), qmake6, hidapi.

**Spec:** `docs/superpowers/specs/2026-11-02-klc-light-profils-groupes-design.md`

## Global Constraints

- Aucun changement dans `protocol.*`, `effects.*`, `device.*`, `hidapitransport.*`, `sudobinder.*`, `layout*`, `preview.*`. Écart de nommage assumé par rapport au §6 de la spec : le `Profile` v1 reste tel quel (c'est le format « envoyé ») et le profil v2 s'appelle `Document`, dans de nouveaux fichiers `document.h/.cpp`.
- Aucun envoi au vrai clavier pendant le développement. Lancer l'interface seulement avec `KLC_LIGHT_FAKE_DEVICE=1`. Ne jamais `pkill -f build/klc-light` (tue le shell) : `pkill -x klc-light`.
- Tests : `bin/test` (construit et lance `build/tests/klc-light_tests` en offscreen). Les 86 tests existants restent verts, sauf ceux explicitement réécrits par une tâche (listés dans la tâche).
- Au plus 16 étapes par cycle (`kMaxSteps`), au plus 6 emplacements animés (`kMaxAnimatedSlots`, compté par `animatedSlots()` sur `lighting(doc)`).
- Une touche appartient au plus à un groupe ; un groupe vidé est supprimé.
- Noms de profil et de groupe : non vides après `trimmed()`, uniques (comparaison sensible à la casse).
- Écritures de fichiers atomiques (`QSaveFile`).
- Chaînes d'interface en anglais dans le code (`qsTr`/`tr`), traduites dans `src/i18n/fr.json`. Message exact du refus d'animation : `"At most 6 animated groups (controller limit)"` → fr `"6 groupes animés au maximum (limite du contrôleur)"`.
- Commits : messages en anglais, à l'impératif, terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. Profil Tron réel de l'utilisateur (`~/.config/omarchy/themes/tron/keyboard.klc.json`, v1) migré : paquets identiques octet par octet → test `documentTronUserFileSamePackets` (Tâche 1), sur une copie dans `tests/fixtures/tron-user.klc.json`.
2. Changement de thème rapide pendant un `apply --theme` : le dernier thème doit gagner, comme `cliRapidThemeSwitch` aujourd'hui → test `cliThemeRapidSwitchLinked` (Tâche 3).
3. Renommer un profil vers un nom qui ne diffère que par la ponctuation (« Tron! » vs « Tron? ») : noms de fichiers distincts, pas d'écrasement → test `libraryFileNameCollision` (Tâche 2).
4. Saisie hexadécimale avec espaces ou minuscules (` 00e5ff `) acceptée ; `#12345` refusée sans modifier la couleur → test `uiHexFieldParses` (Tâche 8).
5. Supprimer le dernier profil restant : un « Default » vide est créé et ouvert, rien ne plante → test `backendDeleteLastProfile` (Tâche 5).

---

### Task 1: `Document`, le profil v2

**Files:**
- Create: `src/document.h`, `src/document.cpp`
- Create: `tests/fixtures/tron-user.klc.json` (copie de `~/.config/omarchy/themes/tron/keyboard.klc.json`)
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes: `Profile`, `EffectLayer`, `BaseLayer`, `Reactive`, `Step`, `toJson(const Profile&)`, `profileFromJson`, `validate(const Profile&, const Layout&)`, `animatedSlots`, `kMaxSteps`, `kMaxAnimatedSlots` (`profile.h`) ; `Layout::gs76()`, `Layout::keys()` (ordre de la disposition).
- Produces:
  ```cpp
  struct Group {
      QString name;
      QString behavior = "static"; // static, pulse, wave
      QStringList keys;
      QColor color = Qt::white;    // static
      QColor start;                // pulse, wave (as EffectLayer)
      QList<Step> steps;
      int closing = 0;
      QString origin;              // "aqua-0".."aqua-5"
      bool animated() const { return behavior != "static"; }
  };
  struct Document {
      QString name;
      int brightness = 100, speed = 100;
      BaseLayer base;
      QList<Group> groups;
      Reactive reactive;
      int groupOf(const QString &keyId) const; // index, -1 if none
  };
  Profile lighting(const Document &doc);                       // version 1
  Document fromLighting(const Profile &profile, const QString &name);
  QJsonObject toJson(const Document &doc);                     // "version": 2, "name"
  std::optional<Document> documentFromJson(const QJsonObject &o, QStringList *errors); // v1 or v2
  QStringList validate(const Document &doc, const Layout &layout);
  std::optional<Document> readDocument(const QString &path, QStringList *errors);
  bool saveDocument(const Document &doc, const QString &path, QString *error);
  ```

- [ ] **Step 1: Write the failing tests**

  - `documentRoundTrip` : un `Document` avec un groupe static (`KEY_W`,`KEY_A` `#00E5FF`), un pulse et un wave (`origin` `aqua-2`) → `documentFromJson(toJson(d))` redonne les mêmes valeurs ; `toJson(d)["version"] == 2`, `["name"] == d.name`.
  - `documentFromV1` : `Profile` v1 avec `keys = {KEY_W:#FF0000, KEY_A:#FF0000, KEY_ESC:#00FF00}` et un effet pulse « Pulse 1 » sur `KEY_F1` → `fromLighting(p, "X")` : 3 groupes ; `groups[0]` static `#ff0000` nommé `"Group #ff0000"` ; ordre des groupes static = ordre de première apparition dans `Layout::gs76().keys()` ; le groupe pulse garde nom, steps, start, closing, origin.
  - `documentV1EffectWinsOverPaint` : `KEY_F1` à la fois dans `keys` et dans un effet → absent du groupe static, présent dans le groupe animé.
  - `documentSamePackets_data/documentSamePackets` : pour `builtinProfile("ares")`, `builtinProfile("tron")`, le profil static `#00E5FF` et le profil ZQSD utilisés par `protocolStaticAll`/`protocolStaticZqsd` → `buildPackets(lighting(fromLighting(p,"t")))` égal octet par octet à `buildPackets(p)` (comparer `kind` et `data` de chaque paquet).
  - `documentTronUserFileSamePackets` : `readProfile("tests/fixtures/tron-user.klc.json")` → même comparaison.
  - `documentRejects_data/documentRejects` : nom de groupe vide → erreur ; deux groupes de même nom → erreur ; une touche dans deux groupes → erreur ; touche inconnue → erreur ; groupe animé sans étape → erreur ; 7 groupes pulse → erreur contenant `"animated"`. Chacun : `validate(doc, Layout::gs76())` non vide.
  - `documentReadsV1File` : `readDocument` sur un fichier v1 écrit dans un `QTemporaryDir` → `Document` valide, `name` vide (le nom vient de la bibliothèque).

- [ ] **Step 2: Run tests to verify they fail**

  Run: `bin/test 2>&1 | tail -20` — Expected: échec de compilation (`document.h` absent).

- [ ] **Step 3: Implement `src/document.h/.cpp`**

  - `lighting` : `version = 1` ; groupes static → `keys.insert(id, color)` ; groupes animés, dans l'ordre → `EffectLayer{name, behavior, keys, start, steps, closing, origin}`.
  - `fromLighting` : groupes animés d'abord collectés depuis `effects` ; puis parcours de `Layout::gs76().keys()` dans l'ordre, une couleur nouvelle (comparée par `QColor::name()`) ouvre un groupe `QCoreApplication::translate("Document", "Group %1").arg(color.name())` ; ordre final : groupes static puis groupes animés.
  - `documentFromJson` : `version` 1 → `profileFromJson` puis `fromLighting(…, "")` ; 2 → lecture directe (mêmes noms de champs que v1 pour `base`, `reactive`, steps : réutiliser les helpers de `profile.cpp` en les exposant si besoin — sinon les dupliquer dans `document.cpp`) ; autre → erreur `"version: N is not supported"`.
  - `validate(Document)` : règles de nom, unicité des touches, touches connues, `steps` 1..16 pour les animés, puis `validate(lighting(doc), layout)` (couvre la limite de 6 animés).
  - `readDocument`/`saveDocument` : même structure que `readProfile`/`saveProfile` (`themeprofile.cpp`), `QSaveFile`.
  - Ajouter `"Group %1": "Groupe %1"` à `src/i18n/fr.json`.

- [ ] **Step 4: Run tests to verify they pass**

  Run: `bin/test 2>&1 | tail -5` — Expected: `Totals: … 0 failed`.

- [ ] **Step 5: Commit** — `git add src/document.* src/i18n/fr.json tests/ && git commit -m "Add the group-based profile format"`

---

### Task 2: `Library`, la bibliothèque de profils

**Files:**
- Create: `src/library.h`, `src/library.cpp`
- Modify: `src/themeprofile.h/.cpp` (ajout de `ThemePaths::stockThemes` = `/usr/share/omarchy/themes` dans `standard()` ; `QStringList installedThemes(const ThemePaths&)` ; `std::optional<Profile> themeLighting(const ThemePaths&, const QString &theme, QStringList *errors)`)
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes: `Document`, `readDocument`, `saveDocument`, `fromLighting` (Tâche 1) ; `profileFromLegacy`, `readProfile`.
- Produces:
  ```cpp
  struct LibraryEntry { QString name; QString path; bool readable = true; };
  class Library {
  public:
      explicit Library(QString dir);                 // ~/.config/klc-light
      QString dir() const;
      QList<LibraryEntry> entries() const;           // sorted by name, QString::localeAwareCompare
      QStringList names() const;
      std::optional<Document> load(const QString &name, QStringList *errors) const;
      bool save(const Document &doc, QString *error);            // create or overwrite by doc.name
      bool rename(const QString &from, const QString &to, QString *error);
      bool remove(const QString &name, QString *error);
      QString nameError(const QString &name, const QString &except = {}) const; // "" if usable
      QString applied() const;
      void setApplied(const QString &name);
      QString profileForTheme(const QString &theme) const;      // "" if unlinked
      QString themeOf(const QString &profile) const;            // "" if unlinked
      QString link(const QString &profile, const QString &theme); // theme "" unlinks; returns the profile the theme was linked to before ("" if none)
      bool migrate(const ThemePaths &paths);                    // only when <dir>/profiles is absent; returns true if it ran
      static QString fileNameFor(const QString &name, const QStringList &takenFileNames);
  };
  QStringList installedThemes(const ThemePaths &paths);           // union of user and stock theme dirs, sorted
  std::optional<Profile> themeLighting(const ThemePaths &paths, const QString &theme, QStringList *errors);
  ```
  Fichiers : `<dir>/profiles/<fileName>.klc.json`, `<dir>/state.json` = `{"applied": "<nom>", "themes": {"<thème>": "<nom>"}}`.

- [ ] **Step 1: Write the failing tests**

  - `librarySaveLoad` : `save(doc "Tron")` → `entries()` contient « Tron », fichier `profiles/tron.klc.json`, `load("Tron")` == doc.
  - `libraryFileNameCollision` : `fileNameFor("Tron!", {"tron-"})` → `"tron--2"` ; plus concrètement : enregistrer « Tron! » puis « Tron? » → deux fichiers distincts, deux entrées.
  - `libraryRename` : renommer « A » en « B » lié au thème `tron` et `applied` → fichier renommé, `themeOf("B") == "tron"`, `applied() == "B"`, `load("A")` vide.
  - `libraryRemove` : supprimer le profil appliqué et lié → plus d'entrée, `applied() == ""`, `profileForTheme("tron") == ""`.
  - `libraryNameError` : `""`, `"  "` → non vide ; nom existant → non vide ; même nom avec `except` = ce nom → `""`.
  - `libraryLinkOneToOne` : `link("A","tron")` puis `link("B","tron")` → retourne `"A"`, `themeOf("A") == ""` ; `link("B","gruvbox")` → `themeOf("B") == "gruvbox"`, `profileForTheme("tron") == ""`.
  - `libraryStateCorrupt` : `state.json` = `"{oops"` → `applied() == ""`, aucun lien, `state.json.bad` existe, la prochaine écriture produit un `state.json` valide.
  - `libraryUnreadableEntry` : fichier `profiles/bad.klc.json` invalide → entrée `readable == false`, `load` retourne vide avec erreur, fichier inchangé après `save` d'un autre profil.
  - `libraryMigrate` (thèmes factices via `fakeTheme`) : thème utilisateur `tron` avec `keyboard.klc.json` + `.essai` ; thème stock `tokyo-night` avec `keyboard.rgb` ; thème `plain` sans éclairage ; thème actif `tron` → profils « Tron » et « Tokyo-night » liés, pas de profil pour `plain`, `applied() == "Tron"`, `.essai` ignoré, fichiers de thème inchangés ; second appel → `false`, rien ne change.
  - `libraryMigrateNothing` : aucun thème éclairé → profil « Default » (fond noir static), non lié, `applied() == "Default"`.

- [ ] **Step 2: Run tests to verify they fail** — `bin/test 2>&1 | tail -20` : échec de compilation.

- [ ] **Step 3: Implement**

  - `fileNameFor` : `name.toLower()`, tout caractère non `[a-z0-9]` → `-`, vide → `profile` ; suffixe `-2`, `-3`… tant que pris.
  - Le nom affiché est lu dans le fichier (`"name"`) ; à défaut, le nom de fichier.
  - `state.json` lu au constructeur, réécrit (atomique) à chaque changement ; corrompu → renommé `state.json.bad`.
  - `themeLighting` : pour un thème donné, `userThemes/<t>/keyboard.klc.json`, sinon `stockThemes/<t>/keyboard.klc.json`, sinon `keyboard.effect` (utilisateur puis stock) via `profileFromLegacy(effect, {})`, sinon `keyboard.rgb` via `profileFromLegacy({}, rgb)`.
  - `migrate` : pour chaque `installedThemes`, `themeLighting` ; nom = thème avec première lettre en capitale ; `link` ; `applied` = profil du thème actif (`paths.themeName`), sinon « Default » créé.
  - `resolveProfile` et `Resolved` ne servent plus après la Tâche 3 ; les laisser jusque-là.

- [ ] **Step 4: Run tests to verify they pass** — `bin/test 2>&1 | tail -5` : 0 failed.

- [ ] **Step 5: Commit** — `git commit -m "Keep profiles in a library, linked to themes"`

---

### Task 3: `klc-light apply` passe par la bibliothèque

**Files:**
- Modify: `src/cli.h` (`CliEnv` gagne `QString libraryDir`), `src/cli.cpp`, `src/main.cpp` (`libraryDir = QDir::homePath() + "/.config/klc-light"`)
- Modify: `src/themeprofile.h/.cpp` (supprimer `Resolved`/`resolveProfile` et leurs tests `themeProfileOrder`, `themeProfileNothing`, `themeProfileNoOmarchy`, `themeProfileInvalid`, `themeProfileSaveStock` — leur logique vit dans `themeLighting`, couverte par `libraryMigrate`)
- Test: `tests/klc-light_tests.cpp` (adapter `CliRun` : `env.libraryDir = dir.path() + "/klc"` ; adapter `cliCheck`, `cliInvalid`, `cliNothing`, `cliSends`, `cliJson`, `cliRapidThemeSwitch`, `cliKeyboardAbsentExitCode` au nouveau comportement)

**Interfaces:**
- Consumes: `Library` (Tâche 2), `lighting`, `buildPackets`, `Device::applyLatest`.
- Produces: usage `klc-light apply [<nom> | <fichier.json>] [--theme] [--check] [--json] [--experimental-reactive]`. Après un envoi réussi d'un profil de la bibliothèque, `Library::setApplied(nom)`. Le JSON de sortie garde `ok`, `packets`, `source` (source = nom du profil ou chemin).

- [ ] **Step 1: Write the failing tests**

  - `cliThemeLinked` : bibliothèque avec « Tron » lié à `tron`, thème actif `tron` → `apply --theme` envoie (le `FakeTransport` voit des paquets), code 0, `applied() == "Tron"`.
  - `cliThemeUnlinked` : thème actif `gruvbox` non lié → `apply --theme` : code 0, aucun `open` dans `log.events`, `applied()` inchangé.
  - `cliBootApplied` : `applied` = « Ares » → `apply` envoie « Ares ».
  - `cliBootNothingApplied` : `applied` vide, thème lié → `apply` se comporte comme `--theme`.
  - `cliByName` : `apply Ares` envoie « Ares » ; `apply Inconnu` → code 1, message contenant `Inconnu`.
  - `cliByPath` : `apply /chemin/x.json` (v1 ou v2) envoie ce fichier ; `applied()` inchangé.
  - `cliMigratesFirst` : bibliothèque absente, thème `tron` avec `keyboard.klc.json` → `apply --theme` migre puis envoie.
  - `cliThemeRapidSwitchLinked` : reprend `cliRapidThemeSwitch` (le thème change pendant l'attente de l'intervalle via `onSleep`) avec deux thèmes liés à deux profils → c'est le profil du dernier thème qui est envoyé.

- [ ] **Step 2: Run tests to verify they fail** — `bin/test 2>&1 | grep -E 'FAIL|Totals'`.

- [ ] **Step 3: Implement in `src/cli.cpp`**

  `build()` (appelé par `applyLatest` au moment de l'envoi) : `Library lib(env.libraryDir); lib.migrate(env.paths);` puis choisit la source — chemin si l'argument contient `/` ou finit par `.json`, sinon nom ; sans argument : `--theme` → `profileForTheme(thème actif)` (vide → rien à envoyer, pas d'erreur) ; sans `--theme` → `applied()`, vide → comme `--theme`. Le thème actif est relu dans `build()` pour que le dernier thème gagne.

- [ ] **Step 4: Run tests** — `bin/test 2>&1 | tail -5` : 0 failed.

- [ ] **Step 5: Commit** — `git commit -m "Apply library profiles from the command line"`

---

### Task 4: `Backend` en groupes

**Files:**
- Modify: `src/backend.h`, `src/backend.cpp`
- Test: `tests/klc-light_tests.cpp` — réécrire `backendPaint`, `backendEffectLayer`, `backendKeysDoNotOverlap`, `backendBaseEffectShowsOnWholeKeyboard`, `backendUndo` sur les groupes ; `EditorRun` gagne une bibliothèque dans `dir.path() + "/klc"` (constructeur ci-dessous).

**Interfaces:**
- Consumes: `Document`, `Group`, `lighting`, `validate(Document)` (Tâche 1) ; `Library` (Tâche 2).
- Produces (remplace `layers`, `currentLayer`, `setCurrentLayer`, `addEffect`, `removeLayer`, `assignSelection`, `profile()`):
  ```cpp
  Backend(ThemePaths paths, QString libraryDir, KeyLabels labels, Sender sender, QString colorsPath, QObject *parent = nullptr);
  Q_PROPERTY(QVariantList groups READ groups NOTIFY profileChanged)   // [{name, behavior, count, color}]
  Q_PROPERTY(QString currentKind READ currentKind NOTIFY currentChanged) // "base" | "group" | "selection" | "reactive"
  Q_PROPERTY(int currentGroup READ currentGroup NOTIFY currentChanged)  // -1 unless kind == "group"
  Q_PROPERTY(QStringList groupKeys READ groupKeys NOTIFY currentChanged) // keys of the open group
  Q_PROPERTY(int animatedCount READ animatedCount NOTIFY profileChanged)
  Q_PROPERTY(QVariantMap current ...)  // as today; for "group": name, behavior, color, steps, origin, count
  Q_INVOKABLE void openBase();
  Q_INVOKABLE void openGroup(int index);     // clears the selection
  Q_INVOKABLE void openReactive();
  Q_INVOKABLE void select(const QStringList &ids, bool add);
  Q_INVOKABLE QString createGroup(const QString &name);       // from the selection; "" or error text
  Q_INVOKABLE void addSelectionToGroup(int index);
  Q_INVOKABLE void removeSelectionFromGroup();                // open group
  Q_INVOKABLE void deleteGroup(int index);
  Q_INVOKABLE QString renameGroup(int index, const QString &name); // "" or error text
  Q_INVOKABLE QString setBehavior(const QString &behavior);   // open group; "" or error text
  Q_INVOKABLE QString defaultGroupName() const;               // label of the single selected key, else "Group N"
  const Document &document() const;
  ```
  `setColor`, `setStepColor`, `setStepDuration`, `addStep`, `removeStep`, `setOrigin`, `setBaseType`, brightness, speed, reactive, undo/redo, gestures : même nom, s'appliquent au fond, au groupe ouvert ou au réactif selon `currentKind`. L'historique d'annulation stocke des `Document`.

- [ ] **Step 1: Write the failing tests**

  - `backendClickGroupedKeyOpensGroup` : groupe « ZQSD » (`KEY_W`,`KEY_A`,`KEY_S`,`KEY_D`) → `select({"KEY_A"}, false)` → `currentKind() == "group"`, `currentGroup() == 0`, `groupKeys().size() == 4`, `selection() == {"KEY_A"}`.
  - `backendClickFreeKeyOffersGroup` : `select({"KEY_ESC"}, false)` → `"selection"` ; `defaultGroupName()` = étiquette de la touche donnée par `KeyLabels::label`.
  - `backendCtrlClickOutsideGroup` : groupe ouvert, `select({"KEY_ESC"}, true)` → `"selection"` ; `select({"KEY_S"}, true)` dans le groupe → reste `"group"`.
  - `backendCreateGroupMovesKeys` : « ZQSD » existe ; sélection `KEY_W`,`KEY_ESC` → `createGroup("Mix")` → `""`, « ZQSD » n'a plus `KEY_W`, « Mix » ouvert ; tout retirer de « ZQSD » le supprime.
  - `backendGroupNameRules` : `createGroup("")` et nom existant → texte d'erreur non vide, aucun changement, `canUndo()` inchangé ; `renameGroup` idem.
  - `backendBehaviorSwitch` : static `#FF0000` → `setBehavior("pulse")` : une étape `#FF0000` ; retour `"static"` : `color` = première étape.
  - `backendAnimatedLimit` : 6 groupes pulse → 7e `setBehavior("pulse")` retourne `"At most 6 animated groups (controller limit)"` (en anglais ; traduit par le `Translator`), rien ne change.
  - `backendPaintGroup` (réécrit de `backendPaint`) : groupe static ouvert, `setColor(#FF0000)` → `previewColor("KEY_W",0) == #FF0000`, `dirty()`.
  - `backendBaseEffectShowsOnWholeKeyboard` (réécrit) : `setBaseType("rainbow")` supprime les groupes animés, garde les static, notice inchangée, Ctrl+Z les rend.
  - `backendUndoGroups` : créer, renommer, supprimer un groupe → trois `undo()` reviennent à l'état initial.
  - `backendNoOpEditKeepsHistory`, `backendGestureIsOneUndoStep` : inchangés, doivent passer (fond ouvert par défaut).

- [ ] **Step 2: Run tests to verify they fail** — compilation ou échecs attendus.

- [ ] **Step 3: Implement** — l'état du profil devient `Document m_doc` ; `edit(std::function<void(Document &)>)` compare `toJson(Document)` ; `changed()` construit `m_preview` depuis `lighting(m_doc)`. Supprimer `m_watcher`, `watchTheme`, `reloadTheme`, `themeName`, `profilePath` (le profil ouvert ne dépend plus du thème). Le chargement initial passe à la Tâche 5 ; ici, le constructeur fait `Library::migrate` puis ouvre `applied()` (ou le premier profil).

- [ ] **Step 4: Run tests** — `bin/test 2>&1 | tail -5` : 0 failed (les tests UI peuvent échouer tant que la Tâche 6 n'est pas faite : les lister, ne pas les désactiver ; ils doivent repasser à la fin de la Tâche 6).

- [ ] **Step 5: Commit** — `git commit -m "Edit named key groups instead of layers"`

---

### Task 5: `Backend`, gestion des profils

**Files:**
- Modify: `src/backend.h`, `src/backend.cpp`
- Test: `tests/klc-light_tests.cpp` (réécrire `backendApplySaves`, `backendApplyInvalid`)

**Interfaces:**
- Consumes: `Library` (Tâche 2), `installedThemes`.
- Produces:
  ```cpp
  Q_PROPERTY(QStringList profiles READ profiles NOTIFY libraryChanged)
  Q_PROPERTY(QString profileName READ profileName NOTIFY libraryChanged)
  Q_PROPERTY(QString appliedName READ appliedName NOTIFY libraryChanged)
  Q_PROPERTY(QString linkedTheme READ linkedTheme NOTIFY libraryChanged)
  Q_PROPERTY(QStringList themes READ themes CONSTANT)          // installedThemes
  Q_INVOKABLE bool openProfile(const QString &name);           // discards unsaved changes; QML asks first
  Q_INVOKABLE QString newProfile(const QString &name);         // "" or error; opens it
  Q_INVOKABLE QString duplicateProfile(const QString &name);
  Q_INVOKABLE QString renameProfile(const QString &name);
  Q_INVOKABLE void deleteProfile();
  Q_INVOKABLE QString linkTheme(const QString &theme);         // "" or the profile it was linked to
  Q_INVOKABLE bool save();                                     // Ctrl+S
  Q_INVOKABLE QString nameError(const QString &name) const;    // for live validation in QML
  signal void libraryChanged();
  ```
  `apply()` : `save()`, envoi, puis `Library::setApplied(profileName)` dans `onApplyFinished` si `result.error == ApplyError::None`.

- [ ] **Step 1: Write the failing tests**

  - `backendOpensApplied` : bibliothèque « A », « B », `applied` = « B » → au démarrage `profileName() == "B"`.
  - `backendNewDuplicateRename` : `newProfile("C")` → ouvert, vide (fond noir static, aucun groupe), fichier écrit ; `duplicateProfile("C (copy)")` → mêmes groupes ; `renameProfile("D")` → `profiles()` contient « D », pas « C (copy) » ; noms vides ou pris → erreur, rien ne change.
  - `backendSwitchResetsHistory` : modifier « A », `save()`, `openProfile("B")` → `!canUndo()`, `!dirty()`.
  - `backendSaveDoesNotSend` : `save()` → fichier mis à jour, `run.sent == 0`, `!dirty()`.
  - `backendApplySetsApplied` : `apply()` → `QTRY_COMPARE(run.sent, 1)`, `QTRY_COMPARE(appliedName(), profileName())`.
  - `backendApplyFailureKeepsApplied` : sender qui renvoie `ApplyError::KeyboardAbsent` → `appliedName()` inchangé.
  - `backendLinkTheme` : `linkTheme("tron")` sur « B » alors que « A » l'avait → retourne `"A"` ; `linkedTheme() == "tron"` ; n'entre pas dans l'historique (`!canUndo()`).
  - `backendDeleteApplied` : supprimer le profil appliqué → `appliedName() == ""`, le profil suivant s'ouvre.
  - `backendDeleteLastProfile` : supprimer l'unique profil → « Default » créé et ouvert.
  - `backendUnreadableProfile` : `openProfile` sur une entrée illisible → `false`, `error()` non vide, profil ouvert inchangé.
  - `backendSaveFailure` : dossier `profiles` en lecture seule (`QFile::setPermissions`) → `save()` faux, `error()` non vide, `dirty()` reste vrai.

- [ ] **Step 2–4:** échec, implémentation, `bin/test` : 0 failed (hors tests UI listés en Tâche 4).

- [ ] **Step 5: Commit** — `git commit -m "Open, create, save and link profiles from the editor"`

---

### Task 6: Interface des groupes

**Files:**
- Create: `src/GroupList.qml` (remplace `src/LayerList.qml`, à supprimer ; mettre à jour `src/resources.qrc`)
- Modify: `src/Main.qml` (utilise `GroupList`), `src/EffectPanel.qml`, `src/KeyboardView.qml` (voile des `backend.groupKeys`, distinct de la bordure blanche de sélection)
- Test: `tests/klc-light_tests.cpp` (`uiAddEffectHint` remplacé par `uiSelectionPanel`)

**Interfaces:**
- Consumes: propriétés et méthodes de la Tâche 4.
- Produces (objectName pour les tests) : `groupList` (ListView), `groupRow<i>`, `newGroupName` (TextField), `createGroupButton`, `addToGroupBox`, `groupNameField`, `behaviorBox`, `removeKeysButton`, `deleteGroupButton`, `animatedCount` (Label `"Animated: %1 / 6"`).

- [ ] **Step 1: Write the failing tests**

  - `uiClickKeyOpensGroup` : groupe « ZQSD » → clic souris sur la touche `KEY_A` dans `KeyboardView` (coordonnées depuis `backend.keys`) → `QTRY_VERIFY(findItem(…,"groupNameField")->property("text") == "ZQSD")`, `behaviorBox` visible.
  - `uiSelectionPanel` : clic sur `KEY_ESC` → `newGroupName` visible, texte = étiquette ; clic `createGroupButton` → `groupList` compte une ligne de plus.
  - `uiGroupRowOpens` : clic `groupRow1` → `backend.currentGroup == 1`.
  - `uiAnimatedCount` : 2 groupes pulse → `animatedCount.text` contient `"2 / 6"`.
  - Les tests UI existants (`uiBaseTypeChoicesVisible`, `uiHueRing*`, `uiRing*`, `uiSlider*`, `uiStepDuration*`, `uiActionBarVisibleInSmallWindow`, `uiUndoRedoButtons`, `uiQuitDiscardCloses`) repassent.

- [ ] **Step 2–4:** échec, implémentation (disposition et libellés : spec §4 « Colonne de gauche » et « Panneau de droite » ; « Échap » : `openBase()` + `clearSelection()`), `bin/test` : 0 failed.

- [ ] **Step 5: Commit** — `git commit -m "List key groups and open a key's group on click"`

---

### Task 7: Barre de profil et Enregistrer

**Files:**
- Create: `src/ProfileBar.qml` (+ `resources.qrc`)
- Modify: `src/Main.qml` (barre en haut ; bouton `saveButton` ; `Shortcut` `StandardKey.Save` → `backend.save()` ; dialogue `switchDialog` Enregistrer/Abandonner/Annuler avant `openProfile` quand `backend.dirty` ; le dialogue de sortie propose aussi Enregistrer)
- Test: `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes: Tâche 5.
- Produces (objectName) : `profileBox`, `newProfileButton`, `duplicateProfileButton`, `renameProfileButton`, `deleteProfileButton`, `profileNameField` (saisie dans un petit dialogue `profileNameDialog`, message d'erreur `profileNameError` lié à `backend.nameError`), `deleteProfileDialog`, `themeBox`, `saveButton`, `switchDialog`.

- [ ] **Step 1: Write the failing tests**

  - `uiProfileBoxListsProfiles` : « A », « B » → `profileBox` a 2 entrées, celle de `appliedName` marquée (texte contenant `"applied"` en anglais).
  - `uiSwitchWithChangesAsks` : modifier, choisir « B » dans `profileBox` → `switchDialog` visible, profil toujours « A » ; Abandonner → « B » ouvert.
  - `uiSaveShortcut` : modifier, `QTest::keySequence(window, QKeySequence::Save)` → `!backend.dirty`, `run.sent == 0`.
  - `uiNewProfileNameValidation` : `newProfileButton` → `profileNameField` = nom existant → `profileNameError` visible, bouton OK désactivé.
  - `uiThemeBoxLinks` : choisir `tron` dans `themeBox` → `backend.linkedTheme == "tron"`.

- [ ] **Step 2–4:** échec, implémentation, `bin/test` : 0 failed.

- [ ] **Step 5: Commit** — `git commit -m "Pick, create and save profiles from a profile bar"`

---

### Task 8: Sélecteur de couleur : hexadécimal et curseurs RGB

**Files:**
- Modify: `src/ColorPicker.qml`
- Test: `tests/klc-light_tests.cpp` (`uiRgbFieldsFit` remplacé)

**Interfaces:**
- Consumes: `picker.pick(color)`, `backend.beginGesture()/endGesture()`.
- Produces (objectName) : `hexField`, `rgbSlider0..2` (Slider 0–255, `stepSize: 1`), `rgbValue0..2` (valeur affichée, clic → `rgbEdit0..2` TextField éphémère).

- [ ] **Step 1: Write the failing tests**

  - `uiHexFieldParses` : saisir ` 00e5ff ` + Entrée → couleur `#00E5FF` ; saisir `#12345` + Entrée → couleur inchangée, texte revenu à l'ancienne valeur.
  - `uiRgbSliderDragIsOneUndoStep` : glisser `rgbSlider0` de 0 à 200 (press, plusieurs move, release) → une seule annulation ramène la couleur d'origine.
  - `uiRgbValueKeys` : clic sur `rgbValue1`, Haut → +1 ; Maj+Haut → +10 ; Échap → valeur d'avant la saisie ; Entrée valide.
  - `uiRgbLabelsTranslated` : langue `fr` → libellés `R`, `V`, `B` ; `en` → `R`, `G`, `B`.
  - `uiRgbNoBoxedFields` : aucun `TextField` visible dans `ColorPicker` hors édition (`hexField` excepté, `background` transparent).

- [ ] **Step 2–4:** échec ; implémentation (piste en `Rectangle` avec `Gradient { orientation: Gradient.Horizontal }` de la couleur avec la composante à 0 vers 255 ; valeur en `font.features: { "tnum": 1 }` ; police à chasse fixe pour `hexField` : `font.family: "monospace"` ; `"G": "V"` dans `fr.json`) ; `bin/test` : 0 failed.

- [ ] **Step 5: Commit** — `git commit -m "Pick colors with RGB sliders and a hex field"`

---

### Task 9: Traductions, hooks, installation

**Files:**
- Modify: `src/i18n/fr.json` (toutes les nouvelles chaînes des Tâches 4–8 ; le test existant `frenchCatalogCoversDeviceMessages` sert de modèle : ajouter `frenchCatalogCoversNewStrings` qui vérifie que chaque `qsTr`/`tr` des fichiers modifiés a une entrée)
- Modify: `README.md` (bibliothèque, liens, commandes `apply`)
- Modify (hors dépôt, après accord, avec `.bak`) : `~/.config/omarchy/hooks/theme-set.d/msi-keyboard-rgb` → `klc-light apply --theme` ; `post-boot.d/msi-keyboard-rgb` → `klc-light apply` (inchangé)
- Modify: `src/main.cpp` (`setApplicationVersion("0.2.0")`), `pkgbuild/PKGBUILD` (`pkgver=0.2.0`)

- [ ] **Step 1:** écrire `frenchCatalogCoversNewStrings`, le voir échouer, compléter `fr.json`, `bin/test` : 0 failed. Commit `"Translate the profile and group strings"`.
- [ ] **Step 2:** `klc-light apply --check --json` avec le binaire construit (`build/klc-light apply --theme --check --json`) sur la vraie config → `"ok":true`, `source` = « Tron » (la migration a tourné ; vérifier `~/.config/klc-light/profiles/tron.klc.json` et `state.json`). **Aucun envoi.**
- [ ] **Step 3:** demander l'accord de l'utilisateur, puis `bin/install`, puis mise à jour du hook `theme-set.d` (copie `.bak` d'abord).
- [ ] **Step 4:** lancer `KLC_LIGHT_FAKE_DEVICE=1 klc-light` pour une vérification visuelle avec l'utilisateur (capture `grim`), puis `pkill -x klc-light`.
- [ ] **Step 5:** commit `"Release 0.2.0"` ; les tests matériels (Tron migré, groupe fixe, groupe animé) se font ensuite avec l'utilisateur, un envoi à la fois.
