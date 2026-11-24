# Notes de travail : performances sur Steam Deck (2026-11-23 / 24)

Journal pour reprendre plus tard. Branche : `perf-bench-deck` (du dépôt `touyoo0/deck-galaxy`, 10 commits, **pas poussée** sur GitHub, pas de PR).
Le mode d'emploi du banc est dans `README.md` de ce dossier ; ici : le contexte, les résultats, ce qui a échoué, la suite.

## 1. Objectif et contraintes

- Problème : chutes de fps sur la Deck. Demande : un test **automatisé, fiable, identique à chaque fois**, puis des optimisations **côté jeu uniquement** (pas de réglages Steam), avec un vrai gain de fps ou de consommation/température.
- Ne jamais toucher au jeu du joueur : `/run/media/deck/SD/Recomp/RogueGalaxy` (ses sauvegardes y sont). Ni à ses states (`~/.local/share/RogueGalaxy/states/`), que le banc copie.
- `CLAUDE.md` du dépôt : commit/PR seulement sur demande ; nouvelle variable d'environnement = ligne dans `docs/DEVELOPMENT.md` ; changement visible = ligne dans `CHANGELOG.md` ("Unreleased") ; changements à PS2Recomp = patches dans `patches/ps2recomp/`.

## 2. Où sont les choses

