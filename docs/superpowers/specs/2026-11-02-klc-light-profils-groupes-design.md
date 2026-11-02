# klc-light : profils nommés, groupes de touches, sélecteur de couleur

Date : 2026-11-02
Statut : conception validée en discussion, en attente de relecture
Branche : `profils-groupes` (depuis `v1`)
Spec précédente : `2026-10-29-klc-light-design.md`

## 1. Objectif

### Ce que l'utilisateur a demandé

1. Le bouton Annuler semble avoir disparu.
2. Un clic sur une touche du clavier doit basculer automatiquement sur ses réglages (couleur, comportement).
3. La section Keys affiche « 1 key selected » ; elle doit lister les touches et groupes de touches pour les modifier indépendamment.
4. Ouvrir, créer et enregistrer des profils, au lieu d'avoir toujours le profil du thème actif (Tron) ouvert.
5. Les champs R G B du sélecteur de couleur sont laids.

### Constat sur le point 1

Ce n'est pas une régression : le paquet installé (`klc-light 0.1.0-1`, construit le 2026-10-29) précède l'ajout des boutons Annuler/Rétablir (commits `00de1a6`, `724ba4e` du 2026-10-31). Réinstaller avec `bin/install` règle le problème ; la livraison de ce travail le fait.

### Décisions prises en discussion

- Profils : **bibliothèque de profils, lien facultatif avec un thème** Omarchy.
- Section Keys : **groupes nommés, chacun avec son comportement** (fixe, pulsation, vague). Le calque « Keys » et les effets fusionnent en une seule liste de groupes.
- Au lancement : **le profil appliqué sur le clavier** est ouvert.

### Critères de réussite

1. Créer, ouvrir, dupliquer, renommer, supprimer et enregistrer des profils depuis l'interface.
2. Lier un profil à un thème : changer vers ce thème l'applique ; un thème non lié ne change rien au clavier.
3. Un clic sur une touche ouvre son groupe dans le panneau ; une touche libre propose d'en créer un.
4. Le profil Tron actuel, migré, produit exactement les mêmes paquets qu'avant (octet par octet).
5. Le sélecteur de couleur n'utilise plus de champs Material encadrés pour R, G, B.
6. Les 86 tests existants restent verts.

### Hors périmètre

Import/export de profils vers d'autres logiciels, partage de profils, plusieurs claviers, changement de protocole ou de format des paquets, tests sur le vrai clavier pendant le développement (faits ensuite avec l'utilisateur, un envoi à la fois).

## 2. Modèle de données : profil v2

```json
{
  "version": 2,
  "brightness": 100,
  "speed": 100,
  "base": { "type": "static", "color": "#000000" },
  "groups": [
    { "name": "ZQSD", "behavior": "static", "keys": ["w","a","s","d"], "color": "#00e5ff" },
    { "name": "F1–F12", "behavior": "pulse", "keys": ["f1", "…"],
      "start": "#000000", "steps": [{ "color": "#ff0000", "duration": 50 }], "closing": 0 },
    { "name": "Flèches", "behavior": "wave", "keys": ["…"], "steps": [ … ], "origin": "aqua-0" }
  ],
  "reactive": { "enabled": false, "color": "#ffffff", "duration": 30 }
}
```

- `base` et `reactive` : inchangés par rapport à v1.
- `groups` : liste ordonnée. `name` unique et non vide dans le profil. `behavior` vaut `static`, `pulse` ou `wave`.
  - `static` : `color`.
  - `pulse` et `wave` : `start`, `steps` (1 à 16), `closing`, et `origin` pour `wave` — les mêmes champs que `EffectLayer` en v1.
- **Une touche appartient au plus à un groupe.** Ajouter une touche à un groupe la retire de son groupe précédent ; un groupe vidé est supprimé.
- Au plus **6 groupes animés** (`pulse` + `wave`), comptés avec le fond animé comme aujourd'hui par `animatedSlots()` ; au-delà, refus.

