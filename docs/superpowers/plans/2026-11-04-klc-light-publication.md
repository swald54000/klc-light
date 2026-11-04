# klc-light : publication — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publier KLC Light (GitHub, AUR) et son widget (GitHub, marketplace Omarchy), avec un accès au clavier sans root par le fichier sysfs `authorized` et un groupe `klc-light`.

**Architecture:** Un `SysfsBinder` remplace `SudoBinder` derrière l'interface `Binder` existante ; le paquet installe une règle udev et un fichier sysusers. Le dépôt devient autonome (README public, `gen_layout.py` sans GG), puis il est poussé sur GitHub ; un PKGBUILD AUR construit depuis le tag ; le plugin a son propre dépôt.

**Tech Stack:** C++17 / Qt 6, qmake6, udev, systemd-sysusers, makepkg / namcap, git / gh.

**Spec:** `docs/superpowers/specs/2026-11-04-klc-light-publication-design.md`

## Global Constraints

- Contrôleur : USB `1038:113a`, trouvé par `idVendor` / `idProduct` sous `/sys/bus/usb/devices`.
- `bind` = écrire `0` puis `1` dans `authorized` ; `unbind` = écrire `0`. Jamais d'écriture dans `/sys/bus/usb/drivers/usb/bind|unbind`.
- Groupe `klc-light` ; `authorized` du contrôleur en `0664`, groupe `klc-light` ; `hidraw` en `uaccess`.
- Message d'écriture refusée, exact : `Add your user to the klc-light group (sudo usermod -aG klc-light $USER), then log in again.`
- Un contrôleur introuvable garde le message et le code de sortie 2 actuels (texte contenant `1038:113a`).
- Versions : application 0.5.0 (`src/version.h`, `pkgbuild/PKGBUILD`, `aur/PKGBUILD`), plugin 1.1.0.
- GitHub : `swald54000/klc-light` (branche `main`, tag `v0.5.0`), `swald54000/omarchy-klc-light`.
- **Toute action hors de la machine (création de dépôt, push, release, envoi AUR) attend l'accord explicite de l'utilisateur à ce moment-là.**
- Aucun fichier SteelSeries GG dans le dépôt ni dans les paquets.
- Tests : `bin/test` ; les 264 tests restent verts (sauf `binderMissingHelper`, réécrit en Tâche 1).
- Commits en anglais, impératif, `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. Après une extinction complète, le contrôleur revient `authorized=1` avec son pilote : le premier `bind` (hook de démarrage) doit marcher → vérification réelle (Tâche 3, Step 4).
2. La règle udev doit s'appliquer à chaque réapparition du contrôleur (`add`), pas seulement au premier démarrage : `authorized` garde le groupe après un cycle → vérification réelle (Tâche 3, Step 3).
3. Utilisateur hors du groupe : message exact du groupe, code 1, pas de code 2 trompeur → test `sysfsBinderPermissionDenied` (Tâche 1).
4. `gen_layout.py` sans le fichier GG doit régénérer un `layoutdata.cpp` identique à l'existant → test `tools/test_gen_layout_fallback.sh` (Tâche 2).
5. L'archive du tag GitHub doit se compiler et passer ses tests sans rien hors du dépôt → `makepkg` dans un dossier vierge (Tâche 5, Step 2).

---

### Task 1 : `SysfsBinder`, règle udev, sysusers

**Files:**
- Create : `src/sysfsbinder.h`, `src/sysfsbinder.cpp`, `pkgbuild/70-klc-light.rules`, `pkgbuild/klc-light.sysusers`
- Delete : `src/sudobinder.h`, `src/sudobinder.cpp`
- Modify : `src/main.cpp` (deux `SudoBinder binder;`), `pkgbuild/PKGBUILD` (installe les deux fichiers), `tests/klc-light_tests.cpp`

**Interfaces:**
- Produces : `class SysfsBinder : public Binder { public: explicit SysfsBinder(QString devicesDir = "/sys/bus/usb/devices"); bool bind(QString *error) override; bool unbind(QString *error) override; protected: virtual bool writeAuthorized(const QString &path, const QByteArray &value, QString *error); };` — après le `1`, `bind` vérifie la présence du lien `driver` du périphérique.

- [ ] **Step 1 : tests en échec** — faux dossier `<tmp>/devices/3-12/` avec `idVendor` = `1038\n`, `idProduct` = `113a\n`, `authorized` = `1\n`, un lien `driver`, et un autre périphérique `1-1` (`046d` / `c52b`) :
  - `sysfsBinderCycle` : une sous-classe de test surcharge `writeAuthorized` pour journaliser ses écritures ; `bind` → vrai, écritures `["0", "1"]` sur `<tmp>/devices/3-12/authorized` ; `unbind` → `["0"]`.
  - `sysfsBinderMissing` : aucun `1038:113a` → faux, message contenant `1038:113a`.
  - `sysfsBinderPermissionDenied` (Review Focus 3) : `authorized` en lecture seule (`chmod 0444`) → faux, message égal à la contrainte globale.
  - `sysfsBinderDriverDetached` : pas de lien `driver` après le `1` → faux, message contenant `power` (extinction complète).
  - `binderMissingHelper` (existant, ligne ~1452) réécrit sur `SysfsBinder("/nonexistent")` → message `1038:113a`.
- [ ] **Step 2 :** `bin/test` → échecs (classe absente).
- [ ] **Step 3 :** implémenter ; `writeAuthorized` par défaut ouvre et écrit le fichier (erreur de permission → message du groupe). Supprimer `SudoBinder` et ses usages ; `main.cpp` construit `SysfsBinder`.
- [ ] **Step 4 :** `pkgbuild/70-klc-light.rules` :
  ```
  SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1038", ATTRS{idProduct}=="113a", TAG+="uaccess"
  ACTION=="add", SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="1038", ATTR{idProduct}=="113a", RUN+="/usr/bin/chgrp klc-light /sys%p/authorized", RUN+="/usr/bin/chmod 0664 /sys%p/authorized"
  ```
  `pkgbuild/klc-light.sysusers` : `g klc-light - -`. `PKGBUILD` les installe dans `/usr/lib/udev/rules.d/70-klc-light.rules` et `/usr/lib/sysusers.d/klc-light.conf`.
- [ ] **Step 5 :** `bin/test` → vert ; `udevadm verify pkgbuild/70-klc-light.rules` → pas d'erreur. Commit `Attach the controller through its sysfs authorized file, without root`.

### Task 2 : dépôt autonome et public

**Files:**
- Modify : `README.md`, `tools/gen_layout.py`, `tools/gen_fixtures.py` (en-tête), `src/version.h`, `pkgbuild/PKGBUILD`, `omarchy-plugin/swald.klc-light/manifest.json` (1.1.0)
- Create : `tools/test_gen_layout_fallback.sh`

- [ ] **Step 1 : test en échec** — `tools/test_gen_layout_fallback.sh` : copie le dépôt dans un dossier temporaire, lance `GG_LAYOUT=/nonexistent python3 -I tools/gen_layout.py` et vérifie que `src/layoutdata.cpp` est inchangé (`cmp`). Il échoue aujourd'hui (le script lit le fichier GG sans condition).
- [ ] **Step 2 :** `gen_layout.py` : `GG_LAYOUT` lu depuis l'environnement (défaut actuel) ; absent → positions `cx, cy` reprises par code HID depuis le `src/layoutdata.cpp` existant, avec un message `GG layout not found, keeping the positions in src/layoutdata.cpp`. Le test passe.
- [ ] **Step 3 :** README public (sections : What it is · Supported hardware · Install (AUR, group, log in again) · Use (editor, CLI, Omarchy hooks, bar widget) · Known limits (power key = F1's twin, LED-matrix ghosting, one send at a time, 18 animated groups) · How it works / sources · Development (`bin/test`, fake device, optional tools) · License). Corriger aussi la phrase sur `list` sans `--json` (noms seulement). En-tête de `gen_fixtures.py` : outil optionnel, msi-perkeyrgb requis.
- [ ] **Step 4 :** versions 0.5.0 et plugin 1.1.0 ; `bin/test` → vert (`versionMatchesPackage`). Commit `Make the repository self-contained for publication`.

### Task 3 : bascule de la machine de l'utilisateur (avec lui)

- [ ] **Step 1 :** l'utilisateur lance `! ~/Work/klc-light/bin/install` (crée le groupe, installe la règle), puis `! sudo usermod -aG klc-light $USER` et `! sudo udevadm trigger --action=add --subsystem-match=usb --attr-match=idVendor=1038`.
- [ ] **Step 2 :** vérifier (lecture seule) : `stat -c '%G %a' /sys/bus/usb/devices/<port>/authorized` → `klc-light 664`. L'utilisateur ferme et rouvre sa session ; `id -nG` contient `klc-light`.
- [ ] **Step 3 (Review Focus 2) :** l'utilisateur applique deux profils (`! klc-light apply Jarvis`, `! klc-light apply Tron`) : couleurs correctes, aucune demande de mot de passe ; `authorized` garde `klc-light 664`.
- [ ] **Step 4 (Review Focus 1) :** extinction complète et redémarrage : le hook de démarrage remet le profil ; `stat` → `klc-light 664`.
- [ ] **Step 5 :** retirer l'ancien mécanisme (commandes données à l'utilisateur) : `/usr/local/bin/msi-kbd-usb`, la règle `sudoers` correspondante (`sudo visudo -c` ensuite), `/etc/udev/rules.d/70-msi-steelseries-klc.rules`. Un dernier `klc-light apply Tron` doit marcher. Consigner dans `docs/origins.md` (mesures `authorized`). Commit `Record the sysfs authorized measurements`.

### Task 4 : GitHub (accord explicite avant chaque commande)

- [ ] **Step 1 :** `gh repo create swald54000/klc-light --public --description "Per-key RGB lighting editor for MSI laptops (SteelSeries KLC), made for Omarchy" --source . --remote origin` puis `git push -u origin main`.
- [ ] **Step 2 :** `git tag -a v0.5.0 -m "KLC Light 0.5.0"`, `git push origin v0.5.0`, `gh release create v0.5.0 --title "KLC Light 0.5.0" --notes-file <notes>` (notes : nouveautés depuis 0.2.0, installation).
- [ ] **Step 3 :** `tools/publish_plugin.sh <dossier>` : recopie `omarchy-plugin/swald.klc-light/*` (sans lien ni `.git`), plus `LICENSE` et un `README.md` court (ce que fait le widget, `omarchy plugin add …`, besoin de KLC Light). Créer `swald54000/omarchy-klc-light`, y lancer le script, `omarchy plugin validate` sur ce dossier, commit, push, tag `v1.1.0`.

### Task 5 : AUR

**Files:**
- Create : `aur/PKGBUILD`, `aur/klc-light.install`, `aur/.SRCINFO`

- [ ] **Step 1 :** `aur/PKGBUILD` : `source=("$pkgname-$pkgver.tar.gz::https://github.com/swald54000/klc-light/archive/refs/tags/v$pkgver.tar.gz")`, `sha256sums` calculé sur l'archive réelle (`updpkgsums`), `depends` actuels + `systemd`, `makedepends` `qt6-base qt6-declarative` (qmake6) + `gcc make`, `build()` : `qmake6 && make` dans `klc-light-$pkgver`, `check()` : `bin/test`, `package()` : binaire, licence, icône, `.desktop`, règle udev, sysusers, `install=klc-light.install` (message `post_install` / `post_upgrade` : la commande `usermod` et la reconnexion). `makepkg --printsrcinfo > aur/.SRCINFO`.
- [ ] **Step 2 (Review Focus 5) :** dans un dossier vierge : `makepkg -s` (compile, `check()` vert), `namcap aur/PKGBUILD`, `namcap klc-light-0.5.0-1-x86_64.pkg.tar.zst` → pas d'erreur (avertissements examinés). Commit `Add the AUR package`.
- [ ] **Step 3 :** commandes d'envoi pour l'utilisateur : `git clone ssh://aur@aur.archlinux.org/klc-light.git`, copie de `PKGBUILD`, `klc-light.install`, `.SRCINFO`, commit, `git push` (son compte et sa clé).

### Task 6 : marketplace

- [ ] **Step 1 :** `docs/marketplace-submission.md` : nom, dépôt `https://github.com/swald54000/omarchy-klc-light`, id `swald.klc-light`, catégorie, description, prérequis (KLC Light, groupe), emplacement de la capture d'écran que l'utilisateur fournit. Commit `Draft the marketplace submission`.
- [ ] **Step 2 :** l'utilisateur ouvre la soumission sur `omacom/omarchy-plugin-marketplace` (formulaire « [Plugin]: »), avec le texte et la capture.