| Quoi | Où |
|---|---|
| Dépôt cloné (sources, branche de travail) | `/home/deck/claude-deck/deck-galaxy` (clone superficiel `--depth 1`, sous-modules vides sauf `thirdparty/dq8-gfx` qui est dans l'arbre) |
| Arbre de dev compilé (copie de RogueGalaxy v0.12.6) | `/run/media/deck/SD/Recomp/DeckGalaxy` (disc/ = lien vers le disque extrait du joueur, lecture seule) |
| Données du banc | `/run/media/deck/SD/Recomp/RogueBench/` : `states/` (copies des quick saves), `mc0/` (copie de la carte mémoire), `settings.ini`, `bin/` (binaires nommés), `runs/LABEL/` (résultats) |
| Compiler | `cp` les sources du clone vers `DeckGalaxy/src/`, puis `distrobox enter rg-build -- sh tools/bench/deck/build-in-container.sh`. Incrémental : quelques secondes à quelques minutes |
| Patches PS2Recomp | `cmake -S . -B build/game` dans le conteneur applique les nouveaux patches (voir `patches/README.md`) |

Binaires gardés dans `RogueBench/bin/` : `base` (v0.12.6 d'origine + variables du banc), `alloc1` (+ recyclage des paquets de `native2d`), `prod5` (+ thread producteur, état actuel), `sync5` (= `prod5` avec `RG_PRODUCER_THREAD=0`), et des scripts d'essai (`pin_phys`, `pin_heavy`, `only_mesh`, `only_piece`). `unpack1`, `prod1`..`prod4`, `sync2/4` sont des étapes intermédiaires.
**Prérequis de lancement** : aucun jeu ne doit tourner (`run.sh` refuse sinon). Sur la Deck en mode Jeu : `WAYLAND_DISPLAY=gamescope-0`, `XDG_RUNTIME_DIR=/run/user/1000` (le script les met).

## 3. Ce qui existe (commité)

Code du jeu (peu de lignes, toutes derrière des variables d'environnement, rien ne change par défaut sauf le thread producteur) :
- `RG_INPUT_FROM_LOAD=1` : le script d'entrées (`RG_INPUT`, horloge en lectures de pad) part de l'instant exact du chargement d'un save state (`savestate::loadedAt`). `RG_INPUT_QUIT=1` : le jeu se ferme à la fin du script.
- `RG_SHOT_FLIPS=A-B[,C-D]` (avec `DQ8_GFX_SCREENSHOT_DIR`) : captures des frames affichées entre ces numéros de flips depuis le chargement du state.
- `RG_PRODUCER_THREAD` (défaut : activé) : voir §6. Lignes ajoutées dans `docs/DEVELOPMENT.md` et `CHANGELOG.md`.
- `native2d::build` recycle le stockage de ses paquets A+D (comme il le faisait déjà pour les sommets des meshes).

Outils (`tools/bench/deck/`) :
- `run.sh SCENARIO LABEL [N]` : un run reproductible (state + entrées + fixed time scale `RG_TIME_SCALE=fixed`), résumé via `analyze.py`. Options : `BENCH_UNPACED=1` (sans limite 60 fps, `RG_SPEED=0`), `BENCH_HIDDEN=1` (fenêtre cachée ; défaut = visible), `RG_GS_SCALE=3` (profil de stress), `BENCH_SHOTS=N` / `BENCH_SHOT_FLIPS=` (captures, **changent le coût de la scène : pas pour mesurer**), `BENCH_PERF=1` (+ `BENCH_PERF_ARGS="--call-graph dwarf,16384"` : fichier de 650 Mo, à supprimer ensuite), `GAME_BIN=` (autre binaire).
- `ab.sh SCENARIO BIN_A BIN_B [PAIRES]` : **alterne** A,B,A,B… et affiche les écarts par paire. À utiliser pour toute comparaison.
- `compare.py`, `analyze.py` (fps, p50/p95/p99, 1 % bas, frames >20/25/33 ms, charge GS/VIF1/GIF, puissance APU, Tctl, cœurs utilisés par thread), `sample.py` (relevé de la machine), `frames.py` (comparaison de captures), `make-field-state.sh` (recrée l'état « forêt » par touches scriptées).
- `scenarios.conf` : `battle1`, `battle2`, `encounter1` (états de combat du joueur ; `battle1/2` viennent d'une v0.12.2), `field_idle`, `field_walk` (états `field1.rgs` : le groupe dans une forêt de Juraika près d'un téléporteur, sans combat).

## 4. Ce que nous avons appris sur la mesure

- **Reproductibilité** : combats = scène identique d'un run à l'autre (même empreinte). `field_walk` dérive parfois dans une portion (un run sur trois, jusqu'à 11 % de meshes) : le jeu suit encore le timing de l'hôte. `compare.py` dit « alike » si 90 % des intervalles sont à ±2 %.
- **La console dérive** (chaleur/charge) d'environ 1 fps en une heure : le même binaire a donné 57,8 puis 56,6 fps. **Toujours comparer avec `ab.sh`**, jamais deux labels mesurés à des heures différentes. Une différence plus petite que l'écart entre runs = bruit.
- **La logique du jeu dépend du timing** : deux builds de performances différentes divergent (animation, issue d'un combat) en quelques centaines de frames. Donc **pas de comparaison d'images au pixel sur de longues séquences** ; elle n'a de valeur que là où le jeu est dans le même état (`frames.py`, voir §6).
- Un state sauvegardé impose son rythme : `RG_FPS=30` n'a pas d'effet quand on charge un state en 60 fps (le mode 30 fps n'a pas pu être mesuré).
- Naviguer les menus à l'aveugle est lent : les touches de `make-field-state.sh` (menu du titre en français : Continuer → carte mémoire fente 1 → premier fichier → confirmations) ont été trouvées avec des captures. Les fast travel (`RG_FAST_TRAVEL=1`, Select puis d-pad puis Croix) ne mènent qu'aux téléporteurs de la planète courante : **Rosa n'est pas atteignable** dans cette sauvegarde (le joueur n'y est pas encore dans l'histoire).

## 5. Résultats chiffrés (échelle 2, sauf mention)

Référence de départ (limite 60 fps, fenêtre cachée) : `battle1` 59,9 fps ; `battle2` 59,4 ; `field_idle` 57,5 (VIF1 à 89,5 %, 1 % bas 40 fps) ; `field_walk` 59,1. En capacité (sans limite) : `battle2` 80 fps, `field_idle` 57. À l'échelle 3 `field_idle` tombe à 47-51 fps.

Consommation (APU seul, hors écran) : `battle2` 10-11 W, `field_idle` 14,7 W ; le jeu utilise ~2 cœurs (GameThread 0,8, VIF1 0,75-0,85, principal 0,4) ; profil plat, aucun thread ne tourne dans le vide.
Leviers testés (runs isolés, hors code) : GPU 1600→1000 MHz : −1,3 W en combat (0 fps perdu) ; échelle 1 : −2 W, échelle 3 : +2 W ; ordonnanceur `lavd` : +0,4 fps et p95 18,2 contre 19,6 ms en combat ; un seul thread matériel par cœur physique (`taskset -c 0,2,4,6`) : +1,08 fps (3/3 paires) ; épingler les 3 threads lourds : aucun gain. (J'ai changé temporairement GPU/ordonnanceur via steamos-manager, remis à `manual 1600 MHz` / `none`.) Le joueur ne veut **pas** de solution par réglage Steam.

Optimisations dans le code :

| Changement | Résultat (`field_idle`, alterné, 3 paires) | Décision |
|---|---|---|
| Recyclage des paquets A+D de `native2d::build` | +0,85 fps (+1,11, +0,91, +0,54) | gardé |
| `unpackPlain` (PS2Recomp, patch 0043) largeur fixe | −0,54 fps (0 paire sur 3) | abandonné (le coût est la mémoire, pas `memcpy`) |
| Cache des chunks VU1 | non fait : le hachage coûte autant que la conversion (0,9 µs/chunk), voir README | écarté |
| **Thread producteur** (`mesh`+`piece` sur le thread GIF) | sans limite : 56,1 → 61,1 fps (**+4,9**, 3/3) ; 1 % bas 37,4 → 39,5 ; frames >25 ms 19 → 7 ; avec limite 60 fps : 56,9 → 58,6 (**+1,7**, 3/3) et −1 W / −1,6 °C | gardé, **à valider en jouant** |

`battle2` ne gagne rien avec le thread producteur (limité par le thread du jeu, 79 fps dans les deux cas).

**Gain total actuel (vs. version d'origine), `field_idle`** : +4,9 fps (+9 %) en capacité, +1,7 fps sous la limite 60. Pas un doublement.

## 6. Thread producteur : comment ça marche et risques

- `nativeproducer::mesh` / `piece` (un tiers du temps de VIF1) sont envoyés au `GifStage` (thread « GIF », jusque-là inactif sous le pipeline natif) : enregistrements `NativeMesh`/`NativePiece` (état de dessin copié, 64 quads de la mémoire VU1 pour les matrices, en-tête + sommets), exécutés dans l'ordre. Si le producteur refuse : repli identique à l'ancien (rien si les paquets sont absorbés, sinon `submitNativeMesh`).
- Tout autre appel au producteur fait par le thread VIF1 attend d'abord le thread GIF (`producerBarrier()`) : `replaceUpload`, `hostUpload`, `noteLocalCopy`, `noteEffect`/`effect`, et **`takeProduced` / `noteWithheld` (appelés via l'alias `producer::`, que mon premier inventaire avait ratés → images avec éléments manquants)**.
- Le thread du jeu attend la fin des listes+GIF à chaque flip (`afterFlip` : `rg::vif1::drainForSnapshot()`) avant d'enregistrer la présentation (pour le jeu, une liste est « finie » quand VIF1 l'a passée au thread GIF, pas quand les dessins sont faits).
- `RG_PRODUCER_THREAD=0` rétablit l'ancien chemin ; `mesh` ou `piece` ne déplace qu'un des deux (diagnostic d'ordre).
- Validation faite : 5 scénarios en fenêtre cachée et visible sans plantage ni blocage ; images **identiques au pixel** là où le jeu est dans le même état (`field_idle` flips 300/900/1500 ; `battle2` 300-303) ; les différences restantes sont des phases d'animation/issue de combat (divergence du jeu). **Non prouvé** : une longue partie variée (UI, cinématiques, chargements, sauvegardes/chargements de state, changement de zone, mode fenêtre/plein écran, pause). Aucun test des chargements de save state pendant que des enregistrements sont en vol (le chargement appelle `drainForSnapshot`, a priori sûr).

## 7. Pistes pour la suite (par ordre de rentabilité estimée)

1. **Déplacer aussi `native2d::build`** (16 % du temps de VIF1) vers le thread GIF : +2 à +3 fps espérés dans `field_idle`. Même méthode (enregistrements ordonnés, barrières, vérification par `frames.py`). Attention : l'état du convertisseur est `thread_local` par thread émetteur (VIF1 et thread du jeu se relaient) et `sendPacket` mélange `drawstate::observe` (côté VIF1) et conversions ; à étudier avant.
2. **Thread du jeu (GameThread, maintenant 43 % des échantillons)** : code EE recompilé, profil plat (plus grosse fonction ≈ 0,5 %). Candidats mineurs vus : `EeScheduler::publishSnapshotNow` (~1,3 % du thread, tri des sémaphores à chaque publication : à protéger derrière le panneau de debug), `__vdso_clock_gettime`, `pthread_mutex_lock`. Gain probable faible ; un gain réel demanderait de changer l'architecture (ex. logique à 30 Hz avec rendu 60 Hz interpolé : gros chantier).
3. Recycler les tampons de `nativeproducer::mesh` (MeshDraw : vecteurs `indices`/`vertices` créés à chaque mesh, ~3,4 % des échantillons VIF1 en allocations) : ~+0,4 fps, faible risque. Et mémoïser les matrices (`multiply`/`invert` par mesh quand modèle/vue/projection ne changent pas).
4. Valider le thread producteur en jeu réel (le joueur) : visiter ville, combat, boss, menus, cinématiques, sauvegarde/chargement de states ; surveiller artefacts (éléments 2D manquants ou mal ordonnés) ; en cas de doute `RG_PRODUCER_THREAD=0`.
5. Nouveaux scénarios : une ville avec PNJ (Rosa n'est pas disponible ; le joueur peut faire une quick save F2 ailleurs puis la copier dans `RogueBench/states/` et ajouter une ligne à `scenarios.conf`), un boss, la même scène à l'échelle 3.
6. Mesurer le mode 30 fps du jeu (il faut un state pris en mode 30 fps) pour chiffrer la consommation.
7. Si le but reste la consommation : le code n'a plus de gros levier (profil plat, ~2 cœurs de travail réel). Les gains viennent de réduire le travail par image (30 fps, échelle plus basse) ou de réglages système (exclus par le joueur).

## 8. Procédure pour reprendre

```sh
cd /home/deck/claude-deck/deck-galaxy && git checkout perf-bench-deck   # (ou recloner touyoo0/deck-galaxy puis récupérer la branche : elle n'est pas poussée)
# compiler une modification
cp src/MODIFIE.cpp /run/media/deck/SD/Recomp/DeckGalaxy/src/   # + recréer /tmp/build.sh si besoin
distrobox enter rg-build -- sh /tmp/build.sh
cp /run/media/deck/SD/Recomp/DeckGalaxy/build/game/src/roguegalaxy /run/media/deck/SD/Recomp/RogueBench/bin/NOUVEAU
# comparer à la version actuelle, par paires alternées
BENCH_UNPACED=1 BENCH_HIDDEN=1 tools/bench/deck/ab.sh field_idle prod5 NOUVEAU 3
# profiler
BENCH_PERF=1 BENCH_UNPACED=1 BENCH_HIDDEN=1 tools/bench/deck/run.sh field_idle prof 1; perf report -i /run/media/deck/SD/Recomp/RogueBench/runs/prof/field_idle.1.perf --no-children --sort comm,sym
```

Important : le dépôt est un clone ; **la branche `perf-bench-deck` n'existe que localement** (`/home/deck/claude-deck/deck-galaxy`). Si ce dossier est perdu, rien n'a été poussé. Le plus simple pour la sauver : `git push -u origin perf-bench-deck` (à faire seulement sur demande du joueur ; `gh` est connecté en `touyoo0`).