### Conversion v1 → v2 (à la lecture)

- `keys` (touche → couleur) : une couleur distincte donne un groupe `static`, nommé « Groupe #rrggbb », dans l'ordre de première apparition dans la disposition.
- `effects` : chaque effet donne un groupe `pulse` ou `wave` portant le même nom et les mêmes champs.
- Si une touche figure dans `keys` et dans un effet, l'effet l'emporte (c'est ce que le clavier affiche aujourd'hui) ; elle quitte le groupe fixe.
- Le fichier v1 n'est pas réécrit ; l'enregistrement produit un fichier v2 dans la bibliothèque.

### Envoi

`Protocol` reçoit la même structure qu'aujourd'hui, reconstituée depuis les groupes : groupes `static` → carte touche → couleur ; groupes animés → liste d'effets dans l'ordre des groupes. **Aucun changement dans `protocol.cpp` ni `effects.cpp`.** Un test vérifie, pour chaque fixture v1, que la conversion v1 → v2 → paquets donne les mêmes octets que v1 → paquets.

## 3. Stockage et application

```
~/.config/klc-light/
  profiles/<nom-de-fichier>.klc.json   un fichier par profil (v2)
  state.json                           { "applied": "<nom>", "themes": { "<thème>": "<nom>" } }
```

- Le nom affiché est stocké dans le fichier (`"name"`). Le nom de fichier en est dérivé (minuscules, caractères non alphanumériques → `-`, suffixe `-2`, `-3` en cas de collision). Renommer renomme aussi le fichier et met à jour `state.json`.
- Écritures atomiques (fichier temporaire puis renommage), comme `saveProfile()` aujourd'hui.
- Lien un pour un : un thème est lié à au plus un profil, un profil à au plus un thème.
- `applied` est mis à jour après chaque envoi réussi (interface ou `klc-light apply`).

### `klc-light apply` (hooks)

- `klc-light apply --theme` (hook `theme-set.d`) : lit le thème actif ; s'il est lié, applique ce profil et met à jour `applied` ; sinon, ne fait rien (code de sortie 0, aucun envoi).
- `klc-light apply` (hook `post-boot.d`) : applique le profil `applied`. Absent : comportement de `--theme`.
- `klc-light apply <nom>` : applique ce profil de la bibliothèque. Un argument qui contient `/` ou finit par `.json` reste un chemin de fichier, comme aujourd'hui.
- Les options existantes `--check`, `--json` et `--experimental-reactive` sont conservées.
- Codes de sortie et notification inchangés (code 2 : KLC absent du bus, -71).
- Les deux hooks, aujourd'hui identiques, divergent : `theme-set.d` appelle `klc-light apply --theme`, `post-boot.d` appelle `klc-light apply`. Copie `.bak` avant modification (règle existante des hooks).

### Migration (premier lancement, quand `~/.config/klc-light/profiles/` n'existe pas)

Pour chaque thème qui a un `keyboard.klc.json` (dans `~/.config/omarchy/themes/<thème>/`, sinon dans le thème stock), ou à défaut un `keyboard.effect` ou un `keyboard.rgb` : créer le profil « <Thème> » (première lettre en capitale), le lier à ce thème. Le profil du thème actif devient `applied`. Les fichiers des thèmes ne sont ni modifiés ni supprimés ; ils ne sont plus lus ensuite. Le fichier `keyboard.klc.json.essai` est ignoré.

Si aucun thème n'a d'éclairage : créer un profil « Défaut » vide (fond noir fixe), non lié, `applied`.

## 4. Interface

### Barre de profil (en haut)

`[Profil ▾]  Nouveau · Dupliquer · Renommer · Supprimer        Lié au thème : [aucun ▾]`

