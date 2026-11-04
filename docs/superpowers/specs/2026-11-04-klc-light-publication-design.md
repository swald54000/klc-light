# klc-light : publication

Date : 2026-11-04
Statut : conception validée en discussion, en attente de relecture
Branche : `v1`
Spec précédente : `2026-11-04-klc-light-plugin-omarchy-design.md`

## 1. Objectif

Rendre KLC Light et son widget de barre installables par d'autres utilisateurs d'Omarchy
et d'Arch : dépôts GitHub publics, paquet AUR, fiche sur la marketplace Omarchy.

### Décisions prises en discussion

- **Accès au clavier sans root** : le contrôleur est connecté / déconnecté par son seul
  fichier sysfs `authorized`, rendu modifiable par un **groupe dédié `klc-light`**
  (option A). Mesuré sur le GS76 : deux cycles `authorized` 0 → 1, envoi, 0 affichent les
  couleurs sans erreur, le pilote `usb` restant attaché.
- **Adresse de l'auteur** : l'historique garde `contact@sebastien-wald.fr` (option A).
- **Hébergement** : GitHub, compte `swald54000` : `klc-light` (application) et
  `omarchy-klc-light` (plugin, manifeste à la racine). Paquet AUR `klc-light`. Les trois
  noms sont libres (vérifié le 2026-11-04).
- Les fichiers déchiffrés de SteelSeries GG ne sont ni versionnés ni redistribués ; ils
  restent cités comme source.

### Critères de réussite

1. Sur une machine Omarchy neuve : `yay -S klc-light`, `sudo usermod -aG klc-light $USER`,
   nouvelle session, et `klc-light apply` éclaire le clavier sans sudo ni configuration
   manuelle.
2. `omarchy plugin add https://github.com/swald54000/omarchy-klc-light.git` installe un
   widget qui passe `omarchy plugin validate`.
3. Le dépôt public se compile et passe ses tests sans aucun fichier hors du dépôt.
4. Sur la machine de l'utilisateur, l'ancien mécanisme (`msi-kbd-usb`, règle sudoers,
   ancienne règle udev) peut être retiré sans perte.
5. Rien n'est créé ni poussé sans l'accord explicite de l'utilisateur à ce moment-là.

### Hors périmètre

Autres modèles de clavier (le GS76 est le seul testé ; d'autres `1038:113a` « peuvent
fonctionner »), traduction de la ligne de commande, CI GitHub, paquets pour d'autres
distributions.

## 2. Accès au clavier

### `SysfsBinder` (remplace `SudoBinder`)

- Trouve le contrôleur : le dossier de `/sys/bus/usb/devices` dont `idVendor` vaut `1038`
  et `idProduct` `113a` (racine configurable pour les tests).
- `bind` : écrit `0` puis `1` dans `authorized`. Le noyau reconfigure le périphérique et
  `hidraw` réapparaît ; `Device::waitAndOpen` attend déjà ce nœud.
- `unbind` : écrit `0`. C'est le nouvel état de repos.
- Messages d'erreur :
  - contrôleur introuvable : le message actuel avec `1038:113a` (code 2, consigne
    d'extinction complète, inchangé) ;
  - écriture refusée : « Add your user to the klc-light group (sudo usermod -aG klc-light
    $USER), then log in again » ;
  - pilote `usb` détaché (état laissé par l'ancien `msi-kbd-usb unbind`) : `authorized`
    passe à 1 mais aucune configuration ne suit ; le message demande une extinction
    complète, qui rattache le pilote au démarrage.

### Fichiers installés par le paquet

- `/usr/lib/sysusers.d/klc-light.conf` : `g klc-light - -`.
- `/usr/lib/udev/rules.d/70-klc-light.rules` :
  - `hidraw` du contrôleur : `TAG+="uaccess"` (comme aujourd'hui) ;
  - périphérique USB `1038:113a` à l'ajout : `RUN` qui donne `authorized` au groupe
    `klc-light`, mode `0664`.

## 3. Dépôt prêt pour le public

- README réécrit pour un utilisateur : matériel testé, installation (AUR, groupe, session),
  usage (éditeur, ligne de commande, hooks Omarchy, widget), limites connues (touche Power
  jumelle de F1, ghosting de la matrice, un envoi à la fois), sources (msi-perkeyrgb,
  Keyglow, mesures GE76 de msi-laptop-ec-protocols, SteelSeries GG cité sans être
  redistribué), licence MIT.
- `tools/gen_layout.py` : sans le fichier GG, reprend les positions de
  `src/layoutdata.cpp` existant au lieu d'échouer.
- `tools/gen_fixtures.py` : documenté comme outil de développement optionnel
  (msi-perkeyrgb requis).
- `SudoBinder` et la mention de `~/.local/share/msi-perkeyrgb-setup` disparaissent du code.
- Version 0.5.0 (`src/version.h`, `PKGBUILD`), plugin 1.1.0.

## 4. GitHub

- `swald54000/klc-light` : le dépôt actuel, historique conservé, branche par défaut `v1`
  renommée `main` à l'envoi, tag `v0.5.0` et release.
- `swald54000/omarchy-klc-light` : le contenu de `omarchy-plugin/swald.klc-light` à la
  racine, plus un README court et la licence. `tools/publish_plugin.sh <dossier du dépôt
  du plugin>` y recopie le dossier (sans `git` ni lien) pour chaque version.
- Le widget affiche `https://github.com/swald54000/klc-light` quand KLC Light manque.

## 5. AUR

- `aur/PKGBUILD` (dans le dépôt) : `source` = l'archive du tag GitHub, `sha256sums`
  réel, `depends` inchangés plus `systemd` (sysusers, udev), installation des fichiers du
  § 2, `install=klc-light.install` qui rappelle la commande `usermod`.
- Vérifié chez l'utilisateur : `makepkg -si` dans un dossier propre, `namcap` sur le
  PKGBUILD et le paquet.
- Envoi par l'utilisateur (son compte AUR, sa clé SSH) : commandes fournies.

## 6. Marketplace et migration locale

- Texte de soumission du plugin pour `omacom/omarchy-plugin-marketplace` (nom, dépôt,
  description, catégorie, capture d'écran à fournir par l'utilisateur).
- Sur la machine de l'utilisateur, une fois le paquet AUR (ou local) 0.5.0 installé et
  vérifié : retirer `/usr/local/bin/msi-kbd-usb`, `/etc/sudoers.d/` correspondant et
  `/etc/udev/rules.d/70-msi-steelseries-klc.rules` ; `~/.local/share/msi-perkeyrgb-setup`
  reste (outils personnels).

## 7. Tests

- `SysfsBinder` sur un faux `/sys` (dossier temporaire) : périphérique trouvé, `bind`
  écrit `0` puis `1`, `unbind` écrit `0`, erreurs (introuvable → texte avec `1038:113a`,
  écriture refusée → texte du groupe).
- Les tests existants restent verts ; aucun ne dépend plus de `SudoBinder`.
- Vérification réelle avec l'utilisateur : groupe créé, règle chargée, `klc-light apply`
  sans sudo, deux cycles, puis redémarrage et hook de démarrage.
