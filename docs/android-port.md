# Port a Android

Estado: **implementado** (v1.4.28). Este documento empezó como estudio de
viabilidad (v1.4.26) y ahora describe el port real. El núcleo no cambió:
el port es un frontend nuevo sobre el mismo C99.

## 1. Resumen

El port es **viable y de coste moderado**, y así se ha hecho. La razón es
que el proyecto ya estaba separado en dos capas con una frontera limpia:

- Un **núcleo de emulación** en C99 puro, sin dependencias de plataforma.
- Una **capa de presentación** por plataforma: SDL2 (Linux/macOS), Win32
  nativo (GDI/Direct3D9/WinMM), y ahora Android (OpenGL ES 2.0 + AudioTrack).

Android es, desde el punto de vista del código, una plataforma más: se
añadió un frontend (Android NDK + Java) que usa el mismo núcleo y un
backend de vídeo/audio nuevo. No hubo que reescribir el emulador.

## 1.1 Lo que se construyó (v1.4.28)

| Pieza | Archivos | Notas |
|---|---|---|
| Wrapper portable del frontend | `android/app/src/main/cpp/acore.{c,h}` | Ajustes, carga ROM/BIOS, estados, audio, conversión ARGB. **Sin dependencias de SO.** Compartido con el test de escritorio. |
| Puente JNI | `android/app/src/main/cpp/jni_bridge.c` | Traduce las llamadas Java a `acore`. Compila también en Linux para probarlo en una JVM. |
| Vídeo | `GbaRenderer.java` | OpenGL ES 2.0; textura 240x160, aspecto 3:2 con barras negras, GL_NEAREST (GL_LINEAR si se activa el suavizado). |
| Audio | `EmulatorThread.java` | `AudioTrack` en modo stream; el dispositivo es el reloj (bloquea en `write`), igual que los frontends de escritorio. |
| Controles táctiles | `TouchControlsView.java` | D-pad (zona circular con zona muerta), A/B, L/R, SELECT/START; multitáctil. |
| Explorador de archivos propio | `FilePickerActivity.java`, `RomStore.java`, `ZipRom.java` | UI propia sobre el **Storage Access Framework** (Android 11+ ignora `requestLegacyExternalStorage`): "Carpeta" concede acceso a una carpeta (permiso persistido), "Archivo" elige un documento suelto, "App" muestra la carpeta privada. Modos ROM (.gba/.agb/.bin/.zip/.7z) y BIOS (.bin/.rom/.gba). Los `.zip` se descomprimen con `ZipRom` (primer `.gba/.agb/.bin` de dentro). |
| Actividad principal | `MainActivity.java` | Barra superior, menú, reinicio, 10 ranuras de estado, toggles de sonido/scanline/suavizado, fast-forward, info de ROM. |
| Build | `android/app/src/main/cpp/CMakeLists.txt`, `build.gradle` | CMake vía NDK; ABIs `arm64-v8a`, `armeabi-v7a`, `x86_64`. |

## 1.2 Verificación

Sin dispositivo no se puede "ver" la pantalla, así que la prueba es dura y
reproducible: `tools/android_jni_test.sh` compila **el mismo**
`jni_bridge.c` + `acore.c` + núcleo para Linux, carga la `.so` desde una JVM
real y compara el CRC del frame que produce con el de la ruta de escritorio
(`tools/acore_test.c`). Resultado: **bit-idéntico** (mismo CRC para el mismo
ROM y mismo número de frames), y el número de muestras PCM coincide
exactamente (735 por frame). El CI además compila el APK y verifica que
contiene las tres ABIs.

`tools/android_zip_test.sh` cubre la carga de `.zip`: comprime una ROM, la
extrae con el **mismo** `ZipRom.java` que usa la app y comprueba que el
fichero extraído corre en el núcleo con el **mismo CRC** que el `.gba`
original. Los dos scripts corren en el CI.

