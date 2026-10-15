# Entrada MIDI y asistente de primer arranque: diseño

Fecha: 2026-10-15
Estado: aprobado en brainstorming, pendiente de plan de implementación
Parte de: `2026-10-14-sampler-loop-cpp-design.md`

## 1. Objetivo

Poder tocar el sampler con un controlador MIDI además de con el teclado. La
configuración se hace sin editar ficheros: si al arrancar no existe el config,
un asistente pregunta el método de entrada, detecta el controlador y aprende las
cuatro acciones pulsándolas.

Sigue mandando la regla del spec original: en escena el programa tiene que
arrancar siempre y el audio no se puede interrumpir nunca. Ningún fallo de MIDI
impide arrancar ni corta el audio.

## 2. Comportamiento

### Primer arranque (no existe el config)

El config es el mismo `sampler.conf` de siempre, en la ruta del segundo
argumento o en la carpeta actual. El asistente solo se lanza si ese fichero no
existe. Para repetirlo, se borra el fichero.

1. Pregunta `¿Entrada? [t] teclado / [m] MIDI`.
2. Si se elige MIDI y no hay ningún puerto MIDI de entrada, lo dice y vuelve al
   paso 1.
3. Pide las cuatro acciones en este orden: SIGUIENTE, ANTERIOR, PLAY/STOP, SALIR.
4. Guarda el fichero, dice la ruta y continúa con el arranque normal usando lo
   que acaba de aprender.

Ctrl-C en cualquier punto del asistente sale sin escribir nada y deja la
terminal restaurada.

### Aprender en modo teclado

Se acepta cualquier tecla que `keyNameToBytes` sepa nombrar: flechas (en sus dos
secuencias), espacio, intro, escape y caracteres imprimibles. Lo demás (F1,
Re Pág, combinaciones con Ctrl) se rechaza con `tecla no soportada, prueba otra`.

### Aprender en modo MIDI

El asistente escucha todos los puertos de entrada a la vez. El puerto desde el
que llega la pulsación de SIGUIENTE queda elegido, y a partir de ahí solo se
aceptan mensajes de ese puerto.

Cuenta como pulsación:

- Note On con velocidad mayor que 0.
- Control Change con valor mayor que 0.

Se ignoran en silencio: Note Off, Note On con velocidad 0 (es como muchos
aparatos mandan el soltar), Control Change con valor 0, y cualquier otro tipo de
mensaje. RtMidi ya descarta por defecto sysex, reloj y active sensing.

### Repetidos

En los dos modos, una pulsación que ya está asignada a otra acción se rechaza con
`ya está usado para SIGUIENTE` y se vuelve a pedir la misma acción.

### Arranques siguientes

- `input = keyboard` o sin línea `input`: exactamente como hoy. Los configs que
  ya existen siguen funcionando sin cambios.
- `input = midi`: se abre el puerto guardado y el teclado sigue activo como
  respaldo, con las teclas por defecto o las que haya en el fichero.

Si el puerto guardado no aparece, o RtMidi falla al abrirlo, se avisa igual que
cuando falla un WAV, antes de entrar en pantalla completa:

```
no encuentro el MIDI "nanoKONTROL2"
pulsa intro para seguir solo con teclado, ctrl-c para salir
```

No hay reconexión automática. Si se enchufa después, hay que reiniciar.

## 3. Formato del config

Mismo formato `clave = valor` de siempre, con claves nuevas:

```
input       = midi
midi_port   = nanoKONTROL2
midi_next   = cc:1:44
midi_prev   = cc:1:43
midi_toggle = note:1:60
midi_quit   = cc:1:46
ramp_ms     = 2
blink_ms    = 250
```

- `input`: `keyboard` o `midi`. Por defecto, `keyboard`.
- `midi_port`: nombre del puerto tal como lo da RtMidi, sin el sufijo de ALSA
  (ver sección 6).
- `midi_<acción>`: `tipo:canal:número`. Tipo `note` o `cc`, canal de 1 a 16,
  número de 0 a 127. Fuera de rango, la línea se ignora.

En modo teclado el asistente escribe `input = keyboard` y las líneas `next`,
`prev`, `toggle` y `quit` de siempre. En los dos modos escribe también `ramp_ms`
y `blink_ms` con sus valores por defecto, para que el fichero sirva de
referencia.

