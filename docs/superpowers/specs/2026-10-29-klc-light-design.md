# klc-light : éditeur d'éclairage clavier pour MSI GS76 sous Omarchy

Date : 2026-10-29
Statut : conception validée en discussion, en attente de relecture

## 1. Objectif

Remplacer, pour le clavier RGB par touche du MSI GS76 Stealth 11UH (contrôleur SteelSeries KLC, USB `1038:113a`, firmware 1.02), la partie « Mystic Light » de MSI Center et l'éditeur d'éclairage de SteelSeries GG par une application **open source, native Linux**, intégrée à Omarchy.

### Ce que l'utilisateur a demandé

- Un équivalent open source et Linux de Mystic Light, compatible avec ce système.
- But principal : une **interface graphique** pour régler l'éclairage du clavier.
- **Aperçu à l'écran** ; rien n'est envoyé au clavier avant un clic sur « Appliquer ».
- **Un profil par thème Omarchy** : changer de thème recharge l'éclairage de ce thème.
- Types d'éclairage en v1 : couleurs fixes par touche, effets animés (vagues, pulsation), mode réactif à la frappe, effets de base de Mystic Light.
- Approche : application C++/Qt Quick native, protocole réécrit en C++.
- Disposition de l'interface : trois colonnes, façon Mystic Light (maquette A).

### Critères de réussite

1. Depuis l'éditeur, peindre des touches, choisir un effet, régler couleur, vitesse et luminosité, voir l'aperçu, puis appliquer en un clic.
2. Le profil est enregistré avec le thème actif et réappliqué au démarrage et à chaque changement de thème.
3. Les effets actuels `ares` et `tron` donnent sur le clavier exactement le même résultat qu'aujourd'hui.
4. Aucun envoi ne bloque le contrôleur pendant le développement ni à l'usage normal.

### Hors périmètre

Synchronisation de plusieurs appareils, souris Razer DeathAdder 2013 (éclairage vert uniquement), Philips Hue / Nanoleaf (Ambient Link), Dynamic Dashboard, aperçu ou envoi en direct sur le clavier, autres modèles de portables MSI.

## 2. Contexte matériel et existant

