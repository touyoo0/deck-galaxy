# Notes de version (français)

Les trois dernières versions de `CHANGELOG.md`, traduites : l'écran d'accueil
les affiche dans cette langue. Les titres (`## version - date`) restent comme
dans `CHANGELOG.md` : ce sont eux qui relient une version à sa traduction. Une
version absente de ce fichier s'affiche en anglais.

## 0.12.1 - 2026-11-22

- L'import d'une sauvegarde PCSX2 ne laisse plus une sauvegarde vide (le jeu la disait corrompue) avec certaines cartes mémoire ; refaire l'import depuis l'installeur la répare.
- Avec un disque européen, le texte du jeu démarre dans la langue de la Deck (celle de Steam, sinon du système) au premier lancement, comme l'installeur et le menu ; en anglais si cette langue n'est pas parmi les cinq.
- Les monstres chiens de pierre ont leurs textures (ils étaient gris et transparents).
- Charger un état sauvegardé ne fige ni ne plante plus le jeu, et ne casse plus le menu pause, avec les textes européens.

## 0.12.0 - 2026-11-22

- L'installeur peut importer votre sauvegarde PCSX2 du jeu : il cherche seul votre carte mémoire (PCSX2, EmuDeck, RetroDECK, carte SD), et vous pouvez choisir un fichier ou un dossier à la main s'il n'en trouve pas.
- Les logos au démarrage se passent avec un bouton (A, B, X, Y ou Start), aussi au premier lancement.
- Le « Appuyez sur A » de l'écran de chargement apparaît dès que le jeu est chargé, pas après tout le texte de l'histoire.
- L'écran d'accueil fait de petits sons : un léger tic quand la sélection bouge, un tap quand on appuie sur un bouton, un accord chaleureux quand Jouer lance le jeu.
- Une nouvelle partie commence avec la rotation de la caméra sur « Inversée » ; les sauvegardes existantes gardent leur choix.
- Plus de combinaisons de boutons : L4 (F1) ouvre le menu. Outils de développeur supprimés.
- Rendu natif, plus léger sur le Deck : 60 ips stables, environ 4 W de moins. Démarre en 2x. Les packs de textures ne sont pas dessinés pour l'instant.
- Plus d'option « Plein écran » ni de F11 : le jeu est toujours en plein écran sur le Deck.
- Les chargements rapides gardent la musique, les combats et les textures, même après avoir redémarré le jeu.
- Les chargements rapides faits pendant l'affichage des récompenses de combat ne perdent plus les textures du décor (faites une nouvelle sauvegarde rapide : les anciennes gardent le problème).
- Continuer charge votre dernière sauvegarde rapide, pas l'endroit où vous avez laissé le jeu.
- Écraser une sauvegarde rapide : la question commence sur Écraser, pas sur Annuler.

## 0.11.0 - 2026-11-19

- Continuer : l'écran d'accueil reprend là où vous avez laissé le jeu.
- États de sauvegarde : 5 emplacements, F2 pour sauvegarder, F3 pour charger (voir `docs/SAVESTATES.md`).
- Boutons arrière réglés à l'ajout à Steam : L4 menu, L5 pause, R4 sauvegarde, R5 chargement.
- Astuces illustrées dans l'installeur pendant la compilation.
- Transition de chargement après Jouer.
- Écran d'accueil et réglages redessinés ; la mise à jour est dans « Nouveautés ».
- Le menu est une seule fenêtre centrée, avec les pages États de sauvegarde, Actions et Aide.
- La pause est sur F5 (elle était sur F2).
- Le jeu se met en pause quand sa fenêtre perd le focus (nouveau défaut).
