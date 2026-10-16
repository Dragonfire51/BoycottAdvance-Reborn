# BoycottAdvance-Reborn — notas de depuración

Reimplementación limpia del emulador GBA BoycottAdvance 0.2.8 (C).
El usuario se comunica en español. Repo privado `Dragonfire51/BoycottAdvance-Reborn`.
`Pokemon Esmeralda.gba` (16 MB, gamecode BPES) vive en la raíz del repo.

## Estado (v1.4.1, tag `Latest` movido; version en src/gba.h)

- **CAUSA RAÍZ DE DOS QUEJAS DEL USUARIO (v1.4.1)**: existían DOS releases. GitHub marca como "latest" el release **`v1.4.0`** (id 414357160, tag `v1.4.0` = commit `8b48db5`), NO el release con tag `Latest`. Ese binario v1.4.0 es antiguo: subsistema **CONSOLE (3)** → abre ventana de consola, y escribe **`BoycottAdvance.ini`**. El release "Latest" (id 413237927) sí tiene el binario bueno (GUI subsystem 2, `boycottadvance-reborn.ini`). Por eso el usuario veía consola + ini viejo + controles viejos: descargaba el "Latest" de GitHub = v1.4.0. **Fix**: subir versión, reconstruir y publicar el release `Latest` como la versión más nueva, y mover el tag a ese commit.
- **FIX COMMITEADO (controles/ini, v1.4.1)**: los binarios Windows de v1.4.0 se construyeron desde `8b48db5`, ANTES de `ccec1d1` (que introdujo el menú Controls/joystick) y de `885e9ee` (que cambió el ini a reborn y el subsistema GUI). Por eso esos binarios no tienen el diálogo nuevo. **Lección**: al publicar, compilar SIEMPRE desde el commit del tag de release, no reutilizar exes antiguos.
- **HECHO (v1.4.1)**: `Options > Controls > Input Configuration...` abre un diálogo modal nativo (`gui_native_input_config` en gui.c) con páginas Keyboard/Joystick, una fila por botón GBA (Button A/B/L/R, SELECT, START, UP, DOWN, LEFT, RIGHT), OK/Cancel y captura de tecla/botón. Sustituye los items "Map X" del submenú. Callbacks nuevos en `GuiCallbacks`: `key_get/key_set/joy_get/joy_set`.
- **HECHO (v1.4.1)**: Help→History muestra los cambios de v1.4.1 y v1.4.0 (siempre los de la última versión).
- **HECHO**: GUI nativa Win32 (v1.3.2), renderers GDI/D3D9/Vulkan en Option→Frame Buffer, builds win32/win64, GUI Unicode (v1.3.3).
- **FIX COMMITEADO**: Thumb format 9/10 (LDR/STR/LDRB/STRH inmediate) usaban bits[8:6] como base; debe ser bits[5:3] (Rn). Sin esto, Pokémon Esmeralda se quedaba en el busy-wait VCOUNT de `0x082E7DD0` (`ldrb r0,[r1]` con r1=0x04000006 leía de r0, nunca veía 0x9F) → pantalla negra infinita. Ahora pasa la inicialización.
- **FIX COMMITEADO (controles)**: `ini_get()` comparaba por prefijo, así que `Key_R`/`Key_L` colisionaban con `Key_Right`/`Key_Left` y los remaps R/L se perdían al cargar. Ahora exige delimitador tras el nombre. Submenú nativo Joystick con remap/reset y D-pad (POV). `M_RESETJOY` solapaba con `M_JOY0` (id duplicado).
- **FIX COMMITEADO (núcleo)**: `dma_run` decodificaba mal los bits de control de dirección (0=incremento, 1=decremento, 2=fijo). SRAM/Flash de cartucho en `0x0E000000` se leía/escribía sobre el array de ROM (las escrituras se descartaban) → las partidas no se guardaban. `CpuSet` enmascaraba el contador a 20 bits (son 21). Los buffers de save/load state eran locales de 1 MB → desbordaban la pila del hilo y crasheaban; ahora son `static`.
- **FIX COMMITEADO (ajustes BIOS)**: los toggles BIOS/SkipIntro solo cambiaban el flag; ahora re-ejecutan `gba_reset()` si hay ROM cargada, y `skip_intros` se copia de la config al núcleo en el arranque (antes nunca se leía).
- **PENDIENTE (pantalla blanca/crash Pokémon Esmeralda)**: el núcleo se queda atascado en la inicialización; en 400 frames `DISPCNT` sigue en `0000` y `0x03003090` nunca recibe código, así que nunca se configura el vídeo. El bucle visible es el *busy-wait* de `0x082E9040` (`ldrh r0,[r1]; subs r0,#1; ldrh r0,[r1]; strh r0,[r1]; cmp r0,#0; bne`). El PC acaba en ARM sobre `0x03003090` (región a cero) vía el epílogo `pop {r4-r7}; pop {r1}; bx r1` de `0x0800B75C`; el valor `0x03003090` (bit 0 = 0 → ARM) se carga de `[0x03007DC0]`. Antes de tocar `bx`/`LDM` hay que confirmar por qué ese valor está en la pila y por qué el bloque de código esperado en `0x03003090` no se copia (en el log hay un `CpuSet` con `src` fijo `0x03007DF0` → `0x03003090`, que escribe 80 bytes; puede ser un borrado deliberado). `swi_register_ram_reset` (SWI 0x01) sigue siendo un no-op.


## Harness de depuración

Compilar: `gcc -std=c99 -O2 -Isrc tools/trace_iwram.c src/gba.c src/mem.c src/arm.c src/ppu.c src/apu.c src/bios.c -lm -o /tmp/pk12`
Ejecutar: `/tmp/pk12 g x "Pokemon Esmeralda.gba"` (o `/tmp/pk12 600 /tmp/pokemon.gba`).
Atrapa la primera exec en IWRAM e imprime ring buffer + registros.