- Le KLC n'affiche un nouvel éclairage qu'une fois **détaché** du pilote USB : attacher, envoyer, détacher. Cela passe par `/usr/local/bin/msi-kbd-usb bind|unbind` (sudo sans mot de passe déjà configuré ; gère le cas `-71` où le pilote est attaché sans configuration).
- **Les envois répétés ou inconnus ont déjà bloqué le contrôleur** (récupération : extinction complète). Un seul envoi à chaque fois.
- Le protocole validé sur le firmware 1.02 est celui de `msi-perkeyrgb` : paquets de couleurs par région `0E 00 <région> …` (régions alphanum `0x2a`, entrée `0x0b`, modificateurs `0x18`, pavé numérique `0x24`), paquets d'effet `0B 00 <index> …`, puis le rafraîchissement `09` envoyé **en output report** (sans lui, les effets ne s'affichent pas).
- Existant à remplacer à terme : hooks `~/.config/omarchy/hooks/{post-boot.d,theme-set.d}/msi-keyboard-rgb`, `~/.local/bin/msi-perkeyrgb`, `~/.local/share/msi-perkeyrgb-setup/msi-kbd-effect` (effets `ares` et `tron` bâtis sur le preset `aqua`).
- Spécifications SteelSeries GG déchiffrées : `~/Work/klc-re/decrypted/all/` (`ss-klc-base.lisp` : 19 emplacements d'effets `effect_0`…`effect_18` ; `common_lighting.lisp` : structure `lighting_effect` avec type, direction, `focal_x`/`focal_y`, vitesse, 14 couleurs ; `msi-ge76-klc.lisp` : disposition des touches).
- Omarchy assemble le thème actif en copiant le thème stock puis `~/.config/omarchy/themes/<thème>/` par-dessus. Convention native : `keyboard.rgb` (une couleur).

## 3. Architecture

Un seul exécutable, `klc-light`, en C++ et Qt Quick, compilé avec qmake6 selon les conventions `omarchy-app`, suivant le thème Omarchy, installé en paquet pacman avec une entrée dans le lanceur.

- `klc-light` : ouvre l'éditeur.
- `klc-light apply [chemin-profil]` : envoie au clavier le profil donné, ou celui du thème actif, sans interface. Appelé par les hooks.

| Unité | Rôle | Dépend de |
|---|---|---|
| Layout | Les touches du GS76 : position, taille, étiquette, code HID, région | — |
| Profile | Modèle de données, lecture et écriture JSON, validation | Layout |
| Effects | Construit les effets Mystic Light et personnalisés en définitions firmware | Profile |
| Protocol | Profil → suite exacte de paquets HID. Code pur, sans accès matériel | Profile, Effects |
| Device | Seul code qui touche au matériel : bind, envoi unique via hidraw, unbind | Protocol |
| Preview | Calcule l'animation à l'écran avec la même logique de transitions que le firmware | Profile, Effects |
| UI (QML) | Dessin du clavier, sélection, réglages, bouton Appliquer | toutes sauf Device en direct |

Seule l'unité Device communique avec le clavier, et uniquement sur « Appliquer » ou `apply`.

**Layout.** Construite à partir des keymaps de `msi-perkeyrgb` (modèle GS75/GS76, codes HID et régions) et de la disposition de `msi-ge76-klc.lisp`. Les étiquettes affichées suivent la disposition XKB active (AZERTY ici) ; les profils stockent des identifiants de touche indépendants de la disposition (noms HID).

## 4. Profils et lien avec les thèmes

### Emplacement

Le profil du thème actif est enregistré dans `~/.config/omarchy/themes/<thème>/keyboard.klc.json`. Ça marche aussi pour les thèmes stock, sans modifier `~/omarchy` ni `/usr/share/omarchy`.

### Ordre de recherche de `apply` (sans argument)

1. `keyboard.klc.json` dans `~/.config/omarchy/themes/<thème actif>/`, sinon dans `~/.local/state/omarchy/current/theme/` ;
2. sinon `keyboard.effect` : `ares` et `tron` sont des profils intégrés, reproduisant exactement `msi-kbd-effect` ;
3. sinon `keyboard.rgb` : toutes les touches de cette couleur ;
4. sinon rien n'est envoyé ; sortie 0.

Le nom du thème actif est lu dans `~/.local/state/omarchy/current/theme.name`.

### Format (version 1)

```json
{
  "version": 1,
  "brightness": 100,
  "speed": 100,
  "base": { "type": "static", "color": "#00E5FF" },
  "keys": { "KEY_W": "#FF0000", "KEY_A": "#FF0000" },
  "effects": [
    { "name": "vague", "type": "wave", "keys": ["KEY_F1", "KEY_F2"],
      "steps": [["#FF0000", 14], ["#FFAA00", 10]], "origin": "aqua-0" }
  ],
  "reactive": { "enabled": false, "color": "#FFFFFF", "duration": 30 }
}
```

- `base.type` : `static`, `breathing`, `flashing`, `rainbow` ou `wave`, appliqué à tout le clavier, avec couleur(s) selon le type.
- `keys` : couleurs fixes par touche, prioritaires sur `base`.
- `effects` : effets animés personnalisés attribués à des groupes de touches ; `steps` = suite (couleur, durée en unités du contrôleur), le cycle revient seul à la première couleur.
- `brightness`, `speed` : pourcentages 1–100 (luminosité) et 25–400 (vitesse), appliqués à la construction des paquets (couleurs multipliées, durées divisées). Le réglage matériel Fn+↑ reste indépendant.
- `reactive` : couleur et durée d'allumage à la frappe ; expérimental (§6).

### Validation (avant tout envoi)

Touches connues ; couleurs `#RRGGBB` ; au plus 16 transitions par effet ; nombre d'effets animés distincts (la base comprise si elle est animée ; les couleurs fixes n'occupent pas d'emplacement) au plus égal au nombre d'emplacements validés (6 en v1, jusqu'à 19 après validation) ; une touche appartient à un seul effet ; `version` reconnue. Un profil invalide n'est jamais envoyé : l'éditeur affiche l'erreur, `apply` sort avec un code non nul et un message dans le journal.

### Origine des vagues

v1 : choix parmi les origines présentes dans les presets capturés (`aqua-0`…). Tâche d'analyse hors matériel : faire correspondre les octets des paquets `0B` validés à la structure `lighting_effect` de GG (`focal_x`, `focal_y`, direction). Si la correspondance est établie et validée sur le matériel, l'éditeur propose une origine libre (clic sur une touche) et une direction ; sinon on garde les origines `aqua`.

## 5. Envoi et gestion des erreurs

### Déroulement

1. Construire et valider tous les paquets en mémoire ; rien ne part en cas d'échec.
2. `sudo -n /usr/local/bin/msi-kbd-usb bind`.
3. Attendre l'apparition du hidraw `1038:113a` (délai maximal 3 s).
4. Envoyer la suite **une seule fois**, dans l'ordre validé, rafraîchissement `09` final en output report.
5. `sudo -n /usr/local/bin/msi-kbd-usb unbind`, **toujours**, y compris après un échec.

### Règles de sécurité

- Aucune nouvelle tentative automatique.
- Verrou exclusif `$XDG_RUNTIME_DIR/klc-light.lock` : un envoi à la fois (éditeur et hooks confondus).
- Délai minimal entre deux envois (valeur de départ 5 s, ajustée pendant la validation) ; « Appliquer » grisé pendant ce délai.
- Seuls les types de paquets validés sur ce firmware sont envoyés. Le mode réactif reste désactivé (marqué « expérimental ») tant qu'il n'est pas validé (§7).

### Messages d'erreur

| Situation | Message |
|---|---|
| `msi-kbd-usb` absent ou sudo refusé | Indique la commande d'installation |
| Le hidraw n'apparaît pas après `bind` | Erreur `-71` probable : débrancher le port (cycle d'alimentation) ou éteindre complètement |
| Envoi accepté mais clavier éteint | Vérifier la luminosité (Fn+↑) et le verrou Fn+Esc |

Éditeur : bandeau d'erreur. `apply` : message dans le journal (`journalctl --user`), sortie sans bloquer le démarrage ni le changement de thème.

## 6. Interface (disposition A)

Trois colonnes :

- **Gauche, Profil et calques** : thème actif et fichier du profil ; liste des calques (Base, Touches, chaque effet, Réactif « expérimental ») ; ajout et suppression de calques.
- **Centre, clavier** : dessin du GS76 aux étiquettes de la disposition active, animé par Preview. Clic = une touche ; glisser = rectangle ; groupes prédéfinis (ZQSD/WASD, F1–F12, rangée des chiffres, pavé numérique, modificateurs, tout).
- **Droite, réglages du calque sélectionné** : liste d'effets ; cercle de teintes avec valeurs RVB 0–255 et couleurs enregistrées (emplacements personnalisés, comme Mystic Light) ; transitions de l'effet (couleur, durée) ; curseurs de vitesse et de luminosité ; bouton **Appliquer**.

Les modifications ne sont enregistrées et envoyées qu'avec « Appliquer » ; un indicateur signale les modifications non appliquées. L'application suit les couleurs du thème Omarchy comme les autres applications Omarchy.

## 7. Tests et validation

### Automatiques, sans matériel

- **Protocol, octet par octet** : pour des profils de référence (tout le clavier d'une couleur, ZQSD en rouge, chaque effet Mystic Light, `ares`, `tron`), les paquets C++ doivent être identiques à ceux de `msi-perkeyrgb` / `msi-kbd-effect`, produits par un script de référence Python qui n'ouvre aucun périphérique. Les effets sans équivalent Python sont vérifiés contre la structure de `ss-klc-base` / `common_lighting`.
- **Profile** : lecture, écriture, rejet des profils invalides, ordre de recherche klc.json → effect → rgb.
- **Effects / Preview** : durées après réglage de vitesse, couleurs après réglage de luminosité, transitions de l'aperçu.
- **Device** avec un faux périphérique : ordre bind → envoi → unbind, unbind après échec, aucune nouvelle tentative, verrou, délai minimal.

### Sur le matériel, avec l'utilisateur, une étape à la fois

Avant chaque envoi : description exacte de ce qui part ; un seul envoi ; observation par l'utilisateur ; étape suivante seulement si la précédente est bonne.

1. Couleur fixe sur tout le clavier.
2. Couleurs par touche (ZQSD en rouge).
3. `ares` et `tron` envoyés par klc-light : résultat identique à aujourd'hui.
4. Effets Mystic Light (respiration, clignotement, arc-en-ciel, vague).
5. Origine de vague libre, si l'analyse du §4 a abouti.
6. Mode réactif, en dernier, après accord explicite. En cas de refus ou de blocage : reste désactivé ; récupération par extinction complète ou cycle d'alimentation du port.

## 8. Installation et migration

- Paquet pacman local (PKGBUILD), entrée dans le lanceur.
- Hooks `post-boot.d` et `theme-set.d` réécrits pour appeler `klc-light apply`.
- `msi-perkeyrgb`, `msi-kbd-effect` et les anciens hooks restent en place jusqu'à la fin de la validation sur le matériel, pour pouvoir revenir en arrière ; ils sont retirés ensuite, avec l'accord de l'utilisateur.
- `msi-kbd-usb` est conservé tel quel.
