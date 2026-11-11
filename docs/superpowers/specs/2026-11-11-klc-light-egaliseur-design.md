# klc-light : égaliseur musical (spectre)

Date : 2026-11-11
Statut : conception validée en discussion, en attente de relecture
Branche : `docs/attached-display-measurements` (spec), branche de travail à créer au plan
Mesures de référence : `docs/origins.md`, section du 2026-11-11

## 1. Objectif

### Ce que l'utilisateur a demandé

Ajouter à KLC Light une fonction d'égaliseur musical : le clavier réagit au son que joue
l'ordinateur.

### Décisions prises en discussion

- **Deux modes à terme** : un spectre en barres et une ambiance qui pulse. Cette spec ne
  couvre que le **spectre** ; l'ambiance est un sous-projet ultérieur.
- **À la demande** : l'utilisateur allume et éteint l'égaliseur lui-même, par la ligne de
  commande, l'éditeur et le widget de barre. Rien ne se lance tout seul.
- **Pendant que l'égaliseur tourne**, un `apply` ou un changement de thème ne l'arrête
  pas : le nouveau profil est retenu comme profil appliqué, recolore le spectre, et
  s'affiche à l'arrêt de l'égaliseur.
- **Couleurs** : celles du profil appliqué, touche par touche.
- **Architecture** : tout dans le binaire `klc-light`, lancé comme service utilisateur
  systemd ; audio capté par PipeWire ; pas de dépendance à `cava`.

### Critères de réussite

1. `klc-light visualizer on` fait danser le clavier sur la musique en moins de dix
   secondes ; `klc-light visualizer off` rend le profil appliqué.
2. Les graves montent à gauche, les aigus à droite ; un silence éteint le clavier.
3. Un changement de thème pendant le flux recolore le spectre sans erreur dans le journal.
4. Un plantage du service rend le profil et laisse le contrôleur détaché.
5. Une heure de flux et un cycle veille-réveil ne produisent aucune erreur USB.

## 2. Contexte matériel

Mesuré sur le GS76 le 2026-11-11 (`docs/origins.md`) :

- Le contrôleur affiche un envoi dès qu'il le reçoit, attaché ou non.
- Mode direct de GG (« host-stream ») : touches en masque 2 par les paquets de région
  `0E`, puis `0D 00 02` une fois, puis des images `0C 00 <n> 00` + n × `[hid R V B]` en
  rapports feature de 524 octets.
- Une image de 113 touches en un paquet n'est pas affichée ; découpée en paquets de 57 et
  56 touches espacés de 10 ms, elle l'est. GG plafonne à 110 ; seule la taille 57 est
  validée.
- `0D 00 02` après une image l'efface : il ne s'envoie qu'à l'entrée.
- 300 images à une toutes les 33 ms : fluide, 1,3 ms par paquet.

Non mesuré, donc traité avec prudence dans cette conception : un flux de plus de 10 s, et
la veille avec le contrôleur attaché.

## 3. Architecture

Le service est le binaire `klc-light` lancé par systemd en `klc-light visualizer run`.

| Unité | Rôle | Dépend de |
|---|---|---|
| `AudioSource` (interface) | Fournit des blocs d'échantillons mono | rien |
| `PipewireSource` | `AudioSource` sur le moniteur de la sortie audio par défaut | libpipewire |
| `Spectrum` | Échantillons → niveaux 0..1 par bande | rien |
| `SpectrumRenderer` | Niveaux + profil appliqué → une couleur par touche | `Layout`, `Preview` |
| `StreamSession` | Attache, entre en mode direct, envoie les images, rend le clavier | `Transport`, `Binder` |
| `VisualizerService` | La boucle, le socket, les signaux, la veille | les précédentes |

Une boucle à 30 images par seconde : `Spectrum` → `SpectrumRenderer` → `StreamSession`.

### 3.1 Spectrum

- FFT sur une fenêtre glissante (fenêtre de Hann), bandes réparties sur une échelle
  logarithmique d'environ 40 Hz à 16 kHz, une bande par colonne du clavier.
