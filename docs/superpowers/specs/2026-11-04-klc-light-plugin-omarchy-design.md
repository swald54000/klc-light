# klc-light : plugin de barre Omarchy

Date : 2026-11-04
Statut : conception validée en discussion, en attente de relecture
Branche : `v1`
Modèle suivi : le plugin `swald.taches` (dossier `omarchy-plugin/` dans le dépôt de
l'application, `Commons.Translations`, `Process` sur la ligne de commande)

## 1. Objectif

### Ce que l'utilisateur a demandé

Un widget de barre Omarchy pour KLC Light : voir le profil actif, en changer sans ouvrir
l'éditeur, ouvrir l'éditeur, et pouvoir le publier sur la marketplace (plugins.omarchy.org).

### Décisions prises en discussion

- **Public visé dès la conception** (option B) : interface en anglais traduite en
  français, comportement propre sans `klc-light` installé. La publication (dépôt public,
  AUR, marketplace) est un **sous-projet séparé**, hors de ce document.
- Fonctions retenues : pastille, panneau de profils, ouvrir l'éditeur, éteindre /
  rallumer. Écartées : luminosité depuis la barre, aperçu des couleurs des profils.
- Le plugin ne parle jamais au clavier : il passe par la ligne de commande, qui garde le
  verrou, l'intervalle entre envois et le mode simulé.

### Critères de réussite

1. Depuis la barre, l'utilisateur voit le profil appliqué et en applique un autre en deux
   clics.
2. Éteindre puis rallumer rend exactement le profil d'avant, sans le modifier.
3. Le panneau suit les changements faits ailleurs (éditeur, changement de thème) sans
   rouvrir.
4. Sans `klc-light`, le widget ne casse pas la barre et dit quoi installer.
5. `omarchy plugin validate omarchy-plugin/swald.klc-light` passe.
6. Les tests existants restent verts.

### Hors périmètre

Publication (dépôt séparé, AUR, fiche marketplace), luminosité, aperçu des couleurs,
édition de profils depuis la barre, prise en charge d'autres modèles de clavier.

## 2. Ligne de commande : deux ajouts

### `klc-light list [--json]`

Ne touche pas au clavier. Sortie JSON (une ligne) :

```json
{"applied":"Tron","off":false,"currentTheme":"tron",
 "profiles":[{"name":"Jarvis","theme":"","unreadable":false},
             {"name":"Tron","theme":"tron","unreadable":false}]}
```

- `applied` : le dernier profil appliqué (`state.json`), `""` si aucun.
- `off` : vrai après `klc-light off`, jusqu'au prochain envoi réussi.
- `currentTheme` : le contenu de `~/.local/state/omarchy/current/theme.name`, `""` sinon.
- `profiles` : triés par nom ; `theme` est le thème lié, `""` sinon ; `unreadable` marque
  un fichier illisible (comme dans la liste de l'éditeur).
- Sans `--json` : un profil par ligne, `*` devant le profil appliqué.
- Code de sortie 0 ; 1 si la bibliothèque est illisible.

### `klc-light off [--json] [--check]`

- Envoie un profil fixe tout noir (toutes les touches à `#000000`, sans réactif), par le
  même chemin que `apply` : verrou, intervalle, mode simulé (`KLC_LIGHT_FAKE_DEVICE=1`
  affiche les paquets), codes de sortie (2 : clavier absent).
- En cas de succès, `state.json` gagne `"off": true` ; `applied` est inchangé.
- Tout `apply` réussi (y compris `apply --theme` et `apply` sans argument) remet
  `"off": false`. `apply` sans argument rallume donc le profil d'avant.
- Les hooks Omarchy ne changent pas : un changement de thème rallume le clavier.

`state.json` reste lisible par les versions précédentes : `off` est un champ de plus,
absent vaut faux.

## 3. Le plugin `swald.klc-light`

Dossier `omarchy-plugin/swald.klc-light/` : `manifest.json`, `BarWidget.qml`,
`Panel.qml`, `i18n/en.json`, `i18n/fr.json`.

### Manifest

`schemaVersion` 1, `id` `swald.klc-light`, `name` `KLC Light`, `kinds` `["bar-widget"]`,
`entryPoints.barWidget` `BarWidget.qml`, `barWidget` : `displayName` `Keyboard lighting`,
`category` `System`, `allowMultiple` faux.

### Pastille

- Icône clavier (glyphe Nerd Font), atténuée quand `off` est vrai ou quand `klc-light`
  manque.
- Bulle au survol : le nom du profil appliqué (« off » quand éteint).
- Clic gauche : panneau. Clic droit : `klc-light off`. Clic du milieu : `klc-light apply`.

### Panneau

- La liste des profils : coche sur le profil appliqué, icône de lien sur celui lié au
  thème courant, profils illisibles grisés et non cliquables. Un clic lance
  `klc-light apply <nom> --json`.
- Deux boutons : « Open editor » (`klc-light`, détaché) et « Turn off » / « Turn on »
  selon `off`.
- Pendant un envoi, les commandes sont désactivées (un envoi à la fois, comme l'éditeur).
- Erreurs, sur une ligne sous la liste, d'après la sortie `--json` et le code de sortie :
  clavier absent (2), autre échec (le message de `klc-light`).

### Données

- `klc-light list --json` au chargement, à l'ouverture du panneau et après chaque
  commande.
- Surveillance de `~/.config/klc-light/state.json` : à chaque changement, la liste est
  relue, ce qui suit l'éditeur et les hooks.
- `klc-light` introuvable (le `Process` ne démarre pas) : panneau « KLC Light is not
  installed », avec l'adresse du projet ; aucune commande proposée.

### Langue

Chaînes en anglais dans le QML, traduites par `i18n/fr.json` via `Commons.Translations`,
comme `swald.taches`.

## 4. Tests

- `cliList` : deux profils, l'un lié au thème courant, un fichier illisible → JSON attendu
  (ordre, `theme`, `unreadable`, `currentTheme`, `applied`, `off`).
- `cliOffSendsBlack` : paquets tout noirs, `applied` inchangé, `off` vrai ; puis
  `apply` sans argument → profil d'avant envoyé, `off` faux.
- `cliOffFakeDevice` : avec le mode simulé, rien n'est envoyé et `off` ne change pas.
- `cliOffKeyboardAbsent` : code 2, `off` inchangé.
- `libraryOffState` : `off` lu, écrit, absent = faux, conservé par `setApplied` sauf
  remise à faux explicite.
- Plugin : `omarchy plugin validate`, puis essai dans la barre de l'utilisateur (liste,
  application, éteindre / rallumer, suivi d'un changement fait dans l'éditeur, absence
  simulée de `klc-light` en changeant le nom de la commande).

## 5. Livraison

Commits sur `v1` : ligne de commande et tests, puis plugin, puis README (section
« Omarchy bar widget » : `omarchy plugin add`, ou copie / lien du dossier dans
`~/.config/omarchy/plugins/`). Version 0.4.0.
