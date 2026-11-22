# Notas de versión (español)

Las tres últimas versiones de `CHANGELOG.md`, traducidas: la pantalla de
inicio las muestra en este idioma. Los títulos (`## versión - fecha`) se
quedan como en `CHANGELOG.md`: son los que unen una versión con su
traducción. Una versión que falte en este archivo se muestra en inglés.

## 0.12.3 - 2026-11-22

- El aviso de RetroAchievements ya no aparece cada vez que se inicia el juego.
- Los avisos de logro desbloqueado, cámara libre, bolsa llena y guardado rápido están traducidos al francés, alemán, español e italiano.
- El brillo alrededor de un personaje (el aura de la Espada del Rayo) vuelve a ser un fino contorno blanco, ya no una ancha mancha de luz en el suelo.
- El nombre de la zona en la parte inferior de la pantalla se muestra completo en francés, alemán, español e italiano (los acentos se dibujan como letras simples); se cortaba en el primer acento o guion, o no aparecía.

## 0.12.2 - 2026-11-22

- El salto hacia atrás en combate ya no es mucho más alto y largo de lo debido a 60 fps.

## 0.12.1 - 2026-11-22

- Importar una partida de PCSX2 ya no deja una partida vacía (el juego decía que estaba dañada) con algunas tarjetas de memoria; volver a importar desde el instalador la repara.
- Con un disco europeo, el texto del juego arranca en el idioma de la Deck (el de Steam, si no el del sistema) en el primer inicio, como el instalador y el menú; en inglés si ese idioma no es uno de los cinco.
- Los monstruos perro de piedra tienen sus texturas (eran grises y transparentes).
- Cargar un estado guardado ya no congela ni cierra el juego, ni rompe el menú de pausa, con los textos europeos.

## 0.12.0 - 2026-11-22

- El instalador puede importar tu partida de PCSX2: busca solo tu tarjeta de memoria (PCSX2, EmuDeck, RetroDECK, tarjeta SD), y puedes elegir un archivo o una carpeta a mano si no encuentra ninguna.
- Los logos del arranque se pueden saltar con un botón (A, B, X, Y o Start), también en el primer inicio.
- El «Pulsa A» de la pantalla de carga aparece en cuanto el juego ha cargado, no tras todo el texto de la historia.
- La pantalla de inicio hace sonidos suaves: un tic ligero al mover la selección, un toque al pulsar un botón y un acorde cálido cuando Jugar inicia el juego.
- Una partida nueva empieza con la rotación de la cámara en «Invertida»; las partidas existentes conservan su elección.
- Se acabaron las combinaciones de botones: L4 (F1) abre el menú. Herramientas de desarrollo eliminadas.
- Renderizador nativo, más ligero en la Deck: 60 fps estables, unos 4 W menos. Empieza en 2x. Los paquetes de texturas no se dibujan por ahora.
- Sin opción «Pantalla completa» ni F11: el juego siempre va a pantalla completa en la Deck.
- Las cargas rápidas conservan la música, los combates y las texturas, también tras reiniciar el juego.
- Las cargas rápidas hechas mientras se muestran las recompensas del combate ya no pierden las texturas del escenario (haz un nuevo guardado rápido: los antiguos conservan el problema).
- Continuar carga tu último guardado rápido, no el lugar donde dejaste el juego.
- Sobrescribir un guardado rápido: la pregunta empieza en Sobrescribir, no en Cancelar.

## 0.11.0 - 2026-11-19

- Continuar: la pantalla de inicio retoma donde dejaste el juego.
- Estados de guardado: 5 ranuras, F2 para guardar, F3 para cargar (ver `docs/SAVESTATES.md`).
- Botones traseros configurados al añadir a Steam: L4 menú, L5 pausa, R4 guardar, R5 cargar.
- Consejos ilustrados en el instalador mientras se compila el juego.
- Transición de carga tras Jugar.
- Pantalla de inicio y ajustes rediseñados; la actualización está en «Novedades».
- El menú es una sola ventana centrada, con las páginas Estados de guardado, Acciones y Ayuda.
- La pausa está en F5 (antes F2).
- El juego se pausa cuando su ventana pierde el foco (nuevo valor por defecto).