- Lissage par bande : montée immédiate, descente progressive.
- Normalisation glissante sur quelques secondes, pour que le spectre remplisse la hauteur
  du clavier à bas comme à fort volume ; un plancher évite d'amplifier le bruit de fond
  pendant un silence.
- Aucune dépendance : entrée et sortie sont des tableaux de nombres.

### 3.2 SpectrumRenderer

- Chaque touche visible reçoit une colonne d'après son centre horizontal (`Key::x`) et
  une rangée parmi six d'après son centre vertical (`Key::y`). Le nombre de colonnes est
  fixé par la disposition du GS76 ; les touches hautes ou larges (Entrée, espace, pavé
  numérique) appartiennent à la colonne et à la rangée de leur centre.
- Une touche est allumée, avec sa couleur de profil, quand le niveau de sa colonne
  dépasse sa rangée. La rangée du sommet de la barre est atténuée en proportion du reste,
  ce qui donne plus de six niveaux apparents. Au-dessus : noir.
- La couleur de profil est `Preview::colorAt(touche, 0)` : un profil animé donne la
  couleur de départ de chaque groupe, sans rejouer l'animation. La luminosité du profil
  s'applique.
- La touche power suit F1, comme partout ailleurs (`Layout::lightSource`).

### 3.3 Paquets du mode direct (`protocol.cpp`)

Deux fonctions pures, à côté de `buildPackets` :

- les paquets d'entrée : quatre paquets de région, chaque touche en masque 2 et noire,
  puis le rapport de sortie `0D 00 02` ;
- les paquets d'une image : rapports feature `0C 00 <n> 00` de 524 octets, 57 touches au
  plus par paquet, octets inutilisés à `FF`, chaque touche présente une seule fois.

### 3.4 StreamSession

**Démarrage.** Prend `klc-light.lock`, respecte `MinIntervalMs` depuis le dernier envoi,
attache, ouvre, envoie les paquets d'entrée espacés de 30 ms.

**Flux.** Envoie une image seulement si elle diffère de la précédente ; 10 ms entre les
paquets d'une même image.

**Arrêt.** Envoie le profil appliqué (les paquets de `buildPackets`) sur la connexion déjà
ouverte, ferme, détache, rend le verrou. Un seul cycle d'attache pour toute la séance.

**Erreur.** Au premier paquet refusé : arrêt du flux, détachement, journalisation, sortie
en erreur. Aucun nouvel essai.

Le verrou est tenu pendant toute la séance : aucun autre `klc-light` ne peut envoyer.

## 4. Cohabitation avec le reste de KLC Light

### 4.1 `apply`, `off`, `restore` et les hooks

Avant de chercher le verrou, la commande regarde si le socket du service
(`$XDG_RUNTIME_DIR/klc-light-visualizer.sock`) répond.

- S'il répond : la commande met `state.json` à jour comme aujourd'hui (profil appliqué,
  ou éteint), prévient le service, et sort avec le code 0 sans rien envoyer. Le service
  recharge le profil ; le spectre est recoloré à l'image suivante. Si l'état est
  « éteint », le spectre continue avec les couleurs du dernier profil, et c'est
  l'extinction qui s'applique à l'arrêt.
- S'il ne répond pas : comportement actuel, inchangé.

### 4.2 Veille

Le service écoute `PrepareForSleep` de logind. Avant la veille : arrêt du flux, profil
renvoyé, détachement. Au réveil : attente de l'intervalle, nouvelle attache, reprise.

### 4.3 Plantage

L'unité déclare `ExecStopPost=/usr/bin/klc-light restore` et `Restart=no`. Si le processus
meurt sans nettoyer, systemd rend le profil, et `restore` détache en fin d'envoi. Pas de
redémarrage automatique, pour ne pas multiplier les cycles d'attache.

### 4.4 Démarrage de la machine

L'unité n'est jamais activée. L'égaliseur est éteint après un redémarrage ; le hook
`post-boot` fait `klc-light restore` comme aujourd'hui.

## 5. Interfaces

### 5.1 Ligne de commande

- `klc-light visualizer on [--json]` : démarre l'unité et attend que le service annonce le
  début du flux, ou rapporte son erreur avec les codes d'`apply` (2 : contrôleur absent du
  bus).
