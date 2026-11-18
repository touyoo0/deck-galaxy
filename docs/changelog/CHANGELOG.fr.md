# Notes de version (français)

Les trois dernières versions de `CHANGELOG.md`, traduites : l'écran d'accueil
les affiche dans cette langue. Les titres (`## version - date`, `### Added`)
restent comme dans `CHANGELOG.md` : c'est eux qui relient une version à sa
traduction. Une version absente de ce fichier s'affiche en anglais.

## Unreleased

### Added

- Fork Steam Deck : l'écran d'accueil, les réglages (leurs pages, options et
  textes d'aide), la boîte « Quitter » et les messages de mise à jour sont en
  anglais, français, allemand, espagnol et italien. La langue est celle de
  Steam (sinon celle du système) tant que vous n'en choisissez pas une dans
  Réglages > Interface > Langue du menu. Les notes de version des trois
  dernières versions sont traduites aussi (`docs/changelog/`) ; une version
  sans traduction s'affiche en anglais.

## 0.9.0 - 2026-11-18

### Changed

- Fork Steam Deck : les notes de version de l'écran d'accueil affichent
  chaque date sous la forme « 18 nov. 2026 ».

### Added

- Fork Steam Deck : l'écran d'accueil a un bouton Options, au-dessus de
  Quitter, qui ouvre les réglages par-dessus les notes de version, sans la
  barre du haut du menu.

### Removed

- Fork Steam Deck : les options qui n'ont aucun sens sur le Deck disparaissent
  du menu et de settings.ini : le mode de fenêtre, la synchronisation
  verticale, les résolutions internes au-delà de 2x, le filtre « plus proche
  voisin », les bordures, la mise à l'échelle entière, Couper le son et Couper
  le son en arrière-plan, l'interrupteur Clavier, le choix de la manette et
  les affectations du clavier, les trois options de la caméra à la souris,
  Boutons dans le texte du jeu (toujours actif), le rendu natif, la page
  Textures, Masquer le curseur inactif de la souris (toujours actif) et la
  position du compteur d'images (en haut à droite).

## 0.8.0 - 2026-11-18

### Changed

- Fork Steam Deck : l'image est toujours en 16:10, l'écran du Deck. Les
  options Format d'image et Écran large disparaissent : le jeu est dessiné en
  large dès le départ, et les menus et les écrans d'avant le jeu (ouverture,
  titre, chargement), dessinés pour le 4:3, sont étirés en 16:10 au lieu
  d'être montrés avec des bandes. Une nouvelle fenêtre s'ouvre aussi en 16:10.
