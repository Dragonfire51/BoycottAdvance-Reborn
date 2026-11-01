# Evaluación del port a Android

Documento de viabilidad del port de BoycottAdvance Reborn a Android. No es
una guía de port terminada: es un análisis del código actual (v1.4.26),
del trabajo que falta y de los riesgos, para decidir si se aborda.

## 1. Resumen

El port es **viable y de coste moderado**. La razón es que el proyecto ya
está separado en dos capas con una frontera limpia:

- Un **núcleo de emulación** en C99 puro, sin dependencias de plataforma.
- Una **capa de presentación** por plataforma: SDL2 (Linux/macOS), Win32
  nativo (GDI/Direct3D9/WinMM), y una composición de UI en software
  (`fb.c`) que ya es independiente del backend de vídeo.

Android es, desde el punto de vista del código, una plataforma más: se
añade un frontend (Android NDK + Java/Kotlin de arranque) que use el mismo
núcleo y un backend de vídeo/audio nuevo. No hay que reescribir el
emulador.

## 2. Estado actual del código (evidencia)

| Componente | Archivos | Dependencias de plataforma |
|---|---|---|
| Núcleo (CPU, PPU, APU, memoria, BIOS, saves) | `arm.c` `mem.c` `ppu.c` `apu.c` `bios.c` `gba.c` `eeprom.c` `flash.c` | **Ninguna**: solo `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<string.h>`, `<stdlib.h>`, `<stdio.h>`, `<math.h>`, `<stdatomic.h>` |
| Framebuffer/UI en software | `fb.c` `font8x8.h` | **Ninguna** (compone ARGB8888 en memoria) |
| Salida headless (PNG+WAV) | `export.c` | **Ninguna** (PNG con deflate sin zlib, WAV PCM) |
| Presentación de vídeo | `video.c` `video.h` | SDL2 (no-Windows) o GDI/D3D9 (Windows) |
| Frontend de escritorio | `main.c` (SDL2), `win32.c`+`gui.c` (Win32) | SDL2 / Win32 |
| Utilidades de tiempo | `get_ticks_us`/`sleep_ms` | implementadas en el frontend (`win32.c`), no en el núcleo |

Puntos concretos verificados:

- El núcleo no incluye ninguna cabecera de sistema operativo
  (`windows.h`, `<unistd.h>`, `<sys/*>`, SDL): la comprobación
  `grep -l 'windows.h\|SDL\|unistd.h\|sys/'` sobre los ficheros del núcleo
  solo devuelve `apu.c`, y por `<stdatomic.h>` (estándar C11, disponible en
  el NDK), no por nada específico de plataforma.
- El único uso no estándar-C89 son `__builtin_popcount` / `__builtin_clz`
  (`arm.c`, `bios.c`), que **Clang/NDK soportan** sin cambios.
- El único sitio con riesgo de anchura de tipo es `long sz = ftell(f)` en
  `gba.c:159` (para el tamaño de la ROM); en Android (LP64) `long` es de 64
  bits, así que es correcto, pero conviene migrarlo a un tipo explícito
  (`long`/`off_t`) por claridad.
- El estado global del núcleo está encapsulado en la estructura `GBA`
  (asignada con `calloc`, ~34 MB), más un puñado de `static` de fichero
  (contadores, tablas de CRC, flags de depuración). Para una app de un solo
  emulador es aceptable; para múltiples instancias simultáneas habría que
  revisarlos.
- La UI de escritorio (menús, diálogos) vive en `fb.c` (software) y en el
  frontend. En Android no se reutiliza tal cual: Android quiere UI nativa
  (botones táctiles, menú de la app), no una barra de menús dibujada.

## 3. Trabajo necesario para el port

1. **Proyecto Android (NDK)**. Añadir un árbol `android/` (Gradle) con:
   - `CMakeLists.txt` que compile el núcleo + `fb.c` como biblioteca
     (`libmain.so`); sin SDL, sin `main.c`/`win32.c`/`gui.c`/`video.c`.
   - Módulo Java/Kotlin: `Activity`, ciclo de vida, permiso de
     almacenamiento, selector de ROM, gestión de `.sav`.

2. **Backend de vídeo**. Lo más barato y de mejor relación esfuerzo/calidad
   en Android moderno:
   - **Opción A (recomendada)**: renderizar el ARGB8888 de `fb.c` a una
     `SurfaceView`/`TextureView` con OpenGL ES 2.0 (una textura + un quad).
     Es ~150-250 líneas, equivalente al backend SDL de `video.c`.
   - **Opción B**: `ANativeWindow` + `ANativeWindow_lock`/`unlockAndPost`
     (blit directo), aún más simple, sin GL.
   La UI táctil (botones A/B/L/R, D-pad, START/SELECT, pausa, ajustes) se
   dibuja en el propio `fb.c` o con `View`s de Android superpuestos.