Una acción sin binding MIDI (por ejemplo, falta `midi_quit`) solo funciona por
teclado. Una línea mal escrita se ignora, como hasta ahora.

## 4. Decisiones de stack

**RtMidi 6.0.0.** Dos ficheros (`RtMidi.h`, `RtMidi.cpp`) en `third_party/`.
Usa CoreMIDI en macOS y el secuenciador de ALSA en Linux, con el mismo código.
Se compila en su propia unidad de traducción con `-w`, igual que miniaudio.

Esto es una **excepción consciente** a la regla de "cero dependencias instaladas
en el sistema" del spec original: en Linux hace falta `libasound2-dev` para
compilar. En macOS no cambia nada, porque CoreMIDI viene con el sistema.

Descartado: escribir CoreMIDI a mano en macOS y leer `/dev/snd/midiC*D*` en
Linux. Evita la dependencia, pero son dos adaptadores distintos con un parser de
MIDI propio (running status incluido), y en Linux solo verían aparatos USB
físicos.

## 5. Arquitectura

El puerto `InputSource` no cambia: `main` sigue pidiendo `poll(timeout, acción)`
a una única fuente.

```
src/
  config.h/.cpp            + InputMode, MidiBinding, saveConfig y funciones puras
  setup_wizard.h/.cpp      asistente: SetupLearner (puro) + bucle interactivo
  adapters/
    fd_input.h             interfaz FdInput { fd(); readAction(); }
    termios_keyboard.*     pasa de InputSource a FdInput
    rtmidi_input.h/.cpp    FdInput sobre un puerto MIDI, con self-pipe
    midi_scanner.h/.cpp    escucha todos los puertos, para el asistente
    input_mux.h/.cpp       InputSource sobre varias FdInput, un solo poll()
third_party/
  RtMidi.h
  RtMidi.cpp
```

### Tipos nuevos en config

```cpp
enum class InputMode { Keyboard, Midi };
enum class MidiKind { Note, Cc };

struct MidiBinding {
    MidiKind kind;
    uint8_t channel;  // 1..16
    uint8_t number;   // 0..127
};

struct Config {
    InputMode input = InputMode::Keyboard;
    std::string midiPort;
    std::map<Action, KeyBinding> keys;
    std::map<Action, MidiBinding> midi;
    uint32_t rampMs = kDefaultRampMs;
    uint32_t blinkMs = kDefaultBlinkMs;
};
```

### Funciones puras

- `midiBindingFromMessage(bytes, out)`: convierte un mensaje crudo en binding, o
  lo rechaza si no cuenta como pulsación (sección 2).
- `matchMidi(config, bytes, out)`: el equivalente MIDI de `matchAction`. Canal,
  tipo y número tienen que coincidir.
- `keyNameFromBytes(bytes, out)`: la inversa de `keyNameToBytes`, para que el
  asistente guarde `left` y no bytes.
- `samePort(saved, actual)`: compara nombres de puerto ignorando el sufijo de
  ALSA.
- `saveConfig(path, config)`: escribe el fichero. Lo que escribe, `loadConfig` lo
  lee igual.

### El asistente

`SetupLearner` es una pieza pura que recibe eventos (puerto de origen y bytes) y
decide: aceptado, repetido, no soportado, de otro aparato o ignorado. Lleva el
orden de las acciones y el aparato fijado. Al terminar entrega el `Config`.

Alrededor va un bucle fino que pinta las preguntas, lee del teclado o de
`MidiScanner` y le pasa los eventos. Ese bucle no tiene test, igual que `main`.

`MidiScanner` abre un `RtMidiIn` por puerto y los consulta con `getMessage()`
cada pocos milisegundos. En el asistente la latencia no importa, así que no
necesita self-pipe.

## 6. Nombres de puerto en Linux

Con ALSA, RtMidi devuelve nombres como `nanoKONTROL2:nanoKONTROL2 MIDI 1 20:0`.
El `20:0` final son los números de cliente y puerto del secuenciador, y cambian
cada vez que se conecta el aparato. Si se guardara tal cual, el segundo arranque
ya no lo encontraría.

