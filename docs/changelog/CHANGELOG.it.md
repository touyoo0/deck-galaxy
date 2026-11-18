# Note di versione (italiano)

Le ultime tre versioni di `CHANGELOG.md`, tradotte: la schermata iniziale le
mostra in questa lingua. I titoli (`## versione - data`, `### Added`)
restano come in `CHANGELOG.md`: sono loro a collegare una versione alla sua
traduzione. Una versione che manca in questo file viene mostrata in inglese.

## 0.10.0 - 2026-11-18

### Added

- Fork per Steam Deck: l'installer, la schermata iniziale, le impostazioni
  (le loro pagine, le opzioni e i testi di aiuto), la finestra di uscita e i
  messaggi di aggiornamento sono in inglese, francese, tedesco, spagnolo e
  italiano. La lingua è quella di Steam (altrimenti quella del sistema)
  finché non ne scegli una: con la lista in alto a destra nell'installer, o
  nel gioco, in Impostazioni > Interfaccia > Lingua del menu. Anche le note
  delle ultime tre versioni sono tradotte (`docs/changelog/`); una versione
  senza traduzione viene mostrata in inglese.

## 0.9.0 - 2026-11-18

### Changed

- Fork per Steam Deck: le note di versione della schermata iniziale mostrano
  ogni data come «18 nov. 2026».

### Added

- Fork per Steam Deck: la schermata iniziale ha un pulsante Opzioni, sopra
  Esci, che apre le impostazioni sopra le note di versione, senza la barra in
  alto del menu.

### Removed

- Fork per Steam Deck: le opzioni che non hanno senso sul Deck spariscono dal
  menu e da settings.ini: la modalità finestra, la sincronizzazione verticale,
  le risoluzioni interne oltre 2x, il filtro del vicino più prossimo, i bordi,
  la scala intera, Disattiva l'audio e Disattiva l'audio in secondo piano,
  l'interruttore Tastiera, la scelta del controller e le assegnazioni della
  tastiera, le tre opzioni della camera con il mouse, Pulsanti nel testo del
  gioco (sempre attivo), il renderer nativo, la pagina Texture, Nascondi il
  cursore inattivo del mouse (sempre attivo) e la posizione del contatore dei
  fotogrammi (in alto a destra).

## 0.8.0 - 2026-11-18

### Changed

- Fork per Steam Deck: l'immagine è sempre 16:10, lo schermo del Deck. Le
  opzioni Rapporto d'aspetto e Widescreen spariscono: il gioco viene
  disegnato in formato largo fin dall'inizio, e i menu e le schermate prima
  del gioco (apertura, titolo, caricamento), disegnati per il 4:3, vengono
  allungati a 16:10 invece di essere mostrati con le barre. Anche una nuova
  finestra si apre a 16:10.
