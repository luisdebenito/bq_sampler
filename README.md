# sampler

Reproductor de loops por teclado o MIDI para tocar en directo. Carga una carpeta de
WAV en RAM y permite encadenarlos con tres teclas, cambiando siempre en el
límite de la vuelta para que el audio no se corte.

## Compilar

    make

Funciona en macOS y Linux. miniaudio y doctest viven en `third_party/`, así
que en macOS no hace falta instalar nada. En Linux, instala antes las cabeceras
de ALSA:

    sudo apt install libasound2-dev

## Usar

    ./build/sampler ./mis_samples [sampler.conf]

La primera vez, si no existe el config, el programa pregunta si vas a usar
teclado o MIDI y te pide pulsar lo que quieras para cada acción: siguiente,
anterior, play/stop y salir. Lo guarda en `sampler.conf`, o en la ruta que
pases. Para repetirlo, borra el fichero.

Con MIDI vale cualquier botón o pad que mande Note On o Control Change. Si un
botón está en modo toggle (manda 127 al pulsar y 0 al volver a pulsar), se
pierde una de cada dos pulsaciones: ponlo en modo momentáneo. El teclado sigue
funcionando con sus teclas por defecto como respaldo. Si al arrancar no
encuentra el controlador, avisa y sigue solo con teclado.

Con las teclas por defecto:

- **Flechas**: mueven el índice. Si está parado, en silencio. Si está sonando,
  arman el destino y el cambio entra al acabar la vuelta. Las pulsaciones se
  acumulan.
- **Espacio**: arranca desde el principio, o para y resetea.
- **q** o **Ctrl-C**: salir.

El número de la pantalla parpadea mientras suena y se queda fijo al parar.

Los samples se ordenan alfabéticamente por nombre, así que conviene nombrarlos
con ceros delante (`01_`, `02_`, `10_`) para que el índice sea estable.

## Tests

    make test
