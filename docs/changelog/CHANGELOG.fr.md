# Notes de version (français)

Les trois dernières versions de `CHANGELOG.md`, traduites : l'écran d'accueil
les affiche dans cette langue. Les titres (`## version - date`, `### Added`)
restent comme dans `CHANGELOG.md` : c'est eux qui relient une version à sa
traduction. Une version absente de ce fichier s'affiche en anglais.

## 0.11.0 - 2026-11-19

### Added

- Continuer : l'écran d'accueil propose de reprendre là où le jeu a été laissé. Le jeu garde un état à lui quand sa fenêtre passe à l'arrière-plan (menu Steam) et à la fermeture ; Jouer repart toujours du titre. Un état se charge aussi depuis n'importe quelle zone du jeu.
- La page de progression de l'installeur affiche des astuces illustrées pendant la compilation, dans la langue de la fenêtre, une toutes les 9 secondes.
- Une transition de chargement après Jouer : l'écran d'accueil s'efface dans un flot d'étoiles, puis un écran de chargement reste jusqu'à ce que le jeu s'affiche.
- États de sauvegarde : cinq emplacements qui gardent tout le jeu à un instant, chargés d'un coup, dans la même partie ou après avoir fermé le jeu. F2 ouvre les emplacements où sauvegarder, F3 ceux d'où charger ; chacun a une image et l'heure, et un emplacement utilisé demande confirmation avant d'être écrasé. La page États de sauvegarde du menu donne la même chose à la manette. Environ 10 Mo par état. Pas pendant une cinématique.
- Ajouter le jeu à Steam règle aussi les boutons arrière du Deck : L4 ouvre le menu (F1), L5 met en pause (F5), R4 sauvegarde un état (F2) et R5 en charge un (F3). Une configuration déjà faite pour le jeu est laissée telle quelle.

### Changed

- Dans le menu, la ligne qui dit ce dont une option a besoin (un disque à ajouter, un redémarrage) ne recouvre plus la description : elle passe dessous.
- Les indications sur les disques japonais et européen et sur la mise à jour ne citent plus les fichiers `.bat` de la version Windows : elles disent d'ajouter le disque dans l'installeur et d'utiliser Mettre à jour sur l'écran d'accueil.
- Le jeu se met en pause quand sa fenêtre n'est plus au premier plan (menu Steam, autre fenêtre), par défaut : Interface > Pause en arrière-plan, qui était désactivée. Un fichier de réglages existant garde son choix.
- Le texte d'aide sous une option occupe toute la largeur de la fenêtre au lieu d'une colonne étroite à côté du nom.
- Le menu n'a plus de barre en haut : tout est dans la fenêtre au centre, avec une nouvelle page États de sauvegarde, une page Actions (pause, avance rapide, son coupé, captures, plein écran, statistiques du rendu) et une page Aide (raccourcis, page du projet, signaler un problème, À propos), toutes accessibles à la manette. F1 ouvre cette fenêtre, et Y ne mène plus à une barre.
- La pause est sur F5 (elle était sur F2), pour laisser la place aux touches des états de sauvegarde.
- L'écran d'accueil et les réglages sont redessinés : panneaux sombres et plats à bordure fine, pastille blanche sous l'entrée choisie, curseurs fins, interrupteurs doux et un seul accent ambre, à la place du double cadre blanc, des plaques dorées et du curseur triangulaire. L'écran d'accueil perd ses étincelles et traînées animées, et garde l'image, le logo et les notes.
- La mise à jour de l'écran d'accueil n'est plus un bouton parmi Jouer, Options et Quitter : elle est en haut du panneau « Nouveautés », une carte avec la nouvelle version, la progression et les erreurs, une ligne discrète quand il n'y a rien à faire, et un point doré près du titre quand une mise à jour attend. À la manette, Droite ouvre le panneau et A lance la mise à jour.

### Removed

- Le profileur (`RG_PROFILE`) et la surveillance matérielle (`RG_HWWATCH`) : ils ne fonctionnaient que sous Windows et ne faisaient rien sur le Deck.
- Le compteur d'images du menu (Affichage > Compteur d'images, F3, et son réglage) : le Steam Deck a le sien.

## 0.10.0 - 2026-11-18

### Added

- L'installeur, l'écran d'accueil, les réglages, la boîte « Quitter » et les messages de mise à jour sont en anglais, français, allemand, espagnol et italien. La langue est celle de Steam (sinon du système) tant que vous n'en choisissez pas une : en haut à droite de l'installeur, ou Réglages > Interface > Langue du menu dans le jeu. Les notes des trois dernières versions sont traduites aussi (`docs/changelog/`).

## 0.9.0 - 2026-11-18

### Changed

- Les notes de l'écran d'accueil affichent les dates sous la forme « 18 nov. 2026 ».

### Added

- Un bouton Options sur l'écran d'accueil, au-dessus de Quitter, ouvre les réglages.

### Removed

- Les options sans sens sur le Deck : mode de fenêtre, synchronisation verticale, résolutions internes au-delà de 2x, filtre « plus proche voisin », bordures, mise à l'échelle entière, Couper le son et Couper le son en arrière-plan, interrupteur Clavier, choix de la manette, affectations du clavier, les trois options de la caméra à la souris, Boutons dans le texte du jeu (toujours actif), rendu natif, page Textures, Masquer le curseur inactif (toujours actif) et position du compteur d'images (en haut à droite).
