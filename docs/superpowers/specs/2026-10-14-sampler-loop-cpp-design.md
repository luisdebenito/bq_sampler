# Sampler de loops por teclado — diseño

Fecha: 2026-10-14
Estado: aprobado en brainstorming, pendiente de plan de implementación

## 1. Objetivo

Un programa de consola en C++ para **tocar en directo**. Carga una carpeta de
samples WAV en RAM al arrancar y permite pasar de uno a otro con el teclado
mientras suena, sin huecos ni chasquidos. En pantalla solo hay un número: el
índice del sample seleccionado.

El criterio que manda sobre cualquier otro: en escena el programa tiene que
arrancar siempre y el audio no se puede interrumpir nunca. Antes que cualquier
funcionalidad extra va la previsibilidad.

## 2. Comportamiento

Tres teclas de uso, todas reconfigurables: anterior, siguiente y play/stop.
Más una cuarta para salir del programa.

**Parado.** Las flechas mueven el índice en silencio. La pantalla muestra el
número fijo, sin parpadear.

**Play (barra espaciadora).** Arranca el sample del índice actual desde el frame
0 y lo repite en bucle indefinidamente. La pantalla pasa a parpadear.

**Flechas mientras suena.** No cortan el audio. Cada pulsación mueve el índice
*armado*, de forma acumulativa: dos derechas seguidas avanzan dos posiciones. La
pantalla muestra el armado inmediatamente, aunque siga sonando otro. Cuando el
sample en curso llega al final de su vuelta, el armado toma el relevo y empieza
desde su frame 0. Si al acabar la vuelta el armado coincide con el que ya suena,
simplemente vuelve a empezar.

**Stop (barra espaciadora de nuevo).** Para y resetea la posición a 0. El
siguiente play arranca desde el principio. La pantalla deja de parpadear.

El índice es circular: desde el último, siguiente lleva al primero.

Los samples pueden tener duraciones distintas entre sí. El punto de
cuantización es el final del archivo que esté sonando, no una rejilla de
compases compartida.

### Qué significa "sin latencia" aquí

Dos cosas distintas que conviene no mezclar:

- La pulsación se registra en el instante en que ocurre y el audio nunca se
  interrumpe. Esto sí se cumple.
- El sample no cambia en el acto: cambia al final de la vuelta. Es una decisión
  deliberada, no una limitación.

Queda un retardo irreducible entre pulsar play y oír la primera muestra: el
audio no reacciona hasta el siguiente callback del dispositivo, unos 5 ms con
un buffer de 256 frames. Está por debajo del umbral perceptible.

## 3. Decisiones de stack

**miniaudio (header-only) para el audio.** Un único `.h` que vive en el repo.
Sin brew, sin cmake, sin pkg-config. Usa CoreAudio en macOS y detecta entre
PipeWire, PulseAudio, ALSA o JACK en Linux, con el mismo código. Trae el
decodificador de WAV incluido, así que no hay parser propio que mantener.

Descartados: CoreAudio nativo, porque ata a macOS y habría que tirar el
adaptador entero al pasar a Linux. PortAudio más libsndfile, porque son
dependencias instaladas en el sistema y una librería dinámica que se rompe tras
una actualización es justo el fallo que no se puede tener en directo.

El coste de miniaudio es el tiempo de compilación de un header de ~90k líneas.
Se aísla en su propia unidad de traducción y deja de notarse.

**termios y ANSI para la interfaz.** POSIX puro, idéntico en macOS y Linux.

**WAV únicamente.** Si algún día aparece otro formato, se convierte antes de
meterlo en la carpeta.

## 4. Portabilidad

El objetivo son macOS y Linux con el mismo código fuente. Lo único específico de
cada sistema son los flags de enlazado, que resuelve un `uname -s` en el
Makefile: frameworks de CoreAudio en macOS, `-lpthread -lm -ldl` en Linux.

En Linux, el backend de audio se puede forzar por variable de entorno sin
recompilar. Importa si esa máquina acaba siendo la de directo: PulseAudio añade
algunos milisegundos y ALSA o JACK van más finos.

Windows queda fuera. El adaptador de audio valdría tal cual, pero `termios` no
existe ahí y habría que reescribir el de teclado.

## 5. Arquitectura

Hexagonal. El dominio no incluye ni una cabecera del sistema.

