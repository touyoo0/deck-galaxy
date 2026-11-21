# Notas de versión (español)

Las tres últimas versiones de `CHANGELOG.md`, traducidas: la pantalla de
inicio las muestra en este idioma. Los títulos (`## versión - fecha`,
`### Added`) se quedan como en `CHANGELOG.md`: son los que unen una versión
con su traducción. Una versión que falte en este archivo se muestra en inglés.

## 0.12.0 - 2026-11-22

### Changed

- Ya no queda ninguna combinación de botones o teclas: Ver + Menú (Atrás + Start) ya no abre el menú, y Alt+Intro y Mayús+F12 desaparecen. El L4 de la Deck (F1) lo abre, y la indicación al iniciar lo dice. Se elimina la opción «Atrás + Start abre el menú». Las herramientas de desarrollo que quedaban en el juego (la opción «Herramientas de desarrollo», `RG_DEV_TOOLS`) también desaparecen, con sus combinaciones. El aviso «Conectado: mando» ya no se muestra, ni las indicaciones de botones bajo las novedades de la pantalla de inicio.

## 0.11.0 - 2026-11-19

### Added

- Continuar: la pantalla de inicio ofrece volver a donde se dejó el juego. El juego guarda un estado propio cuando su ventana pasa a segundo plano (menú de Steam) y al cerrarse; Jugar sigue empezando desde el título. Un estado también se carga desde cualquier zona del juego.
- La página de progreso del instalador muestra consejos ilustrados mientras se compila, en el idioma de la ventana, uno cada 9 segundos.
- Una transición de carga tras Jugar: la pantalla de inicio se desvanece en un torrente de estrellas, y una pantalla de carga se queda hasta que el juego dibuja.
- Estados guardados: cinco ranuras que guardan todo el juego en un momento y se cargan al instante, en la misma sesión o tras cerrar el juego. F2 abre las ranuras donde guardar y F3 las de donde cargar; cada una tiene una imagen y la hora, y una ranura usada pide confirmación antes de sobrescribirse. La nueva página Estados guardados del menú ofrece lo mismo con el mando. Unos 10 MB por estado. No durante un vídeo.
- Añadir el juego a Steam también configura los botones traseros del Deck: L4 abre el menú (F1), L5 pausa (F5), R4 guarda un estado (F2) y R5 carga uno (F3). Una configuración que ya hiciste se respeta.

### Changed

- En el menú, la línea que dice lo que necesita una opción (un disco, un reinicio) ya no tapa la descripción: va debajo.
- Las indicaciones sobre los discos japonés y europeo y sobre la actualización ya no nombran los archivos `.bat` de la versión de Windows: dicen que se añada el disco en el instalador y que se use Actualizar en la pantalla de inicio.
- El juego se pausa por defecto cuando su ventana deja de estar al frente (menú de Steam, otra ventana): Interfaz > Pausar en segundo plano, antes desactivada. Un archivo de ajustes anterior conserva su elección.
- La línea de ayuda bajo una opción ocupa todo el ancho de la ventana en lugar de una columna estrecha junto al nombre.
- El menú ya no tiene barra arriba: todo está en la ventana central, con una nueva página Estados guardados, una página Acciones (pausa, avance rápido, silencio, capturas, pantalla completa, estadísticas del renderizado) y una página Ayuda (atajos, página del proyecto, informar de un problema, Acerca de), todas accesibles con el mando. F1 abre esa ventana, y Y ya no lleva a una barra.
- La pausa está ahora en F5 (antes F2), para dejar sitio a las teclas de los estados guardados.
- La pantalla de inicio y los ajustes se rediseñan: paneles planos y oscuros con borde fino, una píldora blanca bajo la entrada elegida, deslizadores finos, interruptores suaves y un único acento ámbar, en lugar del doble marco blanco, las placas doradas y el cursor triangular. La pantalla de inicio pierde sus chispas y estelas animadas y conserva la imagen, el logo y las notas.
- La actualización de la pantalla de inicio ya no es un botón junto a Jugar, Opciones y Salir: está arriba en el panel «Novedades», una tarjeta con la nueva versión, el progreso y los errores, una línea discreta cuando no hay nada que hacer, y un punto dorado junto al título mientras espera una actualización. Con el mando, Derecha abre el panel y A inicia la actualización.

### Removed

- El perfilador (`RG_PROFILE`) y la vigilancia de hardware (`RG_HWWATCH`): solo funcionaban en Windows y no hacían nada en el Deck.
- El contador de fotogramas del menú (Ver > Fotogramas, F3, y su ajuste): el Steam Deck tiene el suyo.

## 0.10.0 - 2026-11-18

### Added

- El instalador, la pantalla de inicio, los ajustes, el cuadro de salida y los mensajes de actualización están en inglés, francés, alemán, español e italiano. El idioma es el de Steam (si no, el del sistema) hasta que elijas uno: arriba a la derecha del instalador, o Ajustes > Interfaz > Idioma del menú en el juego. Las notas de las tres últimas versiones también están traducidas (`docs/changelog/`).
