# Checklist para arrancar en directo

## macOS

- [ ] Instala las herramientas de compilación si no las tienes: `xcode-select --install`.
- [ ] Compila: `make`.
- [ ] Comprueba que todo pasa: `make test`.
- [ ] Prepara la carpeta de samples: solo `.wav`, con ceros delante (`01_`, `02_`, `10_`) para que el índice sea estable.
- [ ] Si quieres otras teclas o tiempos: `cp sampler.conf.example sampler.conf` y edítalo. Se lee desde la carpeta donde lanzas el programa, o pásalo como segundo argumento.
- [ ] Elige la salida de audio en Ajustes del Sistema > Sonido. El programa usa la salida por defecto.
- [ ] Si usas MIDI, conecta el controlador antes de arrancar. No se reconecta en caliente.
- [ ] Si es la primera vez en esta máquina, el asistente te pedirá las cinco acciones. Para repetirlo, borra `sampler.conf`.
- [ ] Arranca desde una ventana de terminal: `./build/sampler ./mis_samples`.
- [ ] Si avisa de WAV que no se pudieron cargar, revisa la lista de índices y pulsa intro para seguir o Ctrl-C para salir.
- [ ] Pulsa espacio y confirma que suena y que el número parpadea.
- [ ] Si usas MIDI, pulsa SIGUIENTE en el controlador y confirma que cambia el número.

## Linux

- [ ] Instala el compilador y make: `sudo apt install build-essential`.
- [ ] Instala las cabeceras de ALSA: `sudo apt install libasound2-dev`.
- [ ] Compila: `make`.
- [ ] Comprueba que todo pasa: `make test`.
- [ ] Prepara la carpeta de samples: solo `.wav`, con ceros delante (`01_`, `02_`, `10_`) para que el índice sea estable.
- [ ] Si quieres otras teclas o tiempos: `cp sampler.conf.example sampler.conf` y edítalo. Se lee desde la carpeta donde lanzas el programa, o pásalo como segundo argumento.
- [ ] Elige la salida de audio por defecto del sistema. Si no suena o hay cortes, fuerza el backend: `SAMPLER_AUDIO_BACKEND=alsa ./build/sampler ./mis_samples` (también vale `pulseaudio` o `jack`).
- [ ] Si usas MIDI, conecta el controlador antes de arrancar. No se reconecta en caliente.
- [ ] Si es la primera vez en esta máquina, el asistente te pedirá las cinco acciones. Para repetirlo, borra `sampler.conf`.
- [ ] Arranca desde una ventana de terminal: `./build/sampler ./mis_samples`.
- [ ] Si avisa de WAV que no se pudieron cargar, revisa la lista de índices y pulsa intro para seguir o Ctrl-C para salir.
- [ ] Pulsa espacio y confirma que suena y que el número parpadea.
- [ ] Si usas MIDI, pulsa SIGUIENTE en el controlador y confirma que cambia el número.