3. **Backend de audio**. El APU ya entrega un flujo estéreo 44.1 kHz por el
   ring buffer (`apu_mix`/`apu_drain`). En Android:
   - **AAudio** (API 26+) o **OpenSL ES** (más antiguo), en modo callback,
     consumiendo `apu_mix` igual que hace SDL en el frontend de escritorio.
   - Alternativa: SDL2 también existe para Android (`SDL_Android`), lo que
     permitiría reutilizar `main.c` casi entero; ver §4.

4. **Entrada**. Sustituir teclado/joystick por toque + mandos Bluetooth
   (`InputDevice`, o el subsistema joystick de SDL si se usa SDL2). El
   núcleo ya expone `gba_update_keypad(g, mask)` con máscara activa en bajo.

5. **Almacenamiento / rutas**. El núcleo resuelve ficheros con rutas
   relativas y `<stdio.h>` (`fopen`). En Android hay que pasar rutas
   absolutas (p. ej. bajo `getFilesDir()` o SAF → copia a caché) y
   sustituir la resolución de `gba_bios.bin` "junto al ejecutable" por un
   recurso empaquetado en `assets/` extraído a fichero en el primer
   arranque. El BIOS no se incluye por defecto; la app puede arrancar con
   el stub fast-boot o pedir el dump al usuario.

6. **Ciclo de vida**. Pausar/reanudar emulación y audio en `onPause`/
   `onResume`; guardado de batería (`.sav`) al salir.

## 4. Atajo: usar SDL2 para Android

El proyecto ya tiene un frontend SDL2 completo (`main.c`, 958 líneas) que
hace exactamente lo que Android necesita: ventana, audio, entrada,
redimensionado. SDL2 tiene port oficial a Android. Reutilizándolo:

- `main.c` + `fb.c` + núcleo compilan con el NDK casi tal cual.
- Se añade un pequeño `android-project` estándar de SDL
  (`SDLActivity`, `AndroidManifest.xml`, `build.gradle`) y se empaqueta
  `main.c` como `libmain.so` (el patrón habitual de SDL en Android).
- La barra de menús dibujada en `fb.c` se puede conservar o sustituir por
  UI táctil.

Ventaja: **es el camino más corto** y reutiliza código ya probado. Coste:
depender de SDL2 en Android (una dependencia grande, pero madura) y
adaptar el frontend de teclado a táctil.

La opción sin SDL (punto 2) da una app más pequeña y "nativa", a cambio de
escribir el backend de vídeo/audio a mano.

## 5. Riesgos y consideraciones

- **Rendimiento**: el emulador es un intérprete de CPU (1 ciclo/instrucción),
  ligero. En un ARM moderno sobra potencia; en dispositivos antiguos de
  32 bits habría que medir. El render del PPU es software (`ppu.c`), ya
  probado en escritorio.
- **Precisión de audio**: el ring buffer del APU usa `stdatomic` y un
  productor/consumidor; hay que respetar el mismo modelo en el callback de
  AAudio/OpenSL (no bloquear en el callback).
- **Tamaño de la ROM en memoria**: el núcleo carga la ROM completa en RAM
  (`calloc` de `rom_size`); ROMs de 16-32 MB son normales, aceptable en
  Android actual.
- **BIOS**: por motivos legales no se distribuye; la app debe funcionar sin
  él (fast-boot) o pedirlo al usuario, igual que en escritorio.
- **Mantenimiento**: un tercer frontend multiplica la superficie a
  mantener. Si se elige SDL2, el coste de mantenimiento es bajo porque
  comparte `main.c`; si se elige nativo, hay que mantener dos backends de
  vídeo/audio además del Win32 y el SDL de escritorio.

## 6. Recomendación

Abordar el port **usando el frontend SDL2 existente** como primer paso
(menor coste, máxima reutilización, ya probado). Si más adelante se quiere
una app más pequeña y nativa, migrar el backend de vídeo a
OpenGL ES / `ANativeWindow` y el de audio a AAudio, reutilizando el núcleo
sin cambios.

Estimación orientativa del primer paso (SDL2 en Android): árbol de proyecto
Gradle + `AndroidManifest` + `SDLActivity` + empaquetado del núcleo +
entrada táctil básica. El núcleo **no requiere cambios**.
