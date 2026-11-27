# Note di versione (italiano)

Le ultime tre versioni di `CHANGELOG.md`, tradotte: la schermata iniziale le
mostra in questa lingua. I titoli (`## versione - data`) restano come in
`CHANGELOG.md`: sono loro a collegare una versione alla sua traduzione. Una
versione che manca in questo file viene mostrata in inglese.

## 0.13.0 - 2026-11-26

- Nuova «Modalità Remaster» (attiva per impostazione predefinita): upscaling GPU 2x delle texture (Anime4K CNN per il 3D e 2xBR per 2D/interfaccia), mipmap, filtro anisotropico 16x, SSAO a mezza risoluzione, FXAA e nitidezza adattiva (CAS).
- Importanti ottimizzazioni di prestazioni e consumi su Steam Deck: generazione diretta di contorni e volumi d'ombra VU1, skinning e culling 3D SIMD (SSE/AVX2), batching dei draw call, compilazione Zen 2 + ThinLTO e minore carico a riposo.
- Menu Schermo semplificato: rimosse le opzioni obsolete «Filtro di scala» e «Immagine più nitida».

## 0.12.7 - 2026-11-25

- 60 fps più stabili e consumi e calore ridotti su Steam Deck sia in esplorazione che in battaglia.

## 0.12.6 - 2026-11-23

- Nuova opzione «Salta le animazioni degli incantesimi»: la scena di un incantesimo o di una mossa speciale viene saltata da sola, senza premere alcun tasto.

## 0.12.5 - 2026-11-23

- La pagina dei traguardi mostra il progresso di un traguardo a contatore (ad esempio 3/9 trasportatori) finché è in corso.
- La barra di scorrimento dell'elenco degli incantesimi in combattimento resta nella sua finestra quando l'elenco è corto.

## 0.12.4 - 2026-11-23

- L'opzione del limite di oggetti arriva ora fino a 999 per oggetto invece di 99.
- Nuova opzione «Viaggio rapido dalla mappa»: con la mappa aperta, scegli con la croce direzionale un trasportatore attivato e viaggia fino a lì.

## 0.12.3 - 2026-11-22

- L'avviso di RetroAchievements non compare più a ogni avvio del gioco.
- Gli avvisi di obiettivo sbloccato, camera libera, borsa piena e salvataggio rapido sono tradotti in francese, tedesco, spagnolo e italiano.
- Il bagliore attorno a un personaggio (l'aura della Spada del Fulmine) torna a essere un sottile contorno bianco, non più un'ampia macchia di luce sul pavimento.
- Il nome della zona in basso sullo schermo appare per intero in francese, tedesco, spagnolo e italiano (gli accenti sono disegnati come lettere semplici); veniva tagliato al primo accento o trattino, oppure mancava.

## 0.12.2 - 2026-11-22

- Il salto all'indietro in combattimento non è più molto troppo alto e lungo a 60 fps.

## 0.12.1 - 2026-11-22

- L'importazione di un salvataggio PCSX2 non lascia più un salvataggio vuoto (il gioco lo diceva danneggiato) con alcune memory card; importare di nuovo dall'installer lo ripara.
- Con un disco europeo, il testo del gioco parte nella lingua del Deck (quella di Steam, altrimenti del sistema) al primo avvio, come l'installer e il menu; in inglese se quella lingua non è tra le cinque.
- I mostri cane di pietra hanno le loro texture (erano grigi e trasparenti).
- Caricare uno stato salvato non blocca né chiude più il gioco, né rompe il menu di pausa, con i testi europei.

## 0.12.0 - 2026-11-22

- L'installer può importare il tuo salvataggio PCSX2 del gioco: cerca da solo la tua memory card (PCSX2, EmuDeck, RetroDECK, scheda SD), e puoi scegliere un file o una cartella a mano se non ne trova.
- I loghi all'avvio si saltano con un pulsante (A, B, X, Y o Start), anche al primo avvio.
- Il «Premi A» della schermata di caricamento compare appena il gioco è caricato, non dopo tutto il testo della storia.
- La schermata iniziale fa suoni lievi: un leggero tic quando si muove la selezione, un tocco quando si preme un pulsante e un accordo caldo quando Gioca avvia il gioco.
- Una nuova partita inizia con la rotazione della camera su «Invertita»; i salvataggi esistenti mantengono la loro scelta.
- Niente più combinazioni di pulsanti: L4 (F1) apre il menu. Strumenti per sviluppatori rimossi.
- Renderer nativo, più leggero sul Deck: 60 fps stabili, circa 4 W in meno. Parte a 2x. I pacchetti di texture per ora non vengono disegnati.
- Niente opzione «Schermo intero» né F11: il gioco è sempre a schermo intero sul Deck.
- I caricamenti rapidi mantengono la musica, le battaglie e le texture, anche dopo aver riavviato il gioco.
- I caricamenti rapidi fatti mentre si vedono le ricompense della battaglia non perdono più le texture dello scenario (fai un nuovo salvataggio rapido: i vecchi mantengono il problema).
- Continua carica il tuo ultimo salvataggio rapido, non il punto in cui hai lasciato il gioco.
- Sovrascrivere un salvataggio rapido: la domanda parte da Sovrascrivi, non da Annulla.

## 0.11.0 - 2026-11-19

- Continua: la schermata iniziale riprende da dove hai lasciato il gioco.
- Stati di salvataggio: 5 slot, F2 per salvare, F3 per caricare (vedi `docs/SAVESTATES.md`).
- Tasti posteriori impostati all'aggiunta a Steam: L4 menu, L5 pausa, R4 salva, R5 carica.
- Suggerimenti illustrati nell'installer durante la compilazione.
- Transizione di caricamento dopo Gioca.
- Schermata iniziale e impostazioni ridisegnate; l'aggiornamento è in «Novità».
- Il menu è un'unica finestra centrata, con le pagine Stati di salvataggio, Azioni e Aiuto.
- La pausa è su F5 (prima F2).
- Il gioco va in pausa quando la sua finestra perde il focus (nuovo default).
