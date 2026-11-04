# klc-light : plugin de barre Omarchy — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Un widget de barre Omarchy (`swald.klc-light`) qui montre le profil appliqué, en applique un autre, ouvre l'éditeur et éteint / rallume le clavier, adossé à deux nouvelles commandes `klc-light list` et `klc-light off`.

**Architecture:** `Library` gagne un état « éteint » dans `state.json`. `cli.cpp` gagne `list` (lecture seule) et `off` (un profil tout noir envoyé par le même chemin que `apply`). Le plugin QML, dans `omarchy-plugin/swald.klc-light/`, ne fait qu'appeler ces commandes avec `Process` et relire la liste quand `state.json` change, sur le modèle de `swald.taches` et de `swald.weather` (BarWidget + Panel chargé).

**Tech Stack:** C++17, Qt 6 (Core, Test), qmake6 ; QML Quickshell / omarchy-shell (`qs.Commons`, `qs.Ui`, `Quickshell.Io`).

**Spec:** `docs/superpowers/specs/2026-11-04-klc-light-plugin-omarchy-design.md`

## Global Constraints

- Aucun envoi au vrai clavier pendant le développement : tests avec les faux périphériques de `CliRun`, essais manuels avec `KLC_LIGHT_FAKE_DEVICE=1` (qui couvre aussi la ligne de commande).
- `state.json` reste lisible par les versions précédentes : `off` est un champ de plus, absent = faux.
- Tout `apply` réussi remet `off` à faux ; `off` ne change jamais `applied`.
- Codes de sortie : 0 succès, 1 erreur, 2 clavier absent (comme `apply`).
- Plugin : id `swald.klc-light`, `name` `KLC Light`, `displayName` `Keyboard lighting`, `category` `System`, `allowMultiple` faux ; chaînes par clés dans `i18n/en.json` et `i18n/fr.json` via `Commons.Translations` (comme `swald.taches`).
- Le plugin ne parle jamais au clavier : uniquement `klc-light list --json`, `klc-light apply [<nom>] --json`, `klc-light off --json`, `klc-light` (éditeur, détaché).
- Tests : `bin/test` ; les 253 tests existants restent verts.
- Commits en anglais, impératif, terminés par `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. Le hook de thème après `off` : `apply --theme` doit rallumer et remettre `off` à faux → test `cliOffThenThemeTurnsOn` (Tâche 2).
2. Premier lancement : `list` sur une bibliothèque encore vide mais des thèmes avec `keyboard.klc.json` doit montrer les profils migrés, comme `apply` le ferait → test `cliListMigratesFirstRun` (Tâche 1).
3. Un nom de profil avec guillemets ou accents (« Été "chaud" ») doit sortir intact en JSON → assertion dans `cliList` (Tâche 1).
4. `off` alors qu'un autre envoi tient le verrou : code 1, `off` inchangé → test `cliOffLocked` (Tâche 2).
5. Le plugin quand `klc-light` répond une sortie non JSON (ancienne version sans `list`) : panneau « KLC Light is not installed or too old », pas d'erreur QML → vérification manuelle (Tâche 3, Step 4).

---

### Task 1 : état « éteint » et `klc-light list`

**Files:**
- Modify : `src/library.h`, `src/library.cpp`, `src/cli.h`, `src/cli.cpp`
- Test : `tests/klc-light_tests.cpp`

**Interfaces:**
- Produces : `bool Library::off() const;` `void Library::setOff(bool off);` (écrit `state.json`) ; `state.json` écrit toujours `"off": <bool>`. `runCli` accepte `list [--json]`.

- [ ] **Step 1 : tests en échec**
  - `libraryOffState` : nouvelle bibliothèque → `off()` faux ; `setOff(true)` puis relecture (nouvel objet `Library`) → vrai ; `setApplied("X")` ne le change pas ; un `state.json` sans clé `off` → faux.
  - `cliList` : profils « Tron » (lié à `tron`), « Été "chaud" », et un fichier `broken.klc.json` illisible ; thème courant `tron` ; `applied` = Tron. `exec({"list","--json"})` → 0 ; JSON : `applied` `"Tron"`, `off` faux, `currentTheme` `"tron"`, `profiles` triés par nom, `Tron.theme == "tron"`, le nom accentué intact, `broken` avec `unreadable` vrai. Sans `--json` : une ligne `* Tron`, les autres préfixées de deux espaces.
  - `cliListMigratesFirstRun` : pas de dossier `profiles`, un thème `tron` avec `keyboard.effect` = `tron` → `list --json` contient un profil `Tron` lié à `tron`.
  - `cliListSendsNothing` : `log.events` vide après `list`.
- [ ] **Step 2 :** `bin/test` → échecs (méthodes absentes, commande inconnue).
- [ ] **Step 3 :** implémenter. `list` crée `Library(env.libraryDir)`, appelle `migrate(env.paths)` comme `apply`, lit le thème courant depuis `env.paths.themeName` (contenu sans espaces de fin), sort le JSON compact sur `env.out`. Message d'usage : ajouter `klc-light list [--json]` et `klc-light off [--check] [--json]`.
- [ ] **Step 4 :** `bin/test` → tout vert.
- [ ] **Step 5 :** commit `Add klc-light list and remember when the keyboard is off`.

### Task 2 : `klc-light off`

**Files:**
- Modify : `src/cli.cpp`, `README.md` (section « Use »)
- Test : `tests/klc-light_tests.cpp`

**Interfaces:**
- Consumes : `Library::setOff` (Task 1).
- Produces : `runCli` accepte `off [--check] [--json]` ; tout `apply` réussi appelle `setOff(false)`.

- [ ] **Step 1 : tests en échec**
  - `cliOffSendsBlack` : `off` → 0 ; tous les éléments des paquets `0x0e` ont la couleur `00 00 00` et le masque 1 ; `Library::applied()` inchangé ; `off()` vrai. Puis `apply` sans argument → le profil appliqué est envoyé (premier élément = sa couleur), `off()` faux.
  - `cliOffThenThemeTurnsOn` (Review Focus 1) : `off`, puis `apply --theme` (profil lié) → `off()` faux.
  - `cliOffFakeDevice` : `env.fake = true` → rien envoyé, `off()` inchangé (faux), paquets affichés sur la sortie.
  - `cliOffKeyboardAbsent` : le faux lieur renvoie l'erreur `1038:113a` → code 2, `off()` inchangé.
  - `cliOffLocked` (Review Focus 4) : verrou `klc-light.lock` tenu dans le dossier d'exécution → code 1, `off()` inchangé.
  - `cliOffCheck` : `off --check --json` → `"packets"` > 0, rien envoyé, `off()` inchangé.
- [ ] **Step 2 :** `bin/test` → échecs.
- [ ] **Step 3 :** implémenter. Le profil noir : `Profile` par défaut, `base.color` noir, réactif désactivé ; même chemin que `apply` (`--check`, mode simulé, `applyLatest`, codes de sortie), `source` = `"off"`. Après un envoi réussi : `setOff(true)` pour `off`, `setOff(false)` pour tout `apply` (fichier, nom, thème, dernier appliqué). README : les deux commandes, une ligne chacune.
- [ ] **Step 4 :** `bin/test` → tout vert.
- [ ] **Step 5 :** commit `Add klc-light off; any apply turns the keyboard back on`.

### Task 3 : le plugin `swald.klc-light`

**Files:**
- Create : `omarchy-plugin/swald.klc-light/manifest.json`, `BarWidget.qml`, `Panel.qml`, `i18n/en.json`, `i18n/fr.json`

**Interfaces:**
- Consumes : `klc-light list --json` (Task 1), `klc-light off --json` (Task 2), `klc-light apply [<nom>] --json` (existant).
- Produces : le widget de barre.

- [ ] **Step 1 : manifest** selon les contraintes globales (`entryPoints.barWidget` `BarWidget.qml`, `version` `1.0.0`, `author` `swald`, description `Per-key keyboard lighting for MSI laptops (SteelSeries KLC), from KLC Light`). `omarchy plugin validate omarchy-plugin/swald.klc-light` → échoue tant que `BarWidget.qml` manque, puis passe.
- [ ] **Step 2 : `BarWidget.qml`** — structure de `swald.weather` (BarWidget, `Loader` sur `Panel.qml`, `injectPanel`, `open`/`close`/`opened`) et `BarIconButton` comme `swald.taches` : glyphe `󰌌`, `dimmed` quand `off` ou indisponible, `tooltipText` = nom du profil appliqué (ou `strings.t("tooltip.off")`), clic gauche = panneau, droit = `klc-light off --json`, milieu = `klc-light apply --json`. L'état (`list` parsé, `available`, `busy`, `error`) vit dans le widget et est transmis au panneau.
- [ ] **Step 3 : `Panel.qml` et données** — liste des profils (coche `󰄬` sur `applied`, `󰌹` sur le profil dont `theme == currentTheme`, illisibles grisés et inactifs) ; boutons `strings.t("panel.editor")` (lance `klc-light` détaché par `bar.run("uwsm-app -- klc-light")`) et `panel.off` / `panel.on` ; ligne d'erreur. `Process` pour `list` au chargement, à l'ouverture et après chaque commande ; `FileView` sur `~/.config/klc-light/state.json` avec `watchChanges` → relecture. Pendant une commande, `busy` désactive tout. Code 2 → `strings.t("error.absent")` ; autre échec → champ `error` du JSON. Sortie non JSON ou `Process` qui ne démarre pas → `available` faux, panneau `strings.t("unavailable")`.
- [ ] **Step 4 : i18n et essais** — clés : `tooltip.off`, `tooltip.none`, `panel.title`, `panel.editor`, `panel.off`, `panel.on`, `panel.linked`, `panel.unreadable`, `error.absent`, `unavailable` (« KLC Light is not installed or too old »), en anglais et en français. `omarchy plugin validate omarchy-plugin/swald.klc-light` → OK. Installation d'essai : lien symbolique du dossier vers `~/.config/omarchy/plugins/swald.klc-light` (le manuel accepte un dossier lié ; seuls les liens *à l'intérieur* sont refusés), puis `omarchy plugin enable swald.klc-light`. Essais avec l'utilisateur, un envoi à la fois : liste, application d'un profil, éteindre / rallumer, suivi d'une application faite dans l'éditeur ; `klc-light` masqué du `PATH` → message « not installed ».
- [ ] **Step 5 :** commit `Add the Omarchy bar widget`.

### Task 4 : documentation et version

**Files:**
- Modify : `README.md`, `pkgbuild/PKGBUILD` (`pkgver=0.4.0`), `src/version.h` (`"0.4.0"`)

- [ ] **Step 1 :** README, section « Omarchy bar widget » : ce que fait la pastille (clics), installation (`omarchy plugin add <dépôt du plugin>` une fois publié ; d'ici là, copier `omarchy-plugin/swald.klc-light` dans `~/.config/omarchy/plugins/` puis `omarchy plugin enable swald.klc-light`).
- [ ] **Step 2 :** `bin/test` → vert (`versionMatchesPackage` vérifie 0.4.0). Commit `Release 0.4.0`.