## 2. Estado del código del núcleo (evidencia)

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

## 3. Cómo quedó resuelto cada punto

1. **Proyecto Android (NDK)**. Árbol `android/` con Gradle; `CMakeLists.txt`
   compila el núcleo + `acore.c` + `jni_bridge.c` como `libboycottadvance.so`
   (sin SDL, sin `main.c`/`win32.c`/`gui.c`/`video.c`).

2. **Backend de vídeo**: OpenGL ES 2.0 (`GbaRenderer.java`), una textura y un
   quad, aspecto 3:2 con barras negras. La UI táctil son `View`s superpuestos.

3. **Backend de audio**: `AudioTrack` en modo stream (`EmulatorThread.java`),
   consumiendo `acore_audio_mix`; el dispositivo marca el ritmo igual que en
   escritorio.

4. **Entrada**: `TouchControlsView` (D-pad, A/B, L/R, SELECT/START,
   multitáctil) → `acore_set_keys` (máscara activa en bajo).

5. **Almacenamiento / rutas**. El núcleo usa `fopen` y escribe el `.sav` y los
   savestates junto a la ROM, así que necesita una **ruta real**. Con Android
   11+ `File.listFiles()` sobre carpetas compartidas devuelve vacío (scoped
   storage), por lo que el explorador usa el **Storage Access Framework** y
   `RomStore` copia el documento elegido (o el `.gba` extraído del `.zip`) a
   `getFilesDir()/roms/`, que es siempre escribible sin permisos. El BIOS se
   elige igual; si no hay dump, el núcleo arranca con el fast-boot.

6. **Ciclo de vida**: `onPause`/`onResume` pausan emulación y audio; el `.sav`
   se guarda al pausar y al salir.

## 4. Por qué no se usó SDL2

Se evaluó reutilizar el frontend SDL2 de escritorio (`main.c`) con el port
oficial de SDL2 para Android, que sería el camino más corto. Se descartó:

- El requisito del proyecto es **cero SDL2 en Windows** y un frontend nativo;
  meter SDL2 por la puerta de Android contradice ese criterio y añade una
  dependencia grande.
- La UI de escritorio (barra de menús dibujada en `fb.c`) no encaja con
  Android: aquí la UI es táctil y nativa.
- El APK con la ruta nativa queda en ~200 KB sin dependencias externas.

Se eligió el backend propio (GLES2 + AudioTrack + SAF) reutilizando solo el
**núcleo**, que es lo que garantiza el comportamiento idéntico.

## 5. Riesgos y consideraciones

- **Rendimiento**: el emulador es un intérprete de CPU (1 ciclo/instrucción),
  ligero. En un ARM moderno sobra potencia; en dispositivos antiguos de
  32 bits habría que medir. El render del PPU es software (`ppu.c`), ya
  probado en escritorio.
- **Precisión de audio**: el ring buffer del APU usa `stdatomic` y un
  productor/consumidor; `AudioTrack.write` bloquea y sirve de reloj, igual
  que en escritorio.
- **Tamaño de la ROM en memoria**: el núcleo carga la ROM completa en RAM
  (`calloc` de `rom_size`); ROMs de 16-32 MB son normales, aceptable en
  Android actual.
- **BIOS**: por motivos legales no se distribuye; la app funciona sin él
  (fast-boot) o lo pide al usuario, igual que en escritorio.
- **Almacenamiento**: en Android 11+ no se puede listar una carpeta
  compartida sin SAF; por eso el explorador propio se apoya en
  `ACTION_OPEN_DOCUMENT_TREE` y el permiso se persiste.

## 6. Estado

Implementado y verificado en la medida en que se puede sin dispositivo:
APK construido, JNI y carga de `.zip` comprobados contra el núcleo de
escritorio con CRCs idénticos. Queda pendiente la prueba en un teléfono real
(pantalla táctil y audio). El núcleo **no requiere cambios**.