- Liste déroulante : tous les profils, triés par nom ; le profil appliqué porte une marque « appliqué ».
- Changer de profil avec des modifications non enregistrées : dialogue « Enregistrer / Abandonner / Annuler ».
- Nouveau : demande un nom ; profil vide (fond noir fixe, aucun groupe). Dupliquer : copie du profil ouvert, nom « <nom> (copie) » modifiable.
- Renommer : nom modifiable sur place ; vide ou déjà pris → refus avec message sous le champ.
- Supprimer : confirmation ; retire ses liens de thème. Si c'est le profil appliqué, `applied` est vidé et le clavier garde son éclairage. Le profil suivant de la liste s'ouvre ; s'il n'en reste aucun, un profil « Défaut » vide est créé.
- Lié au thème : « aucun » ou un thème Omarchy installé. Choisir un thème déjà lié à un autre profil le déplace sur celui-ci, avec la mention « était lié à <autre> ».
- Le lien est enregistré immédiatement dans `state.json` (ce n'est pas une modification du profil, il n'entre pas dans l'historique d'annulation).

### Barre d'actions (en bas, toujours visible)

`[état]   Annuler · Rétablir · Enregistrer · Appliquer`

- Enregistrer (Ctrl+S) : écrit le profil ouvert, sans envoi.
- Appliquer (Espace) : enregistre, envoie, met à jour `applied`.
- L'historique d'annulation est remis à zéro quand on change de profil.

### Colonne de gauche (remplace PROFILE / LAYERS)

```
FOND        Fixe noir
GROUPES
  ● ZQSD          fixe       4 touches
  ◐ F1–F12        pulsation  12 touches
  ≈ Flèches       vague      4 touches
[+ Groupe depuis la sélection]
Animés : 2 / 6
RÉACTIF (expérimental)
```

Chaque ligne de groupe : pastille de sa couleur (ou première étape), nom, comportement, nombre de touches. Clic : ouvre le groupe et sélectionne ses touches.

### Clic sur le clavier