```
src/
  domain/
    sample.h           Sample, SampleLibrary
    transport.h/.cpp   máquina de estados + render (RT-safe, sin dependencias)
    action.h           enum Action { Prev, Next, Toggle, Quit }
  ports/
    audio_device.h
    sample_repository.h
    input_source.h
    display.h
  adapters/
    miniaudio_device.h/.cpp
    wav_folder_repository.h/.cpp
    termios_keyboard.h/.cpp
    ansi_display.h/.cpp
  config.h/.cpp        keymap y opciones
  main.cpp             composición
third_party/
  miniaudio.h
  doctest.h
tests/
Makefile
```

La pieza central es:

```cpp
void Transport::render(float* out, uint32_t frames, const SampleLibrary& lib);
```

Le pasas un buffer y te lo llena. Es código puro, sin dependencias. Eso permite
verificar el comportamiento completo del sampler renderizando a un array en
memoria, sin abrir ningún dispositivo y sin que suene nada. Ese es el motivo
principal de separar dominio y adaptadores aquí, por encima de la portabilidad.

## 6. Concurrencia

Dos hilos y ningún lock.

El hilo de audio lo crea miniaudio, corre con prioridad de tiempo real y lo
único que hace es llenar el buffer de salida. El hilo principal lee el teclado y
pinta la terminal.

Toda la comunicación entre ambos cabe en dos variables atómicas, y va en un
único sentido:

```cpp
std::atomic<int>  armedIndex;     // escribe UI, lee audio
std::atomic<bool> playRequested;  // escribe UI, lee audio
```

El hilo de audio es dueño exclusivo de `cur` (índice sonando), `pos` (frame
actual), `prevPlay` y el estado de la rampa. No los ve nadie más. La pantalla
pinta `armedIndex`, que escribe ella misma.

Como el número que se muestra es el armado y no el que suena, el audio nunca
necesita informar a la UI. Por eso no hace falta ring buffer ni cola de
mensajes. Es la consecuencia directa de haber elegido el cambio cuantizado.

`SampleLibrary` se carga entera antes de arrancar el dispositivo y después es de
solo lectura, así que el hilo de audio la lee sin sincronizar nada.

### Reglas del hilo de audio

Dentro de `render` no se puede: reservar ni liberar memoria, tomar locks, tocar
disco o red, lanzar excepciones, ni llamar a nada de la librería estándar que
pueda hacer cualquiera de esas cosas. Todo lo que necesita está preasignado.

## 7. El dominio: Transport

```
render(out, nframes, lib):
    play = playRequested.load(acquire)

    si play y no prevPlay:            // flanco de arranque
        cur = armedIndex.load(acquire)
        pos = 0
        rampa de entrada
    si no play y prevPlay:            // flanco de parada
        rampa de salida
    prevPlay = play

    si parado y la rampa de salida ya terminó:
        pos = 0
        escribir silencio y salir

    por cada frame i:
        si pos == lib[cur].frameCount:        // fin de vuelta
            nuevo = armedIndex.load(acquire)
            si nuevo != cur:
                cur = nuevo
                rampa de entrada
            pos = 0
        out[i] = lib[cur][pos] * gain
        pos += 1
        avanzar gain según la rampa
```

Toda la cuantización vive en el bloque `si pos == frameCount`. No hay
temporizadores ni relojes: el final de la vuelta es el final del buffer, contado
en frames, que es la referencia más exacta disponible.

El lado de la UI, al pulsar una flecha:

```cpp
armedIndex = ((armedIndex + delta) % n + n) % n;
```

Eso da el circular y la acumulación a la vez. Como solo hay un escritor, basta
con cargar y guardar.

### Rampas anti-click

Tres situaciones, tres tratos distintos:

- **Arranque y parada:** rampa de 2 ms. Obligatoria, porque la onda se corta por
  donde toque.
- **Cambio de sample A a B:** rampa de 2 ms.
- **El loop se repite sobre sí mismo:** no se toca nada. Un loop bien cortado
  está hecho para encadenar consigo mismo, y meterle una rampa le provocaría un
  bache de volumen en cada vuelta. Si un loop chasquea al repetirse, el problema
  está en el recorte del archivo.

La duración de la rampa es configurable.

### Invariantes

