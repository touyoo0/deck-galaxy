# Note di versione (italiano)

Le ultime tre versioni di `CHANGELOG.md`, tradotte: la schermata iniziale le
mostra in questa lingua. I titoli (`## versione - data`, `### Added`)
restano come in `CHANGELOG.md`: sono loro a collegare una versione alla sua
traduzione. Una versione che manca in questo file viene mostrata in inglese.

## 0.11.0 - 2026-11-19

### Added

- Continua: la schermata iniziale offre di riprendere da dove il gioco è stato lasciato. Il gioco conserva uno stato suo quando la finestra passa in secondo piano (menu di Steam) e alla chiusura; Gioca riparte sempre dal titolo. Uno stato si carica anche da qualsiasi area del gioco.
- La pagina di avanzamento dell'installer mostra consigli illustrati durante la compilazione, nella lingua della finestra, uno ogni 9 secondi.
- Una transizione di caricamento dopo Gioca: la schermata iniziale svanisce in un fiume di stelle, poi una schermata di caricamento resta finché il gioco non disegna.
- Stati salvati: cinque slot che conservano tutto il gioco in un momento e si caricano subito, nella stessa sessione o dopo aver chiuso il gioco. F2 apre gli slot dove salvare e F3 quelli da cui caricare; ognuno ha un'immagine e l'ora, e uno slot usato chiede conferma prima di essere sovrascritto. La nuova pagina Stati salvati del menu offre lo stesso con il controller. Circa 10 MB per stato. Non durante un filmato.
- Aggiungere il gioco a Steam imposta anche i tasti posteriori del Deck: L4 apre il menu (F1), L5 mette in pausa (F5), R4 salva uno stato (F2) e R5 ne carica uno (F3). Una configurazione già fatta viene lasciata com'è.

### Changed

- Nel menu, la riga che dice di cosa ha bisogno un'opzione (un disco, un riavvio) non copre più la descrizione: va sotto.
- I suggerimenti sui dischi giapponese ed europeo e sull'aggiornamento non nominano più i file `.bat` della versione Windows: dicono di aggiungere il disco nell'installer e di usare Aggiorna nella schermata iniziale.
- Il gioco va in pausa quando la sua finestra non è più in primo piano (menu di Steam, un'altra finestra), per impostazione predefinita: Interfaccia > Pausa in secondo piano, prima disattivata. Un file di impostazioni precedente mantiene la sua scelta.
- La riga di aiuto sotto un'opzione occupa tutta la larghezza della finestra invece di una colonna stretta accanto al nome.
- Il menu non ha più la barra in alto: tutto sta nella finestra al centro, con una nuova pagina Stati salvati, una pagina Azioni (pausa, avanzamento rapido, muto, schermate, schermo intero, statistiche del rendering) e una pagina Aiuto (scorciatoie, pagina del progetto, segnala un problema, Informazioni), tutte raggiungibili con il controller. F1 apre quella finestra, e Y non porta più a una barra.
- La pausa è ora su F5 (prima F2), per far posto ai tasti degli stati salvati.
- La schermata iniziale e le impostazioni sono ridisegnate: pannelli piatti e scuri con bordo sottile, una pillola bianca sotto la voce scelta, cursori sottili, interruttori morbidi e un solo accento ambra, al posto della doppia cornice bianca, delle placche dorate e del cursore a triangolo. La schermata iniziale perde scintille e scie animate e tiene l'immagine, il logo e le note.
- L'aggiornamento della schermata iniziale non è più un pulsante accanto a Gioca, Opzioni ed Esci: sta in cima al pannello «Novità», una scheda con la nuova versione, l'avanzamento e gli errori, una riga discreta quando non c'è nulla da fare, e un puntino dorato accanto al titolo quando un aggiornamento attende. Con il controller, Destra apre il pannello e A avvia l'aggiornamento.

### Removed

- Il profiler (`RG_PROFILE`) e il controllo hardware (`RG_HWWATCH`): funzionavano solo su Windows e non facevano nulla sul Deck.
- Il contatore di fotogrammi del menu (Vista > Fotogrammi, F3, e la sua impostazione): lo Steam Deck ha il suo.

## 0.10.0 - 2026-11-18

### Added

- Installer, schermata iniziale, impostazioni, riquadro di uscita e messaggi di aggiornamento sono in inglese, francese, tedesco, spagnolo e italiano. La lingua è quella di Steam (altrimenti del sistema) finché non ne scegli una: in alto a destra nell'installer, o Impostazioni > Interfaccia > Lingua del menu nel gioco. Anche le note delle ultime tre versioni sono tradotte (`docs/changelog/`).

## 0.9.0 - 2026-11-18

### Changed

- Le note della schermata iniziale mostrano le date come «18 nov. 2026».

### Added

- Un pulsante Opzioni nella schermata iniziale, sopra Esci, apre le impostazioni.

### Removed

- Le opzioni prive di senso sul Deck: modalità finestra, sincronizzazione verticale, risoluzioni interne oltre 2x, filtro del vicino più prossimo, bordi, scala intera, Disattiva audio e Disattiva audio in secondo piano, interruttore Tastiera, scelta del controller, assegnazioni della tastiera, le tre opzioni della camera con il mouse, Pulsanti nel testo del gioco (sempre attivo), renderer nativo, pagina Texture, Nascondi il cursore inattivo (sempre attivo) e posizione del contatore di fotogrammi (in alto a destra).