- Clic sur une touche d'un groupe : ce groupe s'ouvre dans le panneau ; ses touches sont surlignées.
- Clic sur une touche libre : elle est sélectionnée seule ; le panneau propose « Nouveau groupe » avec un nom modifiable (par défaut, l'étiquette de la touche).
- Ctrl+clic, glisser, boutons de groupe (ZQSD, F1–F12…) : sélection libre ; le panneau affiche « N touches : [Créer un groupe] [Ajouter au groupe ▾] ».
- Échap : vide la sélection et revient au fond.

### Panneau de droite, groupe ouvert

Nom (modifiable sur place), comportement (Fixe / Pulsation / Vague), puis :
- Fixe : le sélecteur de couleur.
- Pulsation, vague : les étapes (comme aujourd'hui), origine pour la vague.
- Boutons « Retirer les touches sélectionnées » et « Supprimer le groupe ».
- Passer de Fixe à Pulsation/Vague : une étape créée avec la couleur du groupe ; refus si 6 groupes animés.
- Passer d'animé à Fixe : la couleur devient celle de la première étape.

## 5. Sélecteur de couleur

Les champs `TextField` R, G, B sont remplacés, sous l'anneau et le curseur d'intensité, par :

```
 #00E5FF   [■]
 R ▕██████░░░░░░░░▏   0
 V ▕██████████████▏ 229
 B ▕██████████████▏ 255
 [■][■][■][+][+][+]
```

- Champ hexadécimal sans cadre Material, police à chasse fixe ; accepte `#rrggbb` ou `rrggbb` ; saisie invalide → la valeur précédente revient.
- Un curseur fin par composante, piste en dégradé de (0, g, b) à (255, g, b) pour R (de même pour V et B) ; valeur à droite en chiffres tabulaires.
- Clic sur la valeur : saisie directe ; Haut/Bas ±1, Maj+Haut/Bas ±10, molette ±1 ; Entrée valide, Échap annule.
- Un geste de curseur (appui → relâchement) = une étape d'annulation (`beginGesture`/`endGesture`) ; une saisie validée = une étape.
- Libellés traduits : R/V/B en français, R/G/B en anglais.

## 6. Composants touchés

| Composant | Changement |
|---|---|
| `profile.h/.cpp` | `Group` remplace `keys` + `effects` dans `Profile` ; lecture v1 et v2, écriture v2 ; `validate()` (noms uniques, une touche par groupe, 6 animés) ; fonction qui reconstitue la carte par touche et les effets pour `Protocol` et `Preview` |
| `library.h/.cpp` (nouveau) | Bibliothèque : lister, lire, créer, dupliquer, renommer, supprimer ; `state.json` (applied, liens) ; migration depuis les thèmes |
| `themeprofile.h/.cpp` | Ne sert plus qu'à la migration (lecture des fichiers de thème) |
| `cli.cpp` | `apply`, `apply --theme`, `apply <nom>` selon §3 |
| `backend.h/.cpp` | Profil ouvert + bibliothèque ; groupes au lieu des calques keys/effets ; enregistrer sans envoyer ; clic touche → groupe |
| `Main.qml` | Barre de profil ; bouton Enregistrer ; Ctrl+S |
| `LayerList.qml` → `GroupList.qml` | Liste fond / groupes / réactif |
| `EffectPanel.qml` | Panneau de groupe, actions sur la sélection |
| `ColorPicker.qml` | Hexadécimal + curseurs RGB |
| `i18n/fr.json` | Nouvelles chaînes |
| hooks Omarchy | `apply --theme` pour `theme-set.d`, `apply` pour `post-boot.d` |

`protocol.*`, `effects.*`, `device.*`, `hidapitransport.*`, `sudobinder.*`, `layout*`, `preview.*` (hors adaptation de l'entrée) ne changent pas.

## 7. Erreurs

- Profil illisible ou invalide : message dans le bandeau d'erreur, profil non chargé, fichier jamais écrasé ; il reste visible dans la liste, marqué « illisible ».
- `state.json` absent ou corrompu : repart sans liens ni `applied` ; ouvre le premier profil ; le fichier corrompu est conservé en `state.json.bad`.
- Nom vide ou en double : refusé à la saisie.
- 7e groupe animé : refus, message « 6 groupes animés au maximum (limite du contrôleur) ».
- Écriture impossible (disque, droits) : message, modifications conservées en mémoire, état toujours « non enregistré ».

## 8. Tests

Ajoutés aux 86 existants (qui restent verts) :

- Profil : conversion v1 → v2 (couleurs regroupées, effets en groupes, conflit touche fixe/effet) ; aller-retour v2 ; rejet des noms en double, des touches en double, de plus de 6 animés.
- Paquets : pour chaque fixture v1, v1 → v2 → paquets identiques à v1 → paquets.
- Bibliothèque : créer, dupliquer, renommer (fichier et `state.json`), supprimer (liens retirés, `applied` vidé), noms de fichiers en collision, `state.json` corrompu.
- Migration : thèmes avec `keyboard.klc.json` (utilisateur et stock), `keyboard.effect`, `keyboard.rgb`, aucun ; `.essai` ignoré ; idempotente.
- CLI (avec un faux émetteur) : `apply --theme` lié / non lié (aucun envoi), `apply` au démarrage, `apply <nom>`, mise à jour de `applied`.
- Interface : clic sur une touche groupée ouvre son groupe ; touche libre → « Nouveau groupe » ; création depuis la sélection ; déplacer une touche entre groupes ; changement de profil avec modifications → dialogue ; champ hexadécimal ; curseur RGB = une étape d'annulation.

## 9. Livraison

- Développement sur `profils-groupes` ; aucun envoi au clavier pendant le développement.
- Réinstallation avec `bin/install` (ce qui rend aussi visibles Annuler/Rétablir).
- Mise à jour des hooks avec copie `.bak`.
- Tests matériels ensuite avec l'utilisateur, un envoi à la fois : le profil Tron migré d'abord (doit être identique), puis un groupe fixe, puis un groupe animé.
