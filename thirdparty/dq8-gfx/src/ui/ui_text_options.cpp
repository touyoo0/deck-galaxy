// Translations (ui_text.h): the game's own options, as src/graphics.cpp lists
// them (label, help, section), the reasons they are greyed out, and the
// RetroAchievements notices. Order: English, French, German, Spanish,
// Italian. An empty text shows the English one.
#include "ui/ui_text_data.h"

namespace dq8::ui {

const TextEntry kTextsOptions[] = {
    // sections
    {"Camera and view", "Caméra et vue", "Kamera und Ansicht", "Cámara y vista", "Camera e visuale"},
    {"Convenience", "Confort", "Komfort", "Comodidad", "Comodità"},
    {"Battles and rewards", "Combats et récompenses", "Kämpfe und Belohnungen", "Combates y recompensas",
     "Scontri e ricompense"},
    {"Developers", "Développeurs", "Entwickler", "Desarrolladores", "Sviluppatori"},

    // the options
    {"60 frames per second", "60 images par seconde", "60 Bilder pro Sekunde", "60 fotogramas por segundo",
     "60 fotogrammi al secondo"},
    {"The game runs at 60 frames a second instead of its own 30, its logic still at the PS2's pace; movies and the "
     "loading screen keep 30. Takes effect the next time the game starts.",
     "Le jeu tourne à 60 images par seconde au lieu de ses 30, sa logique gardant le rythme de la PS2 ; les films et l'écran de chargement restent à 30. Prend effet au prochain démarrage du jeu.",
     "Das Spiel läuft mit 60 statt seiner eigenen 30 Bilder pro Sekunde, die Logik bleibt im Tempo der PS2; Filme und der Ladebildschirm bleiben bei 30. Wirkt beim nächsten Start des Spiels.",
     "El juego va a 60 fotogramas por segundo en lugar de sus 30, con su lógica al ritmo de la PS2; las películas y la pantalla de carga se quedan en 30. Surte efecto la próxima vez que se inicie el juego.",
     "Il gioco gira a 60 fotogrammi al secondo invece dei suoi 30, con la logica al ritmo della PS2; i filmati e la schermata di caricamento restano a 30. Ha effetto al prossimo avvio del gioco."},

    {"Minimap size", "Taille de la mini-carte", "Minikartengröße", "Tamaño del minimapa",
     "Dimensione della minimappa"},
    {"The map in the corner while exploring, against the game's size (1.0x); made smaller, it keeps to its corner. "
     "The transporter menu's map stays as it is.",
     "La carte dans le coin pendant l'exploration, par rapport à la taille du jeu (1.0x) ; réduite, elle reste dans son coin. La carte du menu du transporteur ne change pas.",
     "Die Karte in der Ecke beim Erkunden, im Vergleich zur Größe des Spiels (1.0x); verkleinert bleibt sie in ihrer Ecke. Die Karte im Transporter-Menü bleibt unverändert.",
     "El mapa de la esquina al explorar, respecto al tamaño del juego (1.0x); si se reduce, se queda en su esquina. El mapa del menú del transportador no cambia.",
     "La mappa nell'angolo durante l'esplorazione, rispetto alla dimensione del gioco (1.0x); se rimpicciolita, resta nel suo angolo. La mappa del menu del trasportatore non cambia."},

    {"Compass size", "Taille de la boussole", "Kompassgröße", "Tamaño de la brújula", "Dimensione della bussola"},
    {"The compass in the corner while exploring, against the game's size (1.0x); made smaller, it keeps to its "
     "corner.",
     "La boussole dans le coin pendant l'exploration, par rapport à la taille du jeu (1.0x) ; réduite, elle reste dans son coin.",
     "Der Kompass in der Ecke beim Erkunden, im Vergleich zur Größe des Spiels (1.0x); verkleinert bleibt er in seiner Ecke.",
     "La brújula de la esquina al explorar, respecto al tamaño del juego (1.0x); si se reduce, se queda en su esquina.",
     "La bussola nell'angolo durante l'esplorazione, rispetto alla dimensione del gioco (1.0x); se rimpicciolita, resta nel suo angolo."},

    {"Field of view", "Champ de vision", "Sichtfeld", "Campo de visión", "Campo visivo"},
    {"How wide the camera sees while exploring and in battle, in degrees across a 4:3 picture (the game's is 52); a "
     "wide picture adds to it. Scenes keep their own camera.",
     "L'ouverture de la caméra pendant l'exploration et les combats, en degrés sur une image 4:3 (celle du jeu est 52) ; une image large s'y ajoute. Les scènes gardent leur propre caméra.",
     "Wie weit die Kamera beim Erkunden und im Kampf sieht, in Grad über ein 4:3-Bild (im Spiel sind es 52); ein breites Bild kommt hinzu. Szenen behalten ihre eigene Kamera.",
     "Cuánto abre la cámara al explorar y en combate, en grados sobre una imagen 4:3 (la del juego es 52); una imagen ancha se suma. Las escenas conservan su propia cámara.",
     "Quanto ampia è la visuale della camera durante l'esplorazione e in battaglia, in gradi su un'immagine 4:3 (quella del gioco è 52); un'immagine larga si aggiunge. Le scene mantengono la propria camera."},
    {"%d degrees", "%d degrés", "%d Grad", "%d grados", "%d gradi"},

    {"Camera distance", "Distance de la caméra", "Kameraabstand", "Distancia de la cámara",
     "Distanza della camera"},
    {"How far the camera stays from the party, against the game's (1.0x).",
     "La distance de la caméra au groupe, par rapport à celle du jeu (1.0x).",
     "Wie weit die Kamera von der Gruppe entfernt bleibt, im Vergleich zum Spiel (1.0x).",
     "A qué distancia se queda la cámara del grupo, respecto a la del juego (1.0x).",
     "A che distanza la camera resta dal gruppo, rispetto a quella del gioco (1.0x)."},

    {"Faster camera rotation", "Rotation de caméra plus rapide", "Schnellere Kameradrehung",
     "Rotación de cámara más rápida", "Rotazione della camera più veloce"},
    {"The stick turns the camera twice as fast, exploring, in battle and in the first-person view. Otherwise it "
     "turns at the game's speed, at 60 frames a second as at 30.",
     "Le stick fait tourner la caméra deux fois plus vite, en exploration, en combat et à la première personne. Sinon elle tourne à la vitesse du jeu, à 60 images par seconde comme à 30.",
     "Der Stick dreht die Kamera doppelt so schnell, beim Erkunden, im Kampf und in der Ego-Ansicht. Sonst dreht sie sich mit der Geschwindigkeit des Spiels, bei 60 Bildern pro Sekunde wie bei 30.",
     "El stick gira la cámara el doble de rápido, al explorar, en combate y en vista en primera persona. Si no, gira a la velocidad del juego, a 60 fotogramas por segundo igual que a 30.",
     "Lo stick ruota la camera il doppio più velocemente, durante l'esplorazione, in battaglia e in vista in prima persona. Altrimenti ruota alla velocità del gioco, a 60 fotogrammi al secondo come a 30."},

    {"Draw distance", "Distance d'affichage", "Sichtweite", "Distancia de dibujado",
     "Distanza di visualizzazione"},
    {"How far away the scenery and the objects are still drawn, against the game's (1.0x), the fog thinned in step. "
     "Most of Salgin has no limit to raise; the open areas gain more. Takes effect from the next map loaded.",
     "À quelle distance le décor et les objets sont encore dessinés, par rapport au jeu (1.0x), le brouillard s'éclaircissant en proportion. La plus grande partie de Salgin n'a pas de limite à repousser ; les zones ouvertes y gagnent davantage. Prend effet à partir de la prochaine carte chargée.",
     "Wie weit entfernt Kulisse und Objekte noch gezeichnet werden, im Vergleich zum Spiel (1.0x); der Nebel wird entsprechend gelichtet. Der Großteil von Salgin hat keine Grenze, die sich anheben ließe; die offenen Gebiete profitieren mehr. Wirkt ab der nächsten geladenen Karte.",
     "A qué distancia se siguen dibujando el escenario y los objetos, respecto al juego (1.0x), con la niebla aclarada en proporción. La mayor parte de Salgin no tiene un límite que ampliar; las zonas abiertas ganan más. Surte efecto desde el siguiente mapa que se cargue.",
     "A che distanza vengono ancora disegnati lo scenario e gli oggetti, rispetto al gioco (1.0x), con la nebbia diradata in proporzione. La maggior parte di Salgin non ha un limite da alzare; le aree aperte ne guadagnano di più. Ha effetto dalla prossima mappa caricata."},

    {"Free camera (F6)", "Caméra libre (F6)", "Freie Kamera (F6)", "Cámara libre (F6)", "Camera libera (F6)"},
    {"The camera leaves the party, which stands still, and the controller moves it: the left stick moves the point "
     "it looks at (Square held, faster; L2 held, up and down), the right stick turns it around that point and tilts "
     "it (R2 held, nearer and farther). Start or F6 turns it off, as this switch does, to play again. Off at every "
     "start.",
     "La caméra quitte le groupe, qui reste immobile, et la manette la déplace : le stick gauche déplace le point regardé (Carré maintenu, plus vite ; L2 maintenu, haut et bas), le stick droit la fait tourner autour de ce point et l'incline (R2 maintenu, plus près et plus loin). Start ou F6 la désactive, comme cet interrupteur, pour rejouer. Désactivée à chaque démarrage.",
     "Die Kamera löst sich von der Gruppe, die stehen bleibt, und der Controller bewegt sie: Der linke Stick bewegt den Punkt, auf den sie blickt (Quadrat gehalten, schneller; L2 gehalten, hoch und runter), der rechte Stick dreht sie um diesen Punkt und neigt sie (R2 gehalten, näher und weiter weg). Start oder F6 schaltet sie aus, ebenso dieser Schalter, um wieder zu spielen. Bei jedem Start aus.",
     "La cámara se separa del grupo, que se queda quieto, y el mando la mueve: el stick izquierdo mueve el punto al que mira (Cuadrado mantenido, más rápido; L2 mantenido, arriba y abajo), el stick derecho la gira alrededor de ese punto y la inclina (R2 mantenido, más cerca y más lejos). Start o F6 la desactiva, igual que este interruptor, para volver a jugar. Desactivada en cada inicio.",
     "La camera si stacca dal gruppo, che resta fermo, e il controller la muove: lo stick sinistro sposta il punto che guarda (Quadrato tenuto premuto, più veloce; L2 tenuto premuto, su e giù), lo stick destro la ruota attorno a quel punto e la inclina (R2 tenuto premuto, più vicino e più lontano). Start o F6 la disattiva, come questo interruttore, per tornare a giocare. Disattivata a ogni avvio."},
    {"Free camera", "Caméra libre", "Freie Kamera", "Cámara libre", "Camera libera"},

    {"Skip the opening", "Passer l'intro", "Intro überspringen", "Saltar la introducción",
     "Salta l'introduzione"},
    {"At boot, the Sony and Level-5 logos and the opening movie give way to the title screen at once.",
     "Au démarrage, les logos de Sony et de Level-5 et le film d'ouverture laissent aussitôt place à l'écran-titre.",
     "Beim Start weichen die Logos von Sony und Level-5 und der Eröffnungsfilm sofort dem Titelbildschirm.",
     "Al arrancar, los logotipos de Sony y Level-5 y la película de apertura dan paso de inmediato a la pantalla de título.",
     "All'avvio, i loghi di Sony e Level-5 e il filmato di apertura lasciano subito il posto alla schermata del titolo."},

    {"Quick loading recap", "Résumé de chargement rapide", "Schnelle Ladezusammenfassung",
     "Resumen de carga rápido", "Riepilogo di caricamento rapido"},
    {"When a game loads, the story so far appears whole instead of line by line, and Press X comes as soon as it "
     "has loaded.",
     "Au chargement d'une partie, l'histoire jusque-là s'affiche d'un bloc au lieu de ligne par ligne, et « Press X » apparaît dès que le chargement est terminé.",
     "Beim Laden eines Spielstands erscheint die bisherige Geschichte auf einmal statt Zeile für Zeile, und „Press X“ erscheint, sobald geladen ist.",
     "Al cargar una partida, la historia hasta ahora aparece entera en vez de línea a línea, y «Press X» aparece en cuanto termina la carga.",
     "Quando si carica una partita, la storia finora appare tutta insieme invece che riga per riga, e «Press X» compare appena terminato il caricamento."},

    {"Instant text", "Texte instantané", "Sofortiger Text", "Texto instantáneo", "Testo istantaneo"},
    {"Dialogue and messages appear a page at a time instead of letter by letter, as the game itself does where a "
     "scene asks for it; the pauses written into the text stay.",
     "Les dialogues et messages apparaissent une page à la fois au lieu de lettre par lettre, comme le jeu le fait lui-même quand une scène le demande ; les pauses écrites dans le texte restent.",
     "Dialoge und Meldungen erscheinen seitenweise statt Buchstabe für Buchstabe, wie es das Spiel selbst tut, wenn eine Szene es verlangt; die im Text vorgesehenen Pausen bleiben.",
     "Los diálogos y mensajes aparecen una página a la vez en lugar de letra a letra, como hace el propio juego cuando una escena lo pide; las pausas escritas en el texto se mantienen.",
     "I dialoghi e i messaggi appaiono una pagina alla volta invece che lettera per lettera, come fa il gioco stesso quando una scena lo richiede; le pause scritte nel testo restano."},

    {"Chests on the map", "Coffres sur la carte", "Truhen auf der Karte", "Cofres en el mapa",
     "Forzieri sulla mappa"},
    {"The chests not yet opened show on the maps in the parts you have explored, as the game shows them once every "
     "save point of a planet is activated. Only the map changes: nothing goes into your save, and activating every "
     "save point still counts as before.",
     "Les coffres pas encore ouverts s'affichent sur les cartes dans les parties explorées, comme le jeu les montre une fois tous les points de sauvegarde d'une planète activés. Seule la carte change : rien n'entre dans votre sauvegarde, et activer tous les points de sauvegarde compte comme avant.",
     "Noch nicht geöffnete Truhen werden auf den Karten in den erkundeten Bereichen angezeigt, so wie das Spiel sie zeigt, sobald alle Speicherpunkte eines Planeten aktiviert sind. Nur die Karte ändert sich: Nichts gelangt in deinen Spielstand, und das Aktivieren aller Speicherpunkte zählt weiterhin wie zuvor.",
     "Los cofres aún sin abrir aparecen en los mapas en las partes exploradas, como los muestra el juego cuando se activan todos los puntos de guardado de un planeta. Solo cambia el mapa: nada se guarda en tu partida, y activar todos los puntos de guardado cuenta como antes.",
     "I forzieri non ancora aperti compaiono sulle mappe nelle parti esplorate, come li mostra il gioco una volta attivati tutti i punti di salvataggio di un pianeta. Cambia solo la mappa: nulla entra nel tuo salvataggio, e attivare tutti i punti di salvataggio conta come prima."},

    {"Checks the game's 130 RetroAchievements as you play and shows a notice when one is earned. Turn it on, "
     "then sign in; nothing is sent but your unlocks.",
     "Vérifie les 130 RetroAchievements du jeu pendant que vous jouez et affiche un avis quand l'un est obtenu. Activez-le, puis connectez-vous ; rien n'est envoyé hormis vos déblocages.",
     "Prüft beim Spielen die 130 RetroAchievements des Spiels und zeigt eine Meldung, wenn einer erreicht wird. Schalte es ein und melde dich an; gesendet werden nur deine Freischaltungen.",
     "Comprueba los 130 RetroAchievements del juego mientras juegas y muestra un aviso cuando se consigue uno. Actívalo e inicia sesión; solo se envían tus logros desbloqueados.",
     "Controlla i 130 RetroAchievements del gioco mentre giochi e mostra un avviso quando se ne ottiene uno. Attivalo e accedi; viene inviato solo ciò che sblocchi."},

    {"Fast transporters", "Transporteurs rapides", "Schnelle Transporter", "Transportadores rápidos",
     "Trasportatori veloci"},
    {"A transporter's warp plays at up to twice the speed. The music and the sounds keep their pace.",
     "Le saut d'un transporteur se joue jusqu'à deux fois plus vite. La musique et les sons gardent leur rythme.",
     "Der Warp eines Transporters läuft mit bis zu doppelter Geschwindigkeit. Musik und Geräusche behalten ihr Tempo.",
     "El salto de un transportador se reproduce hasta al doble de velocidad. La música y los sonidos mantienen su ritmo.",
     "Il salto di un trasportatore viene riprodotto fino al doppio della velocità. La musica e i suoni mantengono il loro ritmo."},

    {"Check for new versions", "Rechercher de nouvelles versions", "Nach neuen Versionen suchen",
     "Buscar nuevas versiones", "Cerca nuove versioni"},
    {"At each start the game asks GitHub for this project's releases (nothing is sent but the request) and, when a "
     "newer one is out, says so and links to it from the menu's About window.",
     "À chaque démarrage, le jeu demande à GitHub les versions de ce projet (rien n'est envoyé hormis la requête) et, quand une plus récente est sortie, l'indique et y renvoie depuis la fenêtre À propos du menu.",
     "Bei jedem Start fragt das Spiel GitHub nach den Versionen dieses Projekts (gesendet wird nur die Anfrage) und weist, wenn eine neuere erschienen ist, darauf hin und verlinkt sie im Info-Fenster des Menüs.",
     "En cada inicio el juego pide a GitHub las versiones de este proyecto (solo se envía la petición) y, cuando hay una más reciente, lo indica y enlaza a ella desde la ventana Acerca de del menú.",
     "A ogni avvio il gioco chiede a GitHub le versioni di questo progetto (viene inviata solo la richiesta) e, quando ne è uscita una più recente, lo segnala e la collega dalla finestra Informazioni del menu."},

    {"Full bag to storage", "Sac plein vers le stockage", "Volle Tasche ins Lager", "Bolsa llena al almacén",
     "Borsa piena al deposito"},
    {"An item found when the bag has no room for it (no free space, or as many of that item as the bag holds) goes "
     "to the transporter's item storage, instead of the overflow the game makes you sort out in the item menu, or "
     "losing it when that is full too. Shops and the Factory still ask for room in the bag.",
     "Un objet trouvé alors que le sac n'a plus de place (plus d'espace libre, ou autant de cet objet que le sac peut en contenir) va dans le stockage d'objets du transporteur, au lieu du surplus que le jeu vous oblige à trier dans le menu d'objets, ou d'être perdu quand celui-ci est plein aussi. Les boutiques et l'Usine demandent toujours de la place dans le sac.",
     "Ein gefundener Gegenstand, für den die Tasche keinen Platz hat (kein freier Platz oder so viele davon, wie die Tasche fasst), landet im Gegenstandslager des Transporters, statt im Überlauf, den das Spiel im Gegenstandsmenü sortieren lässt, oder verloren zu gehen, wenn auch dieses voll ist. Läden und die Fabrik verlangen weiterhin Platz in der Tasche.",
     "Un objeto encontrado cuando la bolsa no tiene sitio (sin espacio libre, o tantos de ese objeto como cabe en la bolsa) va al almacén de objetos del transportador, en lugar del desbordamiento que el juego te obliga a ordenar en el menú de objetos, o de perderse cuando este también está lleno. Las tiendas y la Fábrica siguen pidiendo espacio en la bolsa.",
     "Un oggetto trovato quando la borsa non ha spazio (nessuno spazio libero, o tanti di quell'oggetto quanti ne contiene la borsa) va nel deposito oggetti del trasportatore, invece del sovraccarico che il gioco ti costringe a smistare nel menu oggetti, o di andare perso quando anche quello è pieno. I negozi e la Fabbrica continuano a richiedere spazio nella borsa."},

    {"Item limit 99", "Limite d'objets 99", "Gegenstandslimit 99", "Límite de objetos 99", "Limite oggetti 99"},
    {"Every item that stacks can be carried up to 99 in the bag. The game holds some to 20, 30 or 50, healing items "
     "and boosts among them. Off again, nothing carried is lost; more just cannot be added.",
     "Tout objet empilable peut être porté jusqu'à 99 dans le sac. Le jeu en limite certains à 20, 30 ou 50, dont des objets de soin et des boosts. Désactivé de nouveau, rien de ce qui est porté n'est perdu ; on ne peut simplement plus en ajouter.",
     "Jeder stapelbare Gegenstand kann bis zu 99-mal in der Tasche getragen werden. Das Spiel begrenzt einige auf 20, 30 oder 50, darunter Heilgegenstände und Boosts. Wieder ausgeschaltet geht nichts Getragenes verloren; es kann nur nichts mehr hinzukommen.",
     "Todo objeto apilable puede llevarse hasta 99 en la bolsa. El juego limita algunos a 20, 30 o 50, entre ellos objetos de curación y potenciadores. Al desactivarlo de nuevo, no se pierde nada de lo que llevas; solo que no se puede añadir más.",
     "Ogni oggetto impilabile può essere portato fino a 99 nella borsa. Il gioco ne limita alcuni a 20, 30 o 50, tra cui oggetti di cura e potenziamenti. Disattivando di nuovo, nulla di ciò che si porta va perso; semplicemente non se ne può aggiungere altro."},

    {"Seventh bag page", "Septième page du sac", "Siebte Taschenseite", "Séptima página de la bolsa",
     "Settima pagina della borsa"},
    {"The bag gets a seventh page: 280 spaces instead of 240. The transporter's storage keeps its 320. A save made "
     "with it still loads without it: the seventh page's items are kept, hidden, until it is on again. Takes effect "
     "the next time the game starts.",
     "Le sac gagne une septième page : 280 places au lieu de 240. Le stockage du transporteur garde ses 320. Une sauvegarde faite avec cette option se charge toujours sans elle : les objets de la septième page sont conservés, cachés, jusqu'à sa réactivation. Prend effet au prochain démarrage du jeu.",
     "Die Tasche erhält eine siebte Seite: 280 Plätze statt 240. Das Lager des Transporters behält seine 320. Ein damit erstellter Spielstand lässt sich auch ohne sie laden: Die Gegenstände der siebten Seite bleiben erhalten, versteckt, bis sie wieder aktiv ist. Wirkt beim nächsten Start des Spiels.",
     "La bolsa obtiene una séptima página: 280 espacios en lugar de 240. El almacén del transportador conserva sus 320. Una partida guardada con ella sigue cargando sin ella: los objetos de la séptima página se conservan, ocultos, hasta que vuelva a estar activa. Surte efecto la próxima vez que se inicie el juego.",
     "La borsa ottiene una settima pagina: 280 posti invece di 240. Il deposito del trasportatore mantiene i suoi 320. Un salvataggio fatto con essa si carica comunque senza: gli oggetti della settima pagina vengono conservati, nascosti, finché non viene riattivata. Ha effetto al prossimo avvio del gioco."},

    {"Encounter rate", "Fréquence des combats", "Begegnungsrate", "Frecuencia de combates",
     "Frequenza degli scontri"},
    {"How often random battles come, against the game's (1.0x); 0.0x turns them off. The story's battles stay.",
     "La fréquence des combats aléatoires, par rapport à celle du jeu (1.0x) ; 0.0x les désactive. Les combats de l'histoire restent.",
     "Wie oft Zufallskämpfe auftreten, im Vergleich zum Spiel (1.0x); 0.0x schaltet sie aus. Die Kämpfe der Handlung bleiben.",
     "Con qué frecuencia llegan los combates aleatorios, respecto al juego (1.0x); 0.0x los desactiva. Los combates de la historia se mantienen.",
     "Quanto spesso arrivano gli scontri casuali, rispetto al gioco (1.0x); 0.0x li disattiva. Gli scontri della storia restano."},

    {"Experience from battles", "Expérience des combats", "Erfahrung aus Kämpfen", "Experiencia de los combates",
     "Esperienza dai combattimenti"},
    {"The experience a won battle gives, against the game's (1.0x), up to the game's 9999 a battle.",
     "L'expérience que donne un combat gagné, par rapport à celle du jeu (1.0x), jusqu'aux 9999 par combat du jeu.",
     "Die Erfahrung, die ein gewonnener Kampf bringt, im Vergleich zum Spiel (1.0x), bis zu den 9999 pro Kampf des Spiels.",
     "La experiencia que da un combate ganado, respecto al juego (1.0x), hasta los 9999 por combate del juego.",
     "L'esperienza data da uno scontro vinto, rispetto al gioco (1.0x), fino ai 9999 per scontro del gioco."},

    {"Money from battles", "Argent des combats", "Geld aus Kämpfen", "Dinero de los combates",
     "Denaro dai combattimenti"},
    {"The money defeated enemies leave, against the game's (1.0x). Selling, chests and rewards stay as they are.",
     "L'argent que laissent les ennemis vaincus, par rapport à celui du jeu (1.0x). Les ventes, les coffres et les récompenses ne changent pas.",
     "Das Geld, das besiegte Gegner hinterlassen, im Vergleich zum Spiel (1.0x). Verkäufe, Truhen und Belohnungen bleiben, wie sie sind.",
     "El dinero que dejan los enemigos derrotados, respecto al juego (1.0x). Las ventas, los cofres y las recompensas se quedan como están.",
     "Il denaro lasciato dai nemici sconfitti, rispetto al gioco (1.0x). Vendite, forzieri e ricompense restano come sono."},

    {"Item drops", "Objets lâchés", "Gegenstandsdrops", "Botín de objetos", "Oggetti rilasciati"},
    {"How likely defeated enemies are to leave each of their items, against the game's (1.0x): most have a 2 to 10% "
     "chance, which 10.0x makes 20 to 100%; 0.0x for none. A battle still gives three kinds of items at most, as "
     "the game does.",
     "La probabilité que les ennemis vaincus laissent chacun de leurs objets, par rapport à celle du jeu (1.0x) : la plupart ont 2 à 10 % de chances, que 10.0x porte à 20 à 100 % ; 0.0x pour aucun. Un combat ne donne toujours que trois types d'objets au plus, comme le jeu.",
     "Wie wahrscheinlich besiegte Gegner jeden ihrer Gegenstände hinterlassen, im Vergleich zum Spiel (1.0x): Die meisten haben 2 bis 10 % Chance, was 10.0x zu 20 bis 100 % macht; 0.0x für keine. Ein Kampf gibt weiterhin höchstens drei Arten von Gegenständen, wie im Spiel.",
     "Qué probabilidad tienen los enemigos derrotados de dejar cada uno de sus objetos, respecto al juego (1.0x): la mayoría tiene un 2 a 10 % de probabilidad, que 10.0x convierte en 20 a 100 %; 0.0x para ninguno. Un combate sigue dando como mucho tres tipos de objetos, como hace el juego.",
     "Quanto è probabile che i nemici sconfitti rilascino ciascuno dei loro oggetti, rispetto al gioco (1.0x): la maggior parte ha il 2-10% di probabilità, che 10.0x porta al 20-100%; 0.0x per nessuno. Uno scontro dà comunque al massimo tre tipi di oggetti, come fa il gioco."},

    {"Weapon growth", "Croissance des armes", "Waffenwachstum", "Crecimiento de las armas",
     "Crescita delle armi"},
    {"How fast weapons grow in won battles, against the game's (1.0x): each battle fills a weapon's gauge by 1 (15 "
     "to full), then raises one of its attributes by 1, which 3.0x makes 3.",
     "La vitesse à laquelle les armes progressent dans les combats gagnés, par rapport au jeu (1.0x) : chaque combat remplit la jauge d'une arme de 1 (15 pour la remplir), puis augmente l'un de ses attributs de 1, ce que 3.0x porte à 3.",
     "Wie schnell Waffen in gewonnenen Kämpfen wachsen, im Vergleich zum Spiel (1.0x): Jeder Kampf füllt die Anzeige einer Waffe um 1 (15 bis voll), dann erhöht er eines ihrer Attribute um 1, was 3.0x zu 3 macht.",
     "Con qué rapidez crecen las armas en los combates ganados, respecto al juego (1.0x): cada combate llena el medidor de un arma en 1 (15 hasta llenarlo), y luego sube uno de sus atributos en 1, que 3.0x convierte en 3.",
     "Con che rapidità le armi crescono negli scontri vinti, rispetto al gioco (1.0x): ogni scontro riempie di 1 la barra di un'arma (15 per riempirla), poi alza di 1 uno dei suoi attributi, che 3.0x porta a 3."},

    {"Shop prices", "Prix des boutiques", "Ladenpreise", "Precios de las tiendas", "Prezzi dei negozi"},
    {"What shops ask for their items, against the game's (1.0x). Below 1.0x nothing sells for more than it costs, "
     "so buying and selling back makes no money.",
     "Ce que demandent les boutiques pour leurs objets, par rapport au jeu (1.0x). Sous 1.0x, rien ne se vend plus cher qu'il ne coûte, donc acheter puis revendre ne rapporte rien.",
     "Was Läden für ihre Gegenstände verlangen, im Vergleich zum Spiel (1.0x). Unter 1.0x wird nichts für mehr verkauft, als es kostet, sodass Kaufen und Zurückverkaufen kein Geld bringt.",
     "Lo que piden las tiendas por sus objetos, respecto al juego (1.0x). Por debajo de 1.0x nada se vende por más de lo que cuesta, así que comprar y revender no da dinero.",
     "Quanto chiedono i negozi per i loro oggetti, rispetto al gioco (1.0x). Sotto 1.0x nulla si vende a più di quanto costa, quindi comprare e rivendere non fa guadagnare."},

    {"Action gauge speed", "Vitesse de la jauge d'action", "Geschwindigkeit der Aktionsleiste",
     "Velocidad del medidor de acción", "Velocità della barra azione"},
    {"How fast the party's action gauge fills again in battle, against the game's (1.0x).",
     "La vitesse à laquelle la jauge d'action du groupe se remplit à nouveau en combat, par rapport au jeu (1.0x).",
     "Wie schnell sich die Aktionsleiste der Gruppe im Kampf wieder füllt, im Vergleich zum Spiel (1.0x).",
     "Con qué rapidez se vuelve a llenar el medidor de acción del grupo en combate, respecto al juego (1.0x).",
     "Con che rapidità la barra azione del gruppo si riempie di nuovo in battaglia, rispetto al gioco (1.0x)."},

    {"Director's Cut balance", "Équilibrage Director's Cut", "Director's-Cut-Balance",
     "Equilibrio del Director's Cut", "Bilanciamento Director's Cut"},
    {"Battles with the numbers of the Japanese Director's Cut: smaller enemy groups, more money, some enemies "
     "weaker and the Area Masters stronger, bosses never knocked down. Needs your Japanese disc "
     "(add it in the installer). Takes effect the next time the game starts.",
     "Des combats avec les chiffres du Director's Cut japonais : groupes d'ennemis plus petits, plus d'argent, certains ennemis plus faibles et les Area Masters plus forts, boss jamais renversés. Nécessite votre disque japonais (à ajouter dans l'installeur). Prend effet au prochain démarrage du jeu.",
     "Kämpfe mit den Werten des japanischen Director's Cut: kleinere Gegnergruppen, mehr Geld, manche Gegner schwächer und die Area Masters stärker, Bosse werden nie umgeworfen. Benötigt deine japanische Disc (im Installer hinzufügen). Wirkt beim nächsten Start des Spiels.",
     "Combates con las cifras del Director's Cut japonés: grupos de enemigos más pequeños, más dinero, algunos enemigos más débiles y los Area Masters más fuertes, jefes que nunca caen derribados. Necesita tu disco japonés (añádelo en el instalador). Surte efecto la próxima vez que se inicie el juego.",
     "Scontri con i valori del Director's Cut giapponese: gruppi di nemici più piccoli, più denaro, alcuni nemici più deboli e gli Area Master più forti, boss mai abbattuti. Richiede il tuo disco giapponese (aggiungilo nell'installer). Ha effetto al prossimo avvio del gioco."},

    {"Director's Cut enemy behavior", "Comportement des ennemis Director's Cut",
     "Gegnerverhalten wie im Director's Cut", "Comportamiento enemigo del Director's Cut",
     "Comportamento dei nemici Director's Cut"},
    {"The enemy attacks and AI of the Japanese Director's Cut: enemies face you before they attack, some attack "
     "from farther away. Needs your Japanese disc (add it in the installer). Takes effect the next time the game starts.",
     "Les attaques et l'IA des ennemis du Director's Cut japonais : les ennemis vous font face avant d'attaquer, certains attaquent de plus loin. Nécessite votre disque japonais (à ajouter dans l'installeur). Prend effet au prochain démarrage du jeu.",
     "Die Angriffe und die KI der Gegner des japanischen Director's Cut: Gegner wenden sich dir zu, bevor sie angreifen, manche greifen aus größerer Entfernung an. Benötigt deine japanische Disc (im Installer hinzufügen). Wirkt beim nächsten Start des Spiels.",
     "Los ataques y la IA de los enemigos del Director's Cut japonés: los enemigos te miran antes de atacar, algunos atacan desde más lejos. Necesita tu disco japonés (añádelo en el instalador). Surte efecto la próxima vez que se inicie el juego.",
     "Gli attacchi e l'IA dei nemici del Director's Cut giapponese: i nemici ti si voltano verso prima di attaccare, alcuni attaccano da più lontano. Richiede il tuo disco giapponese (aggiungilo nell'installer). Ha effetto al prossimo avvio del gioco."},

    {"Director's Cut party AI", "IA du groupe Director's Cut", "Gruppen-KI wie im Director's Cut",
     "IA del grupo del Director's Cut", "IA del gruppo Director's Cut"},
    {"The ally AI of the Japanese Director's Cut: an ally low on health sometimes falls back to the leader and "
     "guards. Needs your Japanese disc (add it in the installer). Takes effect the next time the game starts.",
     "L'IA des alliés du Director's Cut japonais : un allié en mauvaise santé se replie parfois vers le chef et se garde. Nécessite votre disque japonais (à ajouter dans l'installeur). Prend effet au prochain démarrage du jeu.",
     "Die Verbündeten-KI des japanischen Director's Cut: Ein Verbündeter mit wenig Gesundheit zieht sich manchmal zum Anführer zurück und wehrt ab. Benötigt deine japanische Disc (im Installer hinzufügen). Wirkt beim nächsten Start des Spiels.",
     "La IA de los aliados del Director's Cut japonés: un aliado con poca salud a veces se retira hacia el líder y se defiende. Necesita tu disco japonés (añádelo en el instalador). Surte efecto la próxima vez que se inicie el juego.",
     "L'IA degli alleati del Director's Cut giapponese: un alleato con poca salute a volte si ritira verso il leader e si difende. Richiede il tuo disco giapponese (aggiungilo nell'installer). Ha effetto al prossimo avvio del gioco."},

    {"Game language", "Langue du jeu", "Spielsprache", "Idioma del juego", "Lingua del gioco"},
    {"The game's text, menus and scenes, in one of the European release's languages. Needs your European disc "
     "(SCES-54552, add it in the installer); without it the text stays English. The voices stay as they are. Takes "
     "effect the next time the game starts.",
     "Le texte, les menus et les scènes du jeu, dans l'une des langues de l'édition européenne. Nécessite votre disque européen (SCES-54552, à ajouter dans l'installeur) ; sans lui, le texte reste en anglais. Les voix ne changent pas. Prend effet au prochain démarrage du jeu.",
     "Text, Menüs und Szenen des Spiels in einer der Sprachen der europäischen Version. Benötigt deine europäische Disc (SCES-54552, im Installer hinzufügen); ohne sie bleibt der Text englisch. Die Stimmen bleiben, wie sie sind. Wirkt beim nächsten Start des Spiels.",
     "El texto, los menús y las escenas del juego, en uno de los idiomas de la edición europea. Necesita tu disco europeo (SCES-54552, añádelo en el instalador); sin él, el texto sigue en inglés. Las voces se quedan como están. Surte efecto la próxima vez que se inicie el juego.",
     "Il testo, i menu e le scene del gioco, in una delle lingue dell'edizione europea. Richiede il tuo disco europeo (SCES-54552, aggiungilo nell'installer); senza, il testo resta in inglese. Le voci restano come sono. Ha effetto al prossimo avvio del gioco."},

    {"Undub (Japanese voices)", "Undub (voix japonaises)", "Undub (japanische Stimmen)", "Undub (voces japonesas)",
     "Undub (voci giapponesi)"},
    {"The voices in scenes and the movies come from your Japanese Director's Cut disc instead of the English ones; "
     "the text stays as it is. Needs your Japanese disc (add it in the installer); without it the voices stay English. A "
     "few lines have no Japanese voice and play silent. Takes effect the next time the game starts.",
     "Les voix des scènes et les films viennent de votre disque Director's Cut japonais au lieu des voix anglaises ; le texte ne change pas. Nécessite votre disque japonais (à ajouter dans l'installeur) ; sans lui, les voix restent en anglais. Quelques répliques n'ont pas de voix japonaise et sont muettes. Prend effet au prochain démarrage du jeu.",
     "Die Stimmen in Szenen und die Filme stammen von deiner japanischen Director's-Cut-Disc statt der englischen; der Text bleibt, wie er ist. Benötigt deine japanische Disc (im Installer hinzufügen); ohne sie bleiben die Stimmen englisch. Einige Zeilen haben keine japanische Stimme und bleiben stumm. Wirkt beim nächsten Start des Spiels.",
     "Las voces de las escenas y las películas vienen de tu disco del Director's Cut japonés en lugar de las inglesas; el texto se queda como está. Necesita tu disco japonés (añádelo en el instalador); sin él, las voces siguen en inglés. Algunas líneas no tienen voz japonesa y suenan en silencio. Surte efecto la próxima vez que se inicie el juego.",
     "Le voci nelle scene e i filmati provengono dal tuo disco Director's Cut giapponese invece di quelle inglesi; il testo resta com'è. Richiede il tuo disco giapponese (aggiungilo nell'installer); senza, le voci restano in inglese. Alcune battute non hanno una voce giapponese e restano mute. Ha effetto al prossimo avvio del gioco."},


    // why an option is greyed out
    {"Run the installer again for this: it adds these files.",
     "Relancez l'installeur pour cela : il ajoute ces fichiers.",
     "Starte den Installer dafür erneut: Er fügt diese Dateien hinzu.",
     "Ejecuta el instalador de nuevo para esto: añade estos archivos.",
     "Esegui di nuovo l'installer per questo: aggiunge questi file."},
    {"Needs your Japanese disc: add it in the installer.", "Nécessite votre disque japonais : ajoutez-le dans l'installeur.",
     "Benötigt deine japanische Disc: Füge sie im Installer hinzu.",
     "Necesita tu disco japonés: añádelo en el instalador.",
     "Richiede il tuo disco giapponese: aggiungilo nell'installer."},
    {"Needs your European disc: add it in the installer.",
     "Nécessite votre disque européen : ajoutez-le dans l'installeur.",
     "Benötigt deine europäische Disc: Füge sie im Installer hinzu.",
     "Necesita tu disco europeo: añádelo en el instalador.",
     "Richiede il tuo disco europeo: aggiungilo nell'installer."},

    // the shortcuts the game adds
    // RetroAchievements notices
    {"Signing in...", "Connexion...", "Anmeldung...", "Iniciando sesión...", "Accesso in corso..."},
    {"RetroAchievements: every achievement earned", "RetroAchievements : tous les succès obtenus",
     "RetroAchievements: alle Erfolge erreicht", "RetroAchievements: todos los logros conseguidos",
     "RetroAchievements: tutti gli obiettivi ottenuti"},
    {"RetroAchievements: offline, unlocks wait for the connection",
     "RetroAchievements : hors ligne, les déblocages attendent la connexion",
     "RetroAchievements: offline, Freischaltungen warten auf die Verbindung",
     "RetroAchievements: sin conexión, los logros esperan a que vuelva",
     "RetroAchievements: offline, gli sblocchi attendono la connessione"},
    {"RetroAchievements: back online", "RetroAchievements : de nouveau en ligne",
     "RetroAchievements: wieder online", "RetroAchievements: de nuevo en línea",
     "RetroAchievements: di nuovo online"},
};
const size_t kTextsOptionsCount = sizeof(kTextsOptions) / sizeof(kTextsOptions[0]);

} // namespace dq8::ui
