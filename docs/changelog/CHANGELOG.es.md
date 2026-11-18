# Notas de versión (español)

Las tres últimas versiones de `CHANGELOG.md`, traducidas: la pantalla de
inicio las muestra en este idioma. Los títulos (`## versión - fecha`,
`### Added`) se quedan como en `CHANGELOG.md`: son los que unen una versión
con su traducción. Una versión que falte en este archivo se muestra en inglés.

## Unreleased

### Added

- Fork para Steam Deck: la pantalla de inicio, los ajustes (sus páginas,
  opciones y textos de ayuda), el cuadro de salida y los mensajes de
  actualización están en inglés, francés, alemán, español e italiano. El
  idioma es el de Steam (si no, el del sistema) hasta que elijas uno en
  Ajustes > Interfaz > Idioma del menú. Las notas de las tres últimas
  versiones también están traducidas (`docs/changelog/`); una versión sin
  traducción se muestra en inglés.

## 0.9.0 - 2026-11-18

### Changed

- Fork para Steam Deck: las notas de versión de la pantalla de inicio
  muestran cada fecha como «18 nov. 2026».

### Added

- Fork para Steam Deck: la pantalla de inicio tiene un botón Opciones, sobre
  Salir, que abre los ajustes encima de las notas de versión, sin la barra
  superior del menú.

### Removed

- Fork para Steam Deck: las opciones que no significan nada en la Deck
  desaparecen del menú y de settings.ini: el modo de ventana, la
  sincronización vertical, las resoluciones internas superiores a 2x, el
  filtro del vecino más próximo, los bordes, el escalado entero, Silenciar el
  sonido y Silenciar en segundo plano, el interruptor Teclado, el selector de
  mando y las asignaciones del teclado, las tres opciones de la cámara con el
  ratón, Botones en el texto del juego (siempre activo), el renderizador
  nativo, la página Texturas, Ocultar el cursor inactivo del ratón (siempre
  activo) y la posición del contador de fotogramas (arriba a la derecha).

## 0.8.0 - 2026-11-18

### Changed

- Fork para Steam Deck: la imagen es siempre 16:10, la pantalla de la Deck.
  Las opciones Relación de aspecto y Pantalla ancha desaparecen: el juego se
  dibuja ancho desde el principio, y los menús y las pantallas previas al
  juego (apertura, título, carga), dibujados para 4:3, se estiran a 16:10 en
  lugar de mostrarse con barras. Una ventana nueva también se abre en 16:10.