`samePort` quita un sufijo final de la forma ` <dígitos>:<dígitos>` antes de
comparar. `saveConfig` guarda el nombre ya sin sufijo. En macOS los nombres no
llevan ese sufijo, así que la comparación es exacta.

## 7. Concurrencia

RtMidi entrega los mensajes en su propio hilo. Llamar a `transport.step()` desde
ahí rompería la regla del spec original (sección 6): `armedIndex` tiene un único
escritor, el hilo principal. Con dos escritores, una pulsación de teclado y otra
de MIDI casi simultáneas podrían pisarse.

Por eso `RtMidiInput` usa un **self-pipe**:

1. El callback de RtMidi traduce el mensaje con `matchMidi`.
2. Si hay acción, escribe un byte en una tubería interna. El extremo de escritura
   es no bloqueante: si la tubería estuviera llena, el byte se descarta antes que
   bloquear el hilo de MIDI.
3. `InputMux` hace un único `poll()` sobre stdin y el extremo de lectura de esa
   tubería, y llama a `readAction` del que esté listo.

Consecuencias:

- Solo el hilo principal toca `Transport`. Las reglas de concurrencia del spec
  original quedan intactas.
- Al pulsar, `poll()` despierta en el acto. Play/stop entra sin esperar al
  timeout de 50 ms.
- El `Config` es de solo lectura una vez arrancado, así que el callback lo lee sin
  sincronizar nada.

## 8. Errores

| Situación | Qué pasa |
|---|---|
| RtMidi lanza una excepción (al abrir, al listar) | Se captura dentro del adaptador y se trata como "no hay MIDI". Ninguna excepción sale de los adaptadores. |
| El puerto guardado no está al arrancar | Aviso y espera a intro. Sigue solo con teclado. |
| Se desenchufa el controlador en directo | Deja de llegar MIDI. El audio sigue y el teclado funciona. |
| El asistente no puede escribir el fichero | Avisa con la ruta y arranca con lo aprendido, sin guardarlo. |
| Ningún puerto MIDI en el asistente | Lo dice y vuelve a preguntar el método. |
| Ctrl-C en el asistente | Sale sin escribir nada, terminal restaurada. |
| No hay config y stdin no es un terminal | Error claro y salida, como hoy. El asistente necesita a alguien delante. |

## 9. Tests

Con doctest y sin hardware, como el resto.

- `midiBindingFromMessage`: Note On con velocidad cuenta; Note On con velocidad 0
  y Note Off no; CC con valor 0 no; el canal sale bien (canal 1 y canal 16).
- `matchMidi`: la misma nota en otro canal no dispara; una nota no confunde a un
  CC con el mismo número.
- `keyNameFromBytes`: las flechas en sus dos secuencias, espacio y letras se
  reconocen; F1 se rechaza; ida y vuelta con `keyNameToBytes`.
- `samePort`: `nano 20:0` y `nano 24:0` son el mismo puerto; dos nombres
  distintos no; un nombre que acaba en dígitos sin `:` no se recorta.
- `saveConfig` y `loadConfig`: ida y vuelta en los dos modos; canal 17 o número
  200 se ignoran; un config sin línea `input` carga en modo teclado.
- `SetupLearner`: el orden de las acciones; un repetido se rechaza y se vuelve a
  pedir la misma acción; en MIDI, tras fijar el aparato, se ignoran los mensajes
  de otro puerto; los mensajes de soltar no avanzan.
- `InputMux`: con dos fuentes falsas sobre tuberías, despierta por cualquiera de
  las dos y devuelve la acción correcta.

## 10. Build

- `RtMidi.cpp` en su propia unidad con `-w` y la macro del sistema:
  `__MACOSX_CORE__` en macOS, `__LINUX_ALSA__` en Linux.
- Enlazado: `-framework CoreMIDI` en macOS, `-lasound` en Linux.
- README: cómo funciona el asistente y cómo repetirlo. La línea de
  `libasound2-dev` ya está.
- `prod_checklist.md`: conectar el controlador antes de arrancar y comprobar que
  responde.

## 11. Fuera de alcance

Reconexión en caliente, varios controladores a la vez, encender luces del
controlador (MIDI de salida), usar la velocidad de la pulsación, un flag
`--setup` y Windows.
