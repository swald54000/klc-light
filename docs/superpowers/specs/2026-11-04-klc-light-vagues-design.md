# klc-light : directions des vagues

Date : 2026-11-04
Statut : conception validée en discussion, en attente de relecture
Branche : `profils-groupes`
Spec précédente : `2026-11-02-klc-light-profils-groupes-design.md`
Mesures : `docs/origins.md` (13 envois sur le GS76 et encodeur de GG déchiffré)

## 1. Objectif

### Ce que l'utilisateur a demandé

Remplacer les six origines figées `aqua-0` … `aqua-5` par de vraies directions de vague,
comme dans SteelSeries GG, et pouvoir faire partir ou arriver une vague sur une touche
choisie (option « B » en discussion).

### Ce que le contrôleur permet (mesuré, voir `docs/origins.md`)

- Une vague est un décalage de phase de chaque touche, proportionnel à sa distance à un
  point (0x8C, 0x8E) divisée par une vitesse de propagation (0x94, `scale` de GG).
- 0x90 et 0x92 pondèrent la distance en x et en y : horizontale (1, 0), verticale (0, 1),
  circulaire (1, 1) ; (0, 0) donne une pulsation synchronisée.
- 0x9A = 0 : la vague va vers le point ; 1 : elle s'en éloigne.
- Le point doit rester sur le clavier (x 0 … 4294, y 0 … 1037) : au-delà, le contrôleur
  calcule faux.

### Décisions prises en discussion

- Directions prêtes à l'emploi **et** origine sur une touche choisie au clic.
- Stockage par **origine nommée** (`left`, `KEY_G`…), pas par coordonnées.
- S'applique au fond animé comme à chaque groupe en vague ; la pulsation reste sans origine.

### Critères de réussite

1. L'éditeur propose huit directions et un curseur de taille ; l'ancienne liste `aqua-N`
   disparaît.
2. « Vers une touche » / « Depuis une touche » : un bouton fait choisir la touche d'un
   clic sur le clavier dessiné, Échap annule ; la touche d'origine porte un anneau.
3. L'aperçu montre des vagues horizontales, verticales et circulaires dans le même sens
   que le clavier.