- `0 <= cur < n` y `0 <= armedIndex < n` en todo momento.
- `0 <= pos < lib[cur].frameCount` al salir de cada iteración.
- Parado implica salida en silencio una vez terminada la rampa, y `pos == 0`.
- `cur` solo cambia en un límite de vuelta o en un flanco de arranque. Nunca en
  mitad de un sample.

## 8. Carga de samples

Al arrancar se lee la carpeta indicada por argumento, se filtran los `.wav` y se
ordenan **alfabéticamente por nombre de fichero**. El orden alfabético es lo
único que garantiza que el índice 7 de hoy sea el mismo que el de ayer. La
convención es nombrarlos con ceros delante: `01_`, `02_`, `10_`.

Cada archivo se decodifica a **float32, estéreo, a la frecuencia del
dispositivo**. La conversión la hace el decoder de miniaudio al vuelo, así que
se pueden mezclar mono y estéreo, 44.1 y 48 kHz en la misma carpeta: en RAM todo
queda homogéneo y el render se reduce a copiar y escalar.

Consumo aproximado: 23 MB por minuto de audio. Cincuenta loops de cuatro
segundos son unos 76 MB.

## 9. Entrada y configuración

Fichero `sampler.conf` opcional, de `accion = tecla`:

```
prev   = left
next   = right
toggle = space
quit   = q
```

Las flechas se reconocen en sus dos secuencias posibles, `ESC [ C` y `ESC O C`,
porque los terminales difieren según estén en modo aplicación o no.

El hilo principal usa `poll()` sobre stdin con timeout de 50 ms. Al pulsar,
`poll()` despierta en ese instante y la acción se aplica sin esperar a ningún
tick. El timeout solo sirve para alternar la fase del parpadeo cuando no llega
nada.

## 10. Pantalla

La terminal entra en pantalla alternativa (`ESC [ ? 1049 h`) para no ensuciar el
scrollback, y se oculta el cursor. En el centro, el número del índice armado.

Parpadeo a 2 Hz mientras suena, fijo cuando está parado. La frecuencia es
configurable.

## 11. Errores y robustez

**Al cargar.** Si algún WAV falla, no se aborta ni se salta en silencio: se
imprime la lista de los que han fallado junto con los índices definitivos del
resto y se espera a que el usuario pulse una tecla. En directo, enterarse antes
de empezar es lo que importa.

**Carpeta vacía o sin WAVs.** Error claro y salida, antes de abrir el
dispositivo.

**Dispositivo de audio no disponible.** Error claro y salida.

**Señales.** Un handler de `SIGINT` y `SIGTERM` restaura `termios` y la pantalla
antes de salir. Una terminal que se queda en modo raw y sin eco tras un Ctrl-C
es insufrible, y pasa justo cuando algo ya ha ido mal.

## 12. Tests

Con doctest, header-only en el repo. Todos ejercitan `Transport::render` contra
arrays en memoria, sin dispositivo de audio.

Casos que importan:

- Parado, una vez terminada la rampa de salida, no produce ni una muestra
  distinta de cero.
- Navegar estando parado no produce audio.
- Play arranca en el frame 0 del índice actual.
- El loop se repite: tras `frameCount` frames, vuelve al principio del mismo.
- El cambio armado entra en el frame exacto en que acaba el anterior, ni uno
  antes ni uno después.
- Dos pulsaciones de siguiente saltan dos posiciones.
- El índice envuelve por ambos extremos.
- Stop resetea la posición, y el siguiente play arranca en 0.
- Armar el índice que ya suena no interrumpe nada: solo reinicia la vuelta.
- La rampa se aplica en cambio de sample y no se aplica en repetición del mismo.

## 13. Build

`Makefile` con detección por `uname -s`. miniaudio se compila en su propia
unidad de traducción para que no penalice cada recompilación. Objetivos:
`make`, `make test`, `make clean`.

## 14. Fuera de alcance

No entra en esta versión: múltiples pistas simultáneas, efectos, control MIDI,
grabación, waveform en pantalla, cuantización por rejilla de compases, cambio de
carpeta en caliente, y Windows.

Dos cosas que serían baratas de añadir después si hacen falta: publicar también
el índice que suena realmente (un `atomic` más, para mostrar "suena 3, armado
5"), y un modo retrigger que cambie en el acto en vez de esperar al final.