- `klc-light visualizer off [--json]` : arrête l'unité et attend le retour du profil.
- `klc-light visualizer run` : le processus du service.
- `klc-light list --json` gagne le champ `visualizer` (booléen), tiré de l'état réel du
  service.
- `KLC_LIGHT_FAKE_DEVICE=1 klc-light visualizer run` n'ouvre pas le clavier et imprime les
  images.

### 5.2 Éditeur

Un bouton à bascule « Égaliseur » dans `ProfileBar.qml`. Tant qu'il est actif, l'aperçu
montre le spectre en direct, reçu du service par le socket ; Apply reste utilisable et
suit la règle du 4.1.

### 5.3 Widget de barre

Un bouton « Égaliseur » dans `Panel.qml`, qui appelle `visualizer on` ou `off` selon le
champ `visualizer` ; l'icône a une variante quand l'égaliseur tourne.

### 5.4 Traductions, réglages, paquet

- Libellés et messages par le `Translator` existant, anglais et français.
- Aucun réglage utilisateur dans cette version.
- `PKGBUILD` : `libpipewire` dans `depends` ; `klc-light-visualizer.service` installé dans
  `/usr/lib/systemd/user/`.

## 6. Erreurs

| Situation | Comportement |
|---|---|
| Contrôleur absent du bus (-71) | `visualizer on` sort avec le code 2 et le message d'`apply` |
| Attache ou ouverture impossible | Détachement, sortie en erreur, message d'`apply` |
| Paquet refusé pendant le flux | Arrêt, détachement, journal, sortie en erreur |
| PipeWire indisponible ou sans sortie | `visualizer on` échoue avant d'attacher le clavier |
| La sortie audio par défaut change | Le service suit la nouvelle sortie |
| Égaliseur déjà actif, ou déjà arrêté | `on` et `off` réussissent sans rien faire |
| Service tué | `ExecStopPost` rend le profil |

## 7. Tests

Automatiques, dans `tests/klc-light_tests.cpp`, sans carte son ni clavier :

- `Spectrum` : un sinus à 100 Hz n'allume que les bandes graves, un sinus à 5 kHz une
  bande aiguë ; le silence donne zéro ; la descente est progressive ; le niveau reste
  utile à faible amplitude.
- `SpectrumRenderer` : niveau 0, tout noir ; niveau 1, chaque touche à sa couleur de
  profil ; niveau intermédiaire, bas allumé et sommet atténué ; chaque touche visible a une
  colonne et une rangée.
- Paquets : entrée en mode direct et images conformes au 3.3.
- `StreamSession`, avec les faux `Transport` et `Binder` existants : ordre des opérations,
  arrêt au premier refus, aucune image identique renvoyée, verrou tenu puis rendu.
- `apply` pendant le flux, avec un faux socket : état enregistré, rien d'envoyé, code 0.

À la main sur le GS76, consigné dans `docs/origins.md` avant publication :

1. un flux d'une heure avec de la musique ;
2. un cycle veille-réveil avec le service actif ;
3. un changement de thème pendant le flux ;
4. un `kill -9` du service.

## 8. Hors périmètre

Le mode ambiance, tout réglage utilisateur, le choix de la source audio, les claviers
autres que le GS76, le contrôleur attaché en permanence pour les envois normaux, et le
remplacement de `09` par `0D 00 02` en fin d'envoi normal.

## 9. Ordre de construction

1. Paquets du mode direct.
2. `Spectrum` et `SpectrumRenderer`, avec une source synthétique.
3. `StreamSession` et la boucle, en mode simulé.
4. `PipewireSource`.
5. Sous-commandes, socket, règle du 4.1, unité systemd.
6. Veille et réveil.
7. Bouton de l'éditeur et aperçu en direct.
8. Widget, traductions, `PKGBUILD`, README.
9. Mesures sur le clavier, puis version 0.6.0.

## 10. Risque

L'API de PipeWire sera vérifiée dans sa documentation au moment de l'écrire. Si elle
s'avère trop lourde, le repli est Qt Multimedia derrière la même interface `AudioSource`,
sans toucher aux autres unités.