4. Les profils et thèmes existants s'ouvrent sans erreur, convertis (§3).
5. Sur le clavier, chaque direction part et arrive où l'éditeur l'annonce (vérifié avec
   l'utilisateur à la fin, un envoi par direction).
6. Tous les tests existants restent verts.

### Hors périmètre

Plus de six effets animés (GG en déclare jusqu'à 19), la direction « ligne » de GG (type
3, réservée au réactif), l'affichage dans l'aperçu des étapes allongées à 33 unités, les
tests sur le vrai clavier pendant le développement.

## 2. Modèle de données

Une structure commune remplace le champ `origin` de `BaseLayer`, `EffectLayer` et `Group` :

```cpp
struct Wave {
    QString origin = "left"; // left, right, top, bottom, center, or a key id (KEY_G)
    QString flow = "toward"; // toward: the wave runs to the origin; away: from it
    int size = 68;           // 1..100
};
```

`EffectDef::origin` devient `EffectDef::wave` (la pulsation n'en a pas). Valeurs par défaut :
« vers la gauche », taille 68.

### Choix proposés dans l'éditeur

| Libellé | origin | flow |
|---|---|---|
| Vers la gauche | `left` | `toward` |
| Vers la droite | `right` | `toward` |
| Vers le haut | `top` | `toward` |
| Vers le bas | `bottom` | `toward` |
| Vers le centre | `center` | `toward` |
| Depuis le centre | `center` | `away` |
| Vers une touche | `KEY_…` | `toward` |
| Depuis une touche | `KEY_…` | `away` |

Un bord avec `away` est lu comme le bord opposé avec `toward` : c'est la même vague à l'œil
(pour une vague horizontale ou verticale, s'éloigner du bord gauche revient à aller vers
le droit).

## 3. Format des fichiers

Profils de la bibliothèque : version 3. Dans `base` (type `wave`) et chaque groupe `wave` :

```json
"wave": { "origin": "KEY_G", "flow": "away", "size": 40 }
```

Le format v1 (fichiers `keyboard.klc.json` des thèmes) accepte le même objet `wave` dans
`base` et dans chaque effet.

### Conversion à la lecture (v1, v2, et v3 écrit par erreur avec `origin`)

| Ancienne origine | Devient | Exacte |
|---|---|---|
| `aqua-1` | vers la droite | oui (même vague à l'œil) |
| `aqua-3` | vers la gauche | oui |
| `aqua-4` | vers la droite | oui |
| `aqua-0`, `aqua-2`, `aqua-5` | vers le centre | non : origine proche du centre |

Taille 68 pour toutes. Les fichiers ne sont réécrits qu'au prochain enregistrement. Une
origine qui n'est ni un nom connu ni une touche du clavier est une erreur (§7).

## 4. Envoi

Le paquet d'effet part toujours du modèle `aqua-3` validé ; `transitions()` reste tel quel.
Seuls changent :

| Octets | Valeur |
|---|---|
| 0x8C (u16) | x du point |
| 0x8E (u16) | y du point |
| 0x90 (u16) | 1 pour horizontale ou circulaire, sinon 0 |
| 0x92 (u16) | 1 pour verticale ou circulaire, sinon 0 |
| 0x94 (u16) | vitesse = round(31 · (1001 / 31)^((taille − 1) / 99)), soit 31 … 1001 ; 68 → 326 |
| 0x9A | 0 pour `toward`, 1 pour `away` |

Points : `left` (0, 518), `right` (4294, 518), `top` (2147, 0), `bottom` (2147, 1037),
`center` (2147, 518), une touche : son centre sur la photo de `Layout`, mis à l'échelle
x · 4294 / 997, y · 1037 / 487 (la photo mesure 997 × 487 px, y vers le bas). Type :
horizontale pour `left`/`right`, verticale pour `top`/`bottom`, circulaire sinon.

La pulsation garde le paquet `aqua-0` (0x90 = 0x92 = 0), sans changement.

Le calage x/y d'une touche et le sens vertical ne sont pas encore observés : la
vérification finale (§8) les confirme, et la mise à l'échelle est isolée dans une seule
fonction pour être corrigée sans toucher au reste.

## 5. Aperçu

`Preview` remplace son décalage linéaire en x (`WaveMsPerUnit`) par le modèle du
contrôleur : distance d de la touche au point (|dx| horizontale, |dy| verticale,
√(dx² + dy²) circulaire, en unités du contrôleur), décalage = d / vitesse unités de 10 ms.
`toward` : la touche lointaine est en avance de ce décalage ; `away` : en retard.

## 6. Interface

### Panneau d'effet (fond en vague ou groupe en vague)

La liste `aqua-0 … aqua-5` est remplacée par :

- une liste des huit directions (§2) ;
- un curseur « Taille de vague » 1 … 100, une étape d'annulation par geste comme les durées ;
- pour les deux choix « touche » : le nom de la touche d'origine et un bouton
  « Choisir la touche ». Passer d'un choix « touche » à un autre garde la touche ; y venir
  depuis une direction prend la touche du centre du clavier (`KEY_G`) jusqu'au choix.

### Choix de la touche

« Choisir la touche » met l'éditeur en mode choix : le texte d'aide l'annonce, le prochain
clic sur une touche du clavier dessiné fixe l'origine (une étape d'annulation) au lieu de
la sélectionner, puis le mode se termine. Échap ou un second clic sur le bouton annule.
Pendant le mode, les raccourcis de sélection sont sans effet.

### Repère

La touche d'origine de la vague ouverte dans le panneau porte un anneau blanc sur le clavier
dessiné ; aucun anneau pour les directions vers un bord ou le centre.

## 7. Erreurs

- Origine inconnue : « wave origin: unknown key "KEY_X" » (ou nom inconnu), à la lecture et
  à la validation ; le profil est marqué illisible dans la liste, comme aujourd'hui.
- `flow` autre que `toward`/`away`, taille hors 1 … 100 : même traitement.
- Une touche d'origine peut être hors du groupe : c'est voulu (une vague peut venir d'ailleurs).

## 8. Tests

- Octets 0x8C … 0x9A pour chaque nom d'origine, pour une touche, pour `away`, et pour les
  tailles 1, 68 et 100 ; le reste du paquet d'effet inchangé.
- Conversion de chaque `aqua-N` en v1 et en v2 ; aller-retour JSON v3 sans perte ;
  normalisation bord + `away`.
- Erreurs : touche inconnue, `flow` inconnu, taille 0 et 101.
- Aperçu : pour « vers la gauche », une touche de droite est en avance sur une touche de
  gauche ; pour « vers le haut », la barre d'espace sur les touches F ; « depuis une
  touche » : la touche d'origine en avance sur ses voisines.
- Interface : la liste propose huit choix ; le mode « Choisir la touche » fixe l'origine au
  clic, Échap l'annule, l'anneau suit l'origine ; annuler/rétablir couvre ces changements.
- Les comparaisons octet par octet avec les enregistrements d'origine ignorent désormais
  aussi 0x8C … 0x9A des paquets d'effet (les nouveaux points diffèrent de quelques unités
  de ceux d'`aqua`).

Vérification avec l'utilisateur à la fin : un envoi par direction (huit), en notant d'où
part la vague ; s'il le faut, la mise à l'échelle x/y ou le sens vertical sont corrigés puis
revérifiés.

## 9. Livraison

Commits par étape sur `profils-groupes`, `bin/test` vert à chaque commit, README et aide
mis à jour (format v3, directions), version 0.3.0 à la fin.